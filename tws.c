/*
 * tws.c — Telematics Web Server (TWS)
 * Servidor web HTTP/1.1 con soporte GET, HEAD, POST
 *
 * Compilar: gcc -o server tws.c -lpthread
 * Uso:      ./server <HTTP_PORT> <LogFile> <DocumentRootFolder>
 * Ejemplo:  ./server 8081 tws.log /var/www/html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <fcntl.h>

/* ────────────────────────────────
   CONSTANTES
   ──────────────────────────────── */
#define BACKLOG          128
#define BUFFER_SIZE      8192
#define MAX_REQUEST_SIZE 65536
#define MAX_PATH_LEN     1024
#define MAX_URI_LEN      512

/* ────────────────────────────────
   TIPOS
   ──────────────────────────────── */
typedef struct {
    char method[8];
    char uri[MAX_URI_LEN];
    char version[16];
    char host[256];
    int  content_length;
    int  header_end;     /* offset donde termina la cabecera en raw_buf */
} HttpRequest;

typedef struct {
    int                client_fd;
    struct sockaddr_in client_addr;
    char               doc_root[MAX_PATH_LEN];
    char               log_file_path[MAX_PATH_LEN];
} TwsClientCtx;

/* ────────────────────────────────
   VARIABLES GLOBALES
   ──────────────────────────────── */
static char g_doc_root[MAX_PATH_LEN];
static char g_log_path[MAX_PATH_LEN];
static int  g_port;
static FILE *g_log_fp = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ────────────────────────────────
   PROTOTIPOS
   ──────────────────────────────── */
int   create_server_socket(int port);
void *handle_client(void *arg);
int   recv_full_request(int fd, char *buf, int buf_size);
int   parse_request(const char *raw, int len, HttpRequest *req);
void  handle_get(int fd, HttpRequest *req);
void  handle_head(int fd, HttpRequest *req);
void  handle_post(int fd, HttpRequest *req, const char *raw, int raw_len);
void  send_response(int fd, int code, const char *reason,
                    const char *content_type, const char *body, size_t body_len);
void  send_file_response(int fd, const char *filepath, int send_body);
void  send_error(int fd, int code, const char *reason);
const char *get_mime_type(const char *path);
void  tws_log(const char *level, const char *fmt, ...);
void  resolve_path(const char *uri, char *out, size_t out_size);

/* ────────────────────────────────
   MAIN
   ──────────────────────────────── */
int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <HTTP_PORT> <LogFile> <DocumentRootFolder>\n", argv[0]);
        return EXIT_FAILURE;
    }

    g_port = atoi(argv[1]);
    strncpy(g_log_path, argv[2], sizeof(g_log_path) - 1);
    strncpy(g_doc_root, argv[3], sizeof(g_doc_root) - 1);

    /* Quitar '/' final del doc_root si lo tiene */
    size_t dr_len = strlen(g_doc_root);
    if (dr_len > 1 && g_doc_root[dr_len - 1] == '/')
        g_doc_root[dr_len - 1] = '\0';

    g_log_fp = fopen(g_log_path, "a");
    if (!g_log_fp) { perror("fopen log"); return EXIT_FAILURE; }

    tws_log("INFO", "TWS iniciando en puerto %d, doc_root=%s", g_port, g_doc_root);

    int server_fd = create_server_socket(g_port);
    if (server_fd < 0) return EXIT_FAILURE;

    printf("[TWS] Escuchando en :%d  root=%s\n", g_port, g_doc_root);

    while (1) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(server_fd, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) { if (errno == EINTR) continue; perror("accept"); continue; }

        TwsClientCtx *ctx = malloc(sizeof(TwsClientCtx));
        if (!ctx) { close(cfd); continue; }
        ctx->client_fd   = cfd;
        ctx->client_addr = caddr;
        snprintf(ctx->doc_root,      sizeof(ctx->doc_root),      "%s", g_doc_root);
        snprintf(ctx->log_file_path, sizeof(ctx->log_file_path), "%s", g_log_path);

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, ctx) != 0) {
            perror("pthread_create"); free(ctx); close(cfd);
        } else {
            pthread_detach(tid);
        }
    }

    fclose(g_log_fp);
    close(server_fd);
    return EXIT_SUCCESS;
}

/* ────────────────────────────────
   SOCKET SERVIDOR
   ──────────────────────────────── */
int create_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(fd); return -1;
    }
    if (listen(fd, BACKLOG) < 0) {
        perror("listen"); close(fd); return -1;
    }
    return fd;
}

/* ────────────────────────────────
   HILO POR CLIENTE
   ──────────────────────────────── */
void *handle_client(void *arg) {
    TwsClientCtx *ctx = (TwsClientCtx *)arg;
    int fd = ctx->client_fd;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ctx->client_addr.sin_addr, ip, sizeof(ip));
    free(ctx);

    char raw[MAX_REQUEST_SIZE];
    int n = recv_full_request(fd, raw, sizeof(raw));
    if (n <= 0) { close(fd); return NULL; }

    HttpRequest req;
    memset(&req, 0, sizeof(req));
    if (parse_request(raw, n, &req) != 0) {
        tws_log("WARN", "[%s] Petición inválida", ip);
        send_error(fd, 400, "Bad Request");
        close(fd);
        return NULL;
    }

    tws_log("INFO", "[%s] %s %s %s", ip, req.method, req.uri, req.version);

    if (strcmp(req.method, "GET") == 0) {
        handle_get(fd, &req);
    } else if (strcmp(req.method, "HEAD") == 0) {
        handle_head(fd, &req);
    } else if (strcmp(req.method, "POST") == 0) {
        handle_post(fd, &req, raw, n);
    } else {
        send_error(fd, 400, "Method Not Supported");
    }

    close(fd);
    return NULL;
}

/* ────────────────────────────────
   LEER PETICIÓN COMPLETA
   ──────────────────────────────── */
int recv_full_request(int fd, char *buf, int buf_size) {
    int total = 0;
    int header_end = -1;

    while (total < buf_size - 1) {
        int n = recv(fd, buf + total, buf_size - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';

        /* Detectar fin de cabeceras */
        if (header_end < 0) {
            char *end = strstr(buf, "\r\n\r\n");
            if (end) {
                header_end = (int)(end - buf) + 4;

                /* Leer Content-Length para saber si hay body */
                int content_length = 0;
                char *cl = strcasestr(buf, "Content-Length:");
                if (cl) {
                    content_length = atoi(cl + 15);
                }

                /* Si no hay body o ya lo tenemos completo, salir */
                int body_received = total - header_end;
                if (content_length <= 0 || body_received >= content_length) {
                    break;
                }

                /* Seguir leyendo el body hasta completarlo o agotar el buffer */
                int body_remaining = content_length - body_received;
                while (body_remaining > 0 && total < buf_size - 1) {
                    int space = buf_size - 1 - total;
                    int to_read = (body_remaining < space) ? body_remaining : space;
                    int r = recv(fd, buf + total, to_read, 0);
                    if (r <= 0) break;
                    total += r;
                    buf[total] = '\0';
                    body_remaining -= r;
                }
                break;
            }
        }
    }
    return total;
}

/* ────────────────────────────────
   PARSEO HTTP REQUEST
   ──────────────────────────────── */
int parse_request(const char *raw, int len, HttpRequest *req) {
    (void)len;
    if (sscanf(raw, "%7s %511s %15s", req->method, req->uri, req->version) != 3)
        return -1;

    /* Validar versión */
    if (strncmp(req->version, "HTTP/", 5) != 0) return -1;

    /* Extraer Content-Length */
    const char *cl = strcasestr(raw, "Content-Length:");
    if (cl) req->content_length = atoi(cl + 15);

    /* Calcular offset del body */
    const char *end = strstr(raw, "\r\n\r\n");
    req->header_end = end ? (int)(end - raw) + 4 : len;

    return 0;
}

/* ────────────────────────────────
   RESOLVER URI → RUTA EN DISCO
   ──────────────────────────────── */
void resolve_path(const char *uri, char *out, size_t out_size) {
    /* Quitar query string */
    char clean_uri[MAX_URI_LEN];
    strncpy(clean_uri, uri, sizeof(clean_uri) - 1);
    char *q = strchr(clean_uri, '?');
    if (q) *q = '\0';

    /* Cualquier URI que termine en '/' apunta a index.html de ese directorio.
       Antes solo se manejaba "/", pero "/caso1/" llegaba como directorio
       y stat() lo encontraba pero S_ISREG fallaba → 404 incorrecto.
       Ejemplos:  /        → /index.html
                  /caso1/  → /caso1/index.html
                  /caso4/  → /caso4/index.html               */
    size_t ulen = strlen(clean_uri);
    if (ulen > 0 && clean_uri[ulen - 1] == '/') {
        /* Append "index.html" al final (hay espacio: MAX_URI_LEN - ulen) */
        strncat(clean_uri, "index.html", sizeof(clean_uri) - ulen - 1);
    }

    snprintf(out, out_size, "%s%s", g_doc_root, clean_uri);
}

/* ────────────────────────────────
   HANDLER GET
   ──────────────────────────────── */
void handle_get(int fd, HttpRequest *req) {
    char path[MAX_PATH_LEN];
    resolve_path(req->uri, path, sizeof(path));
    send_file_response(fd, path, 1 /* enviar body */);
}

/* ────────────────────────────────
   HANDLER HEAD (igual que GET pero sin body)
   ──────────────────────────────── */
void handle_head(int fd, HttpRequest *req) {
    char path[MAX_PATH_LEN];
    resolve_path(req->uri, path, sizeof(path));
    send_file_response(fd, path, 0 /* no enviar body */);
}

/* ────────────────────────────────
   HANDLER POST
   Actualmente solo devuelve 200 OK con un eco del body recibido.
   ──────────────────────────────── */
void handle_post(int fd, HttpRequest *req, const char *raw, int raw_len) {
    /* Extraer el body: empieza después del doble CRLF */
    const char *body = raw + req->header_end;
    int body_len = raw_len - req->header_end;
    if (body_len < 0) body_len = 0;

    /* Verificar coherencia: si Content-Length fue declarado y no lo recibimos
       completo, responder con 400 Bad Request */
    if (req->content_length > 0 && body_len < req->content_length) {
        tws_log("WARN", "POST %s: body incompleto (recibido %d de %d bytes)",
                req->uri, body_len, req->content_length);
        send_error(fd, 400, "Bad Request");
        return;
    }

    /* Usar el tamaño real: el menor entre lo declarado y lo recibido */
    int effective_len = (req->content_length > 0 && req->content_length < body_len)
                        ? req->content_length
                        : body_len;

    tws_log("INFO", "POST %s body_len=%d", req->uri, effective_len);

    /* Construir respuesta: eco del body recibido (hasta 200 chars en el HTML) */
    char response_body[1024];
    snprintf(response_body, sizeof(response_body),
             "<html><body>"
             "<h1>POST recibido</h1>"
             "<p>URI: %s</p>"
             "<p>Body (%d bytes): %.*s</p>"
             "</body></html>",
             req->uri,
             effective_len,
             effective_len > 200 ? 200 : effective_len,
             body);

    send_response(fd, 200, "OK", "text/html",
                  response_body, strlen(response_body));
}

/* ────────────────────────────────
   ENVIAR ARCHIVO AL CLIENTE
   ──────────────────────────────── */
void send_file_response(int fd, const char *filepath, int send_body) {
    struct stat st;
    if (stat(filepath, &st) < 0 || !S_ISREG(st.st_mode)) {
        send_error(fd, 404, "Not Found");
        tws_log("WARN", "404 %s", filepath);
        return;
    }

    const char *mime = get_mime_type(filepath);
    long file_size   = (long)st.st_size;

    /* Cabeceras */
    char headers[512];
    int hlen = snprintf(headers, sizeof(headers),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        mime, file_size);

    send(fd, headers, hlen, 0);
    tws_log("INFO", "200 OK %s (%ld bytes, %s)", filepath, file_size, mime);

    if (!send_body) return;   /* HEAD: solo cabeceras */

    /* Body: leer y enviar en bloques */
    FILE *f = fopen(filepath, "rb");
    if (!f) { tws_log("ERROR", "fopen %s: %s", filepath, strerror(errno)); return; }

    char buf[BUFFER_SIZE];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        int sent = 0;
        while (sent < (int)n) {
            int s = send(fd, buf + sent, n - sent, 0);
            if (s <= 0) goto done;
            sent += s;
        }
    }
done:
    fclose(f);
}

/* ────────────────────────────────
   ENVIAR RESPUESTA GENÉRICA
   ──────────────────────────────── */
void send_response(int fd, int code, const char *reason,
                   const char *content_type, const char *body, size_t body_len) {
    char headers[512];
    int hlen = snprintf(headers, sizeof(headers),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        code, reason, content_type, body_len);
    send(fd, headers, hlen, 0);
    if (body && body_len > 0)
        send(fd, body, body_len, 0);
    tws_log("INFO", "%d %s", code, reason);
}

/* ────────────────────────────────
   ENVIAR ERROR HTTP
   ──────────────────────────────── */
void send_error(int fd, int code, const char *reason) {
    char body[256];
    snprintf(body, sizeof(body),
             "<html><body><h1>%d %s</h1></body></html>", code, reason);
    send_response(fd, code, reason, "text/html", body, strlen(body));
}

/* ────────────────────────────────
   MIME TYPES
   Podemos añadir más tipos según los recursos de la aplicación.
   ──────────────────────────────── */
const char *get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0)
        return "text/html";
    if (strcasecmp(ext, ".css")  == 0) return "text/css";
    if (strcasecmp(ext, ".js")   == 0) return "application/javascript";
    if (strcasecmp(ext, ".json") == 0) return "application/json";
    if (strcasecmp(ext, ".png")  == 0) return "image/png";
    if (strcasecmp(ext, ".jpg")  == 0 || strcasecmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    if (strcasecmp(ext, ".gif")  == 0) return "image/gif";
    if (strcasecmp(ext, ".ico")  == 0) return "image/x-icon";
    if (strcasecmp(ext, ".svg")  == 0) return "image/svg+xml";
    if (strcasecmp(ext, ".txt")  == 0) return "text/plain";
    if (strcasecmp(ext, ".pdf")  == 0) return "application/pdf";
    if (strcasecmp(ext, ".xml")  == 0) return "application/xml";
    if (strcasecmp(ext, ".webp") == 0) return "image/webp";
    if (strcasecmp(ext, ".mp4")  == 0) return "video/mp4";
    if (strcasecmp(ext, ".webm") == 0) return "video/webm";
    if (strcasecmp(ext, ".mp3")  == 0) return "audio/mpeg";
    if (strcasecmp(ext, ".woff") == 0) return "font/woff";
    if (strcasecmp(ext, ".woff2")== 0) return "font/woff2";
    return "application/octet-stream";
}

/* ────────────────────────────────
   LOGGER
   ──────────────────────────────── */
void tws_log(const char *level, const char *fmt, ...) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);

    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    pthread_mutex_lock(&g_log_mutex);
    printf("[%s][%s] %s\n", ts, level, msg);
    fflush(stdout);
    if (g_log_fp) {
        fprintf(g_log_fp, "[%s][%s] %s\n", ts, level, msg);
        fflush(g_log_fp);
    }
    pthread_mutex_unlock(&g_log_mutex);
}
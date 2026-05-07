/*
 * PIBL - Proxy Inverso + Balanceador de Carga
 * Proyecto I - Telemática/Internet: Arquitectura y Protocolos
 *
 * Compilar: gcc -o pibl pibl.c -lpthread -o pibl
 * Uso:      ./pibl [config_file]
 *           Por defecto usa pibl.conf
 */

#include "pibl.h"

/* Variables globales */
ServerConfig config;
pthread_mutex_t log_mutex   = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rr_mutex    = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;
int current_backend = 0;  /* índice Round Robin */
FILE *log_file = NULL;

/* ─────────────────────────────────────────────
   MAIN
   ───────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    const char *config_path = (argc > 1) ? argv[1] : "pibl.conf";

    printf("[PIBL] Cargando configuración desde: %s\n", config_path);
    if (load_config(config_path) != 0) {
        fprintf(stderr, "[ERROR] No se pudo cargar la configuración.\n");
        return EXIT_FAILURE;
    }

    /* Abrir archivo de log */
    log_file = fopen(config.log_file, "a");
    if (!log_file) {
        perror("[ERROR] No se pudo abrir el archivo de log");
        return EXIT_FAILURE;
    }

    log_message(LOG_INFO, "PIBL iniciando en puerto %d con %d backends, TTL=%ds",
                config.port, config.backend_count, config.cache_ttl);

    /* Crear directorio de caché si no existe */

if (mkdir(CACHE_DIR, 0755) < 0 && errno != EEXIST) {
    log_message(LOG_ERROR, "No se pudo crear directorio cache");
}
    /* Crear socket de escucha */
    int server_fd = create_server_socket(config.port);
    if (server_fd < 0) {
        log_message(LOG_ERROR, "No se pudo crear el socket servidor.");
        fclose(log_file);
        return EXIT_FAILURE;
    }

    log_message(LOG_INFO, "Escuchando en puerto %d...", config.port);
    printf("[PIBL] Listo. Escuchando en puerto %d\n", config.port);

    /* Bucle principal: aceptar conexiones */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;   /* señal interrumpida, reintentar */
            perror("[ERROR] accept");
            continue;
        }

        /* Crear contexto para el hilo */
        ClientContext *ctx = malloc(sizeof(ClientContext));
        if (!ctx) {
            perror("[ERROR] malloc ClientContext");
            close(client_fd);
            continue;
        }
        ctx->client_fd   = client_fd;
        ctx->client_addr = client_addr;

        /* Lanzar hilo para manejar la conexión */
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client_thread, ctx) != 0) {
            perror("[ERROR] pthread_create");
            free(ctx);
            close(client_fd);
            continue;
        }
        pthread_detach(tid);  /* el hilo libera sus propios recursos */
    }

    close(server_fd);
    fclose(log_file);
    return EXIT_SUCCESS;
}

/* ─────────────────────────────────────────────
   CARGA DE CONFIGURACIÓN
   Lee pibl.conf con formato clave=valor
   ───────────────────────────────────────────── */
int load_config(const char *path) {
    memset(&config, 0, sizeof(config));
    /* Valores por defecto */
    config.port      = DEFAULT_PORT;
    config.cache_ttl = DEFAULT_TTL;
    strncpy(config.log_file, "pibl.log", sizeof(config.log_file) - 1);

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "[WARN] No se encontró %s, usando defaults.\n", path);
        /* backends por defecto: local*/
        strncpy(config.backends[0].host, "127.0.0.1", sizeof(config.backends[0].host) - 1);
        config.backends[0].port = 8081;
        config.backend_count = 1;
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* Ignorar comentarios y líneas vacías */
        if (line[0] == '#' || line[0] == '\n') continue;

        char key[64], value[192];
        if (sscanf(line, "%63[^=]=%191s", key, value) != 2) continue;

        /* Quitar espacios al final de key */
        char *k = key + strlen(key) - 1;
        while (k > key && isspace((unsigned char)*k)) *k-- = '\0';

        if (strcmp(key, "port") == 0) {
            config.port = atoi(value);
        } else if (strcmp(key, "log_file") == 0) {
            strncpy(config.log_file, value, sizeof(config.log_file) - 1);
        } else if (strcmp(key, "cache_ttl") == 0) {
            config.cache_ttl = atoi(value);
        } else if (strcmp(key, "backend") == 0) {
            /* formato: backend=host:port */
            if (config.backend_count < MAX_BACKENDS) {
                char host[128];
                int  port = 80;
                if (sscanf(value, "%127[^:]:%d", host, &port) >= 1) {
                    /* snprintf en lugar de strncpy: garantiza null-termination
                       y no genera el warning de truncación porque el tamaño
                       del formato (%127) ya limita la fuente a 127 chars,
                       que caben con seguridad en el destino de 128. */
                    snprintf(config.backends[config.backend_count].host,
                             sizeof(config.backends[0].host),
                             "%s", host);
                    config.backends[config.backend_count].port = port;
                    config.backend_count++;
                }
            }
        }
    }

    fclose(f);

    if (config.backend_count == 0) {
        fprintf(stderr, "[ERROR] No se definió ningún backend en %s\n", path);
        return -1;
    }
    return 0;
}

/* ─────────────────────────────────────────────
   CREACIÓN DEL SOCKET SERVIDOR
   ───────────────────────────────────────────── */
int create_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    /* Reusar dirección para evitar TIME_WAIT */
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

/* ─────────────────────────────────────────────
   HILO POR CLIENTE
   ───────────────────────────────────────────── */
void *handle_client_thread(void *arg) {
    ClientContext *ctx = (ClientContext *)arg;
    int client_fd = ctx->client_fd;
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ctx->client_addr.sin_addr, client_ip, sizeof(client_ip));
    free(ctx);
        /* Establecer timeout de recepción para evitar bloqueos indefinidos */
    struct timeval tv;
    tv.tv_sec  = 5;  // timeout de 5 segundos
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    log_message(LOG_INFO, "Nueva conexión de %s", client_ip);

    /* Leer la petición HTTP del cliente */
    HttpRequest req;
    memset(&req, 0, sizeof(req));

    char raw_request[MAX_REQUEST_SIZE];
    int bytes_read = recv_http_request(client_fd, raw_request, sizeof(raw_request));
    if (bytes_read <= 0) {
        log_message(LOG_WARN, "Petición vacía o error leyendo de %s", client_ip);
        close(client_fd);
        return NULL;
    }

    log_message(LOG_INFO, "[%s] Petición recibida (%d bytes)", client_ip, bytes_read);

    /* Parsear la petición */
    if (parse_http_request(raw_request, bytes_read, &req) != 0) {
        log_message(LOG_WARN, "[%s] Petición HTTP inválida", client_ip);
        send_error_response(client_fd, 400, "Bad Request");
        close(client_fd);
        return NULL;
    }

    log_message(LOG_INFO, "[%s] %s %s HTTP/1.1", client_ip, req.method, req.uri);

    /* ── Caché: verificar si existe el recurso en disco ── */
    char cache_path[512];
    build_cache_path(req.uri, cache_path, sizeof(cache_path));

    if (is_cache_valid(cache_path) && strcmp(req.method, "GET") == 0) {
        log_message(LOG_INFO, "[CACHE HIT] %s", req.uri);
        serve_from_cache(client_fd, cache_path);  // servir desde caché y cerrar conexión
        close(client_fd);
        return NULL;
    }

    /* ── Round Robin: elegir backend ── */
    Backend *backend = select_backend();
    log_message(LOG_INFO, "Forwarding a %s:%d", backend->host, backend->port);

    /* ── Conectar al backend y reenviar petición ── */
    int backend_fd = connect_to_backend(backend);
    if (backend_fd < 0) {
        log_message(LOG_ERROR, "No se pudo conectar al backend %s:%d", backend->host, backend->port);
        send_error_response(client_fd, 502, "Bad Gateway");
        close(client_fd);
        return NULL;
    }

    /* Reenviar la petición tal cual al backend */
    if (send(backend_fd, raw_request, bytes_read, 0) < 0) {
        perror("send to backend");
        send_error_response(client_fd, 502, "Bad Gateway");
        close(backend_fd);
        close(client_fd);
        return NULL;
    }

    /* ── Recibir respuesta del backend y reenviar al cliente ── */
    relay_response(backend_fd, client_fd, cache_path, req.method);

    close(backend_fd);
    close(client_fd);
    return NULL;
}

/* ─────────────────────────────────────────────
   RECIBIR PETICIÓN HTTP COMPLETA
   Lee hasta encontrar \r\n\r\n (fin de headers)
   y si hay Content-Length, lee el body también.
   ───────────────────────────────────────────── */
int recv_http_request(int fd, char *buf, int buf_size) {
    int total = 0;
    int header_end = -1;

    while (total < buf_size - 1) {
        int n = recv(fd, buf + total, buf_size - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';

        /* Buscar fin de headers */
        if (header_end < 0) {
            char *end = strstr(buf, "\r\n\r\n");
            if (end) {
                header_end = (int)(end - buf) + 4;

                /* Parsear Content-Length para saber cuánto body leer */
                int content_length = 0;
                char *cl = strcasestr(buf, "Content-Length:");
                if (cl) {
                    content_length = atoi(cl + 15);
                }

                /* Si no hay body (GET, HEAD) o ya lo recibimos todo, salir */
                int body_received = total - header_end;
                if (content_length <= 0 || body_received >= content_length) {
                    break;
                }

                /* Seguir leyendo hasta completar el body o llenar el buffer */
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

/* ─────────────────────────────────────────────
   PARSEO DE HTTP REQUEST
   Extrae método, URI, versión y headers básicos
   ───────────────────────────────────────────── */
int parse_http_request(const char *raw, int len, HttpRequest *req) {
    (void)len;
    /* Request-Line: METHOD SP URI SP HTTP/version CRLF */
    if (sscanf(raw, "%7s %511s %15s", req->method, req->uri, req->version) != 3)
        return -1;

    /* Validar método */
    if (strcmp(req->method, "GET")  != 0 &&
        strcmp(req->method, "HEAD") != 0 &&
        strcmp(req->method, "POST") != 0) {
        return -1;  /* método no soportado */
    }

    /* Extraer cabecera Host */
    const char *host_hdr = strcasestr(raw, "Host:");
    if (host_hdr) {
        sscanf(host_hdr + 5, " %255s", req->host);
    }

    /* Extraer Content-Length (necesario para POST) */
    const char *cl_hdr = strcasestr(raw, "Content-Length:");
    if (cl_hdr) {
        req->content_length = atoi(cl_hdr + 15);
    }

    /* Apuntar al body (después de \r\n\r\n) */
    const char *body_start = strstr(raw, "\r\n\r\n");
    if (body_start) req->body = body_start + 4;

    return 0;
}

/* ─────────────────────────────────────────────
   ROUND ROBIN
   ───────────────────────────────────────────── */
Backend *select_backend(void) {
    pthread_mutex_lock(&rr_mutex);
    int idx = current_backend;
    current_backend = (current_backend + 1) % config.backend_count;
    pthread_mutex_unlock(&rr_mutex);
    return &config.backends[idx];
}

/* ─────────────────────────────────────────────
   CONEXIÓN AL BACKEND
   ───────────────────────────────────────────── */
int connect_to_backend(Backend *backend) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket backend"); return -1; }

    struct hostent *he = gethostbyname(backend->host);
    if (!he) {
        fprintf(stderr, "[ERROR] gethostbyname(%s) falló\n", backend->host);
        close(fd);
        return -1;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(backend->port)
    };
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect to backend");
        close(fd);
        return -1;
    }
    return fd;
}

/* Verifica si un método es cacheable, solo para GET con status 200 */
int is_method_cacheable(const char *method, int status_code) {

    return (strcmp(method, "GET") == 0) && (status_code == 200);
}

/* ─────────────────────────────────────────────
   RELAY DE RESPUESTA: backend → cliente (+ caché)
   ───────────────────────────────────────────── */
void relay_response(int backend_fd, int client_fd, const char *cache_path, const char *method) {
    char buf[BUFFER_SIZE];
    char tmp_path[600];

    int  n;
    int status_code = 0;
    FILE *cache_fp = NULL;
    char last_modified[128] = "";

    /*Verificar status de respuesta leyendo el primer chunk*/
    n = recv(backend_fd, buf, sizeof(buf) - 1, 0); 
    if (n <= 0) return;
    buf[n] = '\0';
    sscanf(buf, "%*s %d", &status_code);  // extraer código de status



        // Extraer Last-Modified del primer chunk
    char *lm = strcasestr(buf, "Last-Modified:");
    if (lm) {
        /* Extraer el valor de Last-Modified si lo encuentra, limitando a 127 chars para evitar overflow */
        sscanf(lm + 14, " %127[^\r\n]", last_modified);

    }
    /* En caso de ser un metodo Head comparamos el last modified para ver si se cambio el cache*/
    if(strcmp(method, "HEAD") == 0) {
    comparar_last_modified(last_modified, cache_path);
    // Enviar solo los headers al cliente
    send(client_fd, buf, n, 0);
    return;

    }

/* verificar si el método es cacheable , solo para GET con status 200 */
    if (is_method_cacheable(method, status_code)) {
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", cache_path); 
        cache_fp = fopen(tmp_path, "wb");
        if (!cache_fp) {
            log_message(LOG_WARN, "No se pudo abrir tmp cache: %s", tmp_path);
        }
    }

    /* Enviar el primer chunk al cliente y guardar en caché si aplica */
    send(client_fd, buf, n, 0);
    if (cache_fp) fwrite(buf, 1, n, cache_fp);


    while ((n = recv(backend_fd, buf, sizeof(buf), 0)) > 0) {
        /* Enviar al cliente */
        int sent = 0;
        while (sent < n) {
            int s = send(client_fd, buf + sent, n - sent, 0);
        if (s <= 0) goto done; 
            sent += s;
        }
        /* Guardar en caché */
        if (cache_fp) fwrite(buf, 1, n, cache_fp);
    }

    done:

        if (cache_fp) {
        fclose(cache_fp);

        // Si otro hilo ya terminó primero, rename lo sobreescribe
        if (rename(tmp_path, cache_path) == 0) {
            write_cache_timestamp(cache_path, last_modified);
            log_message(LOG_INFO, "[CACHE WRITE] %s", cache_path);
        } else {
            // Si rename falla, limpiar el tmp
            log_message(LOG_WARN, "rename falló: %s → %s", tmp_path, cache_path);
            remove(tmp_path);
        }
    }
}


void comparar_last_modified(const char *last_modified_original,
     const char *cache_path) {
        char ts_path[600];
    snprintf(ts_path, sizeof(ts_path), "%s.ts", cache_path);

    FILE *f = fopen(ts_path, "r");
    if (!f) return;

    long cached_time = 0;  // leemos el cached_time para mantener el formato del archivo, aunque no lo usemos aquí
    char last_modified_ts[128];
    int fields_read = fscanf(f, "%ld %127[^\r\n]", &cached_time, last_modified_ts);

    fclose(f);
    if (fields_read != 2 ) return;
    if (last_modified_original && last_modified_ts) {
        if (strcmp(last_modified_original, last_modified_ts) != 0) {
            /* El recurso en el backend cambió desde que se cacheó: invalidar caché */
            remove(cache_path);
            remove(ts_path);  
            log_message(LOG_INFO, "[CACHE INVALIDATED] %s (Last-Modified cambió)", cache_path);
        }
    }
}

/* ─────────────────────────────────────────────
   CACHÉ: construir ruta de archivo
   URI "/" → cache/root.cache
   URI "/foo/bar.html" → cache/foo_bar.html.cache
   ───────────────────────────────────────────── */
void build_cache_path(const char *uri, char *out, size_t out_size) {
    char sanitized[512];
    strncpy(sanitized, uri, sizeof(sanitized) - 1);

    /* Reemplazar '/' por '_' */
    for (int i = 0; sanitized[i]; i++) {
        if (sanitized[i] == '/') sanitized[i] = '_';
        /* Evitar caracteres peligrosos en nombre de archivo */
        if (sanitized[i] == '.' && i > 0 && sanitized[i-1] == '.') sanitized[i] = '_';
    }

    /* URI raíz */
    if (strlen(sanitized) == 0 || strcmp(sanitized, "_") == 0)
        strncpy(sanitized, "root", sizeof(sanitized) - 1);

    snprintf(out, out_size, "%s/%s.cache", CACHE_DIR, sanitized);
}


/* Verifica si el caché existe y su TTL no expiró */
int is_cache_valid(const char *cache_path) {
    char ts_path[600];
    snprintf(ts_path, sizeof(ts_path), "%s.ts", cache_path);

    FILE *f = fopen(ts_path, "r");
    if (!f) return 0;

    time_t cached_time = 0;

    int fields_read = fscanf(f, "%ld", &cached_time);

    fclose(f);
    if (fields_read != 1) return 0;

    if (difftime(time(NULL), cached_time) > config.cache_ttl) {
        /* TTL expirado: eliminar archivos */
        remove(cache_path);
        remove(ts_path);
        return 0;
    }
        

    /* Verificar que el archivo de contenido también existe */
    FILE *cf = fopen(cache_path, "rb");
    if (!cf) return 0;
    fclose(cf);

    return 1;
}

/* Escribe timestamp del caché y Last-Modified del recurso */
void write_cache_timestamp(const char *cache_path, const char *last_modified) {
    char ts_path[600];
    snprintf(ts_path, sizeof(ts_path), "%s.ts", cache_path);

    if (last_modified == NULL) {
        /* Manejo de caso donde no se proporciona Last-Modified */
        last_modified = "";
    }

    FILE *f = fopen(ts_path, "w");
    if (f) {
        fprintf(f, "%ld %s", (long)time(NULL), last_modified);
        fclose(f);
    }
}

/* Sirve un recurso desde caché al cliente */
void serve_from_cache(int client_fd, const char *cache_path) {
    FILE *f = fopen(cache_path, "rb");
    if (!f) return;

    char buf[BUFFER_SIZE];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        int sent = 0;
        while (sent < (int)n) {
            int s = send(client_fd, buf + sent, n - sent, 0);
            if (s <= 0) goto done_cache;
            sent += s;
        }
    }
    done_cache:
    fclose(f);
}

/* ─────────────────────────────────────────────
   RESPUESTAS DE ERROR HTTP
   ───────────────────────────────────────────── */
void send_error_response(int fd, int code, const char *msg) {
    char body[256];
    snprintf(body, sizeof(body),
             "<html><body><h1>%d %s</h1></body></html>", code, msg);

    char response[512];
    snprintf(response, sizeof(response),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             code, msg, strlen(body), body);

    send(fd, response, strlen(response), 0);
}

/* ─────────────────────────────────────────────
   LOGGER
   ───────────────────────────────────────────── */
void log_message(LogLevel level, const char *fmt, ...) {
    const char *level_str[] = {"INFO", "WARN", "ERROR"};

    /* Timestamp */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);

    va_list args;
    char msg[1024];

    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    pthread_mutex_lock(&log_mutex);

    /* stdout */
    printf("[%s][%s] %s\n", ts, level_str[level], msg);
    fflush(stdout);

    /* archivo de log */
    if (log_file) {
        fprintf(log_file, "[%s][%s] %s\n", ts, level_str[level], msg);
        fflush(log_file);
    }

    pthread_mutex_unlock(&log_mutex);
}
/*
 * pibl.h — Definiciones y prototipos del Proxy Inverso + Balanceador de Carga
 */

#ifndef PIBL_H
#define PIBL_H

/* ── Includes estándar ── */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <sys/time.h>
#include <time.h>

/* ── Red ── */
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>

/* ── Hilos ── */
#include <pthread.h>

/* ────────────────────────────────
   CONSTANTES CONFIGURABLES
   ──────────────────────────────── */
#define DEFAULT_PORT    8080
#define DEFAULT_TTL     60        /* segundos */
#define MAX_BACKENDS    16
#define BACKLOG         128       /* cola de conexiones pendientes */
#define BUFFER_SIZE     8192      /* buffer de lectura/escritura */
#define MAX_REQUEST_SIZE 65536    /* tamaño máximo de una petición HTTP */
#define CACHE_DIR       "cache"   /* directorio de caché (relativo al ejecutable) */

/* ────────────────────────────────
   ESTRUCTURAS
   ──────────────────────────────── */

/* Un servidor backend */
typedef struct {
    char host[128];
    int  port;
} Backend;

/* Configuración global leída de pibl.conf */
typedef struct {
    int     port;
    char    log_file[256];
    int     cache_ttl;           /* TTL en segundos */
    Backend backends[MAX_BACKENDS];
    int     backend_count;
} ServerConfig;

/* Contexto pasado a cada hilo de cliente */
typedef struct {
    int                client_fd;
    struct sockaddr_in client_addr;
} ClientContext;

/* Representación de una petición HTTP parseada */
typedef struct {
    char   method[8];    /* GET, HEAD, POST */
    char   uri[512];
    char   version[16];  /* HTTP/1.1 */
    char   host[256];
    int    content_length;
    const char *body;    /* puntero dentro del raw buffer, NO copiado */
} HttpRequest;

/* Nivel de log */
typedef enum { LOG_INFO = 0, LOG_WARN, LOG_ERROR } LogLevel;

/* ────────────────────────────────
   VARIABLES GLOBALES (declaradas en pibl.c)
   ──────────────────────────────── */
extern ServerConfig      config;
extern pthread_mutex_t   log_mutex;
extern pthread_mutex_t   rr_mutex;
extern pthread_mutex_t   cache_mutex;
extern int               current_backend;
extern FILE             *log_file;

/* ────────────────────────────────
   PROTOTIPOS
   ──────────────────────────────── */

/* Configuración */
int      load_config(const char *path);

/* Socket servidor */
int      create_server_socket(int port);

/* Hilo por cliente */
void    *handle_client_thread(void *arg);

/* HTTP */
int      recv_http_request(int fd, char *buf, int buf_size);
int      parse_http_request(const char *raw, int len, HttpRequest *req);
void     send_error_response(int fd, int code, const char *msg);

/* Round Robin */
Backend *select_backend(void);

/* Backend */
int      connect_to_backend(Backend *backend);
void     relay_response(int backend_fd, int client_fd, const char *cache_path, const char *method); /* NULL = no cachear */

/* Caché */
void     build_cache_path(const char *uri, char *out, size_t out_size);
int      is_cache_valid(const char *cache_path);
void     write_cache_timestamp(const char *cache_path, const char *last_modified);
void     comparar_last_modified(const char *last_modified_original, const char *cache_path); // ← agregar
void     serve_from_cache(int client_fd, const char *cache_path);

/* Log */
void     log_message(LogLevel level, const char *fmt, ...);

#endif /* PIBL_H */
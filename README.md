# Proyecto 1 — PIBL + TWS

## Estructura de archivos

```
pibl/
├── pibl.c        # Proxy Inverso + Balanceador de Carga
├── pibl.h        # Headers y estructuras compartidas
├── tws.c         # Telematics Web Server
├── pibl.conf     # Configuración del PIBL
└── Makefile
```

## Compilar

```bash
make          # genera ./pibl y ./server
make clean    # limpia binarios
```

## Ejecutar (prueba local)

```bash
# Levantar 3 instancias del TWS en puertos distintos
./server 8081 tws1.log ./www &
./server 8082 tws2.log ./www &
./server 8083 tws3.log ./www &

# Levantar el PIBL
./pibl pibl.conf
```

Luego abre `http://localhost:8080/` en el browser.

## Configuración (pibl.conf)

| Clave       | Descripción                     | Default   |
|-------------|----------------------------------|-----------|
| `port`      | Puerto de escucha del PIBL       | 8080      |
| `log_file`  | Ruta del archivo de log          | pibl.log  |
| `cache_ttl` | TTL del caché en segundos        | 60        |
| `backend`   | `host:port` de un servidor TWS   | —         |

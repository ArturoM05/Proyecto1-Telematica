# Servidor Web Con Proxy Inverso y Balanceador de Carga.

- **Curso:** Internet: Arquitectura y Protocolos o Telemática
- **Integrantes:** Miguel Ángel García, Arturo Arias Murgeytio y Dorian Alejandro Guisao

- **Video:** https://www.youtube.com/watch?v=ohQa_l49UmM

--- 

# 1. Introducción

En el siguiente documento se describe el diseño e implementación de un Proxy Inverso (PI), un Balanceador de Carga (BL) y un Web Server (WS). Está compuesto por dos componentes principales que trabajan en conjunto: TWS (Telematics Web Server) y PIBL (Proxy Inverso + Balanceador de Carga).

TWS es un servidor web HTTP/1.1 capaz de interpretar peticiones HTTP concurrentemente y servir contenido estático desde un directorio configurable. Soporta los métodos GET, HEAD y POST; además, maneja los códigos de respuesta 200, 404, 400 y 304.

PIBL actúa como intermediario transparente entre los clientes y uno o más servidores TWS. Cuando un cliente realiza una petición HTTP, esta llega primero a PIBL, que se encarga de dos responsabilidades fundamentales: distribuir la carga entre los backends disponibles mediante el algoritmo Round Robin y servir respuestas desde una caché en disco cuando el recurso ya ha sido solicitado recientemente.

---



# 2. Desarrollo


## 2.1 Diseño

Para el diseño partimos del enunciado entregado por el profesor y nos apoyamos principalmente del contenido de la clase, el RFC 2616 y la documentación oficial.


### 2.1.1 Topologia de la red

El sistema está compuesto por tres capas lógicas. En la primera capa se encuentran los clientes HTTP, que pueden ser cualquier agente capaz de realizar peticiones GET, HEAD o POST: navegadores web, herramientas como curl o Postman. En la segunda capa se encuentra el PIBL, que actúa como único punto de entrada de la red: recibe todas las peticiones entrantes, consulta su caché en disco y, si no hay una respuesta válida almacenada, selecciona uno de los backends disponibles usando Round Robin y reenvía la petición. En la tercera capa se encuentran las tres instancias del TWS, cada una escuchando en su propio puerto, sirviendo archivos estáticos desde su directorio raíz.

![Topologia de la Red](https://github.com/Luisagudelo372/Akua/blob/f8ec3554aa06416bd47466529cfabc8bd862e252/media/places_photos/Topologia_De_Red_PIBL%20%2B%20TWS.jpeg)

### 2.1.2 Metodologia de desarrollo

Usamos una metodología en cascada, dividida en las fases de requisitos, diseño, implementación y pruebas. Esta metodología fue la más adecuada para el proyecto por dos razones concretas: primero, el enunciado del profesor proporciono desde el inicio un conjunto cerrado y estable de requisitos, sin cambios esperados durante el desarrollo; segundo, al ser un proyecto académico con entrega única, poco tiempo, no planes de escalabilidad y equipo pequeño, la sobrecarga de metodologías iterativas como Scrum no se justificaba.

### 2.1.3 Requisitos (Funcionales)

A partir del enunciado del proyecto se extrajeron los siguientes requisitos funcionales. Para aquellos directamente relacionados con el protocolo HTTP/1.1 se indica la sección del RFC 2616 que los define.

| ID        | Título                     | Descripción                                                                 | RFC 2616                  |
|-----------|-----------------------------|-----------------------------------------------------------------------------|---------------------------|
| INFRA-01  | Despliegue en EC2           | Crear 4 instancias EC2: una para PIBL y tres para TWS                      | —                         |
| TWS-01    | Método GET                  | Parsear y responder peticiones HTTP GET                                    | §9.3                      |
| TWS-02    | Método HEAD                 | Parsear y responder peticiones HTTP HEAD                                   | §9.4                      |
| TWS-03    | Método POST                 | Parsear y responder peticiones HTTP POST                                   | §9.5                      |
| TWS-04    | Códigos de respuesta        | Responder 200 (éxito), 400 (petición inválida) y 404 (recurso no encontrado) | §10.2.1, §10.4.1, §10.4.5 |
| TWS-05    | Concurrencia por hilos      | Crear un hilo por cada conexión entrante                                   | —                         |
| TWS-06    | Logger                      | Registrar en consola cada petición y respuesta enviada                     | —                         |
| TWS-07    | Arranque por CLI            | Iniciar con `./server <PORT> <LogFile> <DocumentRootFolder>`               | —                         |
| PIBL-01   | Servidor base con sockets   | Implementar PIBL con la API de sockets, escuchando en el puerto 80 o 8080  | —                         |
| PIBL-02   | Concurrencia por hilos      | Atender múltiples peticiones simultáneas con hilos bajo demanda            | —                         |
| PIBL-03   | Round Robin                 | Distribuir peticiones entre backends en orden circular                     | —                         |
| PIBL-04   | Reenvío al backend          | Abrir socket hacia el backend elegido, reenviar la petición y retornar la respuesta | §5, §6              |
| PIBL-05   | Sistema de log              | Registrar peticiones y respuestas en stdout y en archivo                   | —                         |
| PIBL-06   | Permitir caché              | El proxy debe cachear cada recurso solicitado                              | §13                       |
| PIBL-07   | Caché persistente en disco  | Almacenar la respuesta en disco y servirla directamente en peticiones posteriores | §13.2               |
| PIBL-08   | Directorio de caché         | Guardar los archivos de caché en el directorio de ejecución del PIBL       | —                         |
| PIBL-09   | TTL configurable            | Cada entrada de caché debe tener un tiempo de vida definido en la configuración | §13.2.4              |
| PIBL-10   | Archivo de configuración    | Leer un archivo `.conf` con el puerto y la lista de backends IP:Puerto     | —                         |


### 2.1.4 Diagramas de secuencia GET, HEAD y POST


<details>
<summary>Secuencia GET</summary>

[![](https://img.plantuml.biz/plantuml/svg/bP9DRjim58Jt0dI7GLOsYZX9brdaJoWQemagn0Iog8E28LzieXIfafITNA07w0swx2aAD3VfIVgeo6wGeejgmGNvZMRUu9t494Fg6biMyPDnhGooGQJF7Jb5bnIZND2fDGi7HIvHU8u5YgrtSOETaVNhqn3y6YbqL1Pb8LNo0PKrMLiMB4j6cLQwX9rwUdQn0nbHltYef5e-Vf-xaJPH-N7FL2yOSJthxqLs6VwFtIFKECJRDm9xWLGNeiUv4FNUmUGWStKcAYP-_g1x4rcXs8e-jZ8jiNhyPetsnnpQ9kGCXFEfm4W6jJGhZq7o024ki3ABR3ak2_1NONVmrZBv24qM6zcaztZYwfxRn3tSXiFh_NsyVpTt7swzVc1vr8H--fko81jfo7CvdSqmSjiiliz2zwq9KllnrkG8ubyzSArNldCQL_xMk44YDX9UXZIwCvPo9UEvkmiaTN_nXnmc_JlxabaA4Q-GfxXvbYPtlWw64qzIq-BNbw-yEqus13dqCtTFxyaqn24O8iTm_9_USbTEbyK9dxpGlm40)](https://editor.plantuml.com/uml/bP9DRjim58Jt0dI7GLOsYZX9brdaJoWQemagn0Iog8E28LzieXIfafITNA07w0swx2aAD3VfIVgeo6wGeejgmGNvZMRUu9t494Fg6biMyPDnhGooGQJF7Jb5bnIZND2fDGi7HIvHU8u5YgrtSOETaVNhqn3y6YbqL1Pb8LNo0PKrMLiMB4j6cLQwX9rwUdQn0nbHltYef5e-Vf-xaJPH-N7FL2yOSJthxqLs6VwFtIFKECJRDm9xWLGNeiUv4FNUmUGWStKcAYP-_g1x4rcXs8e-jZ8jiNhyPetsnnpQ9kGCXFEfm4W6jJGhZq7o024ki3ABR3ak2_1NONVmrZBv24qM6zcaztZYwfxRn3tSXiFh_NsyVpTt7swzVc1vr8H--fko81jfo7CvdSqmSjiiliz2zwq9KllnrkG8ubyzSArNldCQL_xMk44YDX9UXZIwCvPo9UEvkmiaTN_nXnmc_JlxabaA4Q-GfxXvbYPtlWw64qzIq-BNbw-yEqus13dqCtTFxyaqn24O8iTm_9_USbTEbyK9dxpGlm40)

</details>


<details>
<summary>Secuencia HEAD</summary>

[![](https://img.plantuml.biz/plantuml/svg/TPBFJjj04CRlblmE8qx9WV3dY2f4S2jH5LGBB770FKns9ycAzOpPMGVraNhWn2FanLYR60fgVLllxvlvlfazquWXTetFCxrtt6B01fGUEc93LwIAAvfxjs8mn972trWWROLrnCx9o-Cy17c463hAipn34oL0uLslpRCaYywu5Zd2Nldz_78FKA7yT56WMM__r3onJerC1wRun5ItDyF_gk-ogm_xfr322Lnyct-5Wq2c2ofmKLNbmT7ig0TP4YKR2k-SEchQG7MAaV8GRxPF2EXXWUQUs2ONB0aA6m9BQWdCu6xQbolxZjL_EoRjjNHiuLeMZfEY6laqypXPEayjnlNhnUXzfuNZmqFu-QFcIzIuVoNMBHtP4_Yo9BF6qvhlr34inFx-zUQaI7eZJR-R01zqDHjX80-h3eD5Aq0CiwXfGJv-H5E-Pk6spta6wIl6ke6QDBV9h4zFqvh7SzJfA_a-si76C2Hml47lKaFCipJCdTE-v6vZ_mXyf-85reIMWgPmPqauFCyN)](https://editor.plantuml.com/uml/TPBFJjj04CRlblmE8qx9WV3dY2f4S2jH5LGBB770FKns9ycAzOpPMGVraNhWn2FanLYR60fgVLllxvlvlfazquWXTetFCxrtt6B01fGUEc93LwIAAvfxjs8mn972trWWROLrnCx9o-Cy17c463hAipn34oL0uLslpRCaYywu5Zd2Nldz_78FKA7yT56WMM__r3onJerC1wRun5ItDyF_gk-ogm_xfr322Lnyct-5Wq2c2ofmKLNbmT7ig0TP4YKR2k-SEchQG7MAaV8GRxPF2EXXWUQUs2ONB0aA6m9BQWdCu6xQbolxZjL_EoRjjNHiuLeMZfEY6laqypXPEayjnlNhnUXzfuNZmqFu-QFcIzIuVoNMBHtP4_Yo9BF6qvhlr34inFx-zUQaI7eZJR-R01zqDHjX80-h3eD5Aq0CiwXfGJv-H5E-Pk6spta6wIl6ke6QDBV9h4zFqvh7SzJfA_a-si76C2Hml47lKaFCipJCdTE-v6vZ_mXyf-85reIMWgPmPqauFCyN)

</details>


<details>
<summary>Secuencia POST</summary>

[![](https://img.plantuml.biz/plantuml/svg/dP9DRW8n38Nt8yqTBFOKw995nS-cLMaPYKXTT5XuCXP416UQP40SggVenUe0erQegwuYEVwUttCo2H5zRFSspyBES8CUzn3eeoNMz48Xu8QcrcmOD74a_xlDKsWSXwvjHjOTfzwx8qJVKfxb6UheFCojIMoU2HQDDWroX5xnD5lq00CKDnVgRNMkgwHmPQ7_00MCeLYk50mywTO71uzA5OFHtQZakPCcZlq5yIPkn_1QSidlbQjFwwJ2JYJSWVoF23k92Hhrbh3a5KaQuiFN9q8mj6yy0LgeKE-8woHH90lglnPKHriI4wa8MvJLts9GdIA5S_yrSJS0xeT3M3wB96a7DTcBYlWmbQdT-knGc6vXVv7fmLgIiHV-4ax8YHpo1Ru1)](https://editor.plantuml.com/uml/dP9DRW8n38Nt8yqTBFOKw995nS-cLMaPYKXTT5XuCXP416UQP40SggVenUe0erQegwuYEVwUttCo2H5zRFSspyBES8CUzn3eeoNMz48Xu8QcrcmOD74a_xlDKsWSXwvjHjOTfzwx8qJVKfxb6UheFCojIMoU2HQDDWroX5xnD5lq00CKDnVgRNMkgwHmPQ7_00MCeLYk50mywTO71uzA5OFHtQZakPCcZlq5yIPkn_1QSidlbQjFwwJ2JYJSWVoF23k92Hhrbh3a5KaQuiFN9q8mj6yy0LgeKE-8woHH90lglnPKHriI4wa8MvJLts9GdIA5S_yrSJS0xeT3M3wB96a7DTcBYlWmbQdT-knGc6vXVv7fmLgIiHV-4ax8YHpo1Ru1)

</details>



### 2.1.5 Modelo de concurrencia y sockets

Ambos componentes, TWS y PIBL, utilizan el mismo modelo de concurrencia: un hilo por conexión bajo demanda. El hilo principal de cada servidor se mantiene bloqueado en accept(), esperando nuevas conexiones TCP. Cuando llega un cliente, se reserva un bloque de memoria con el contexto de esa conexión (ClientContext en PIBL, TwsClientCtx en TWS) y se lanza un hilo nuevo con pthread_create. El hilo recibe el file descriptor del socket del cliente y es completamente independiente del hilo principal.

En cuanto a los sockets, ambos servidores usan AF_INET (IPv4) con SOCK_STREAM (TCP). El socket servidor se configura con SO_REUSEADDR para evitar el error Address already in use al reiniciar el proceso. PIBL además configura SO_RCVTIMEO de 5 segundos en cada socket de cliente para evitar que un cliente que abre la conexión pero no envía datos bloquee el hilo indefinidamente.

**Nota:** Esto se ve en más detaller en la seccion `2.2 Implementación`

### 2.1.6 Flujo del PIBL

<details>
<summary>Ver flujo del PIBL</summary>

[![](https://img.plantuml.biz/plantuml/svg/ZLL1Jjj05DqZSOSlTMDB5AKae8e8bH1IYWG21TeLaZKUV-CHzemxCtO81-W1UeCkMNJL0rHgRjAJz8zjObDSgOcKsFxlpxpt_flaq5YcRP6bcnlcJiYSQPP1X34hKljUINib7X3U3BlLYTPgCLMfql1gD1ezAt4hIc6NHqf7g1lCtjvUBuRnkrkj2Xark7ZNlVkWnu9bIaOlE-BuRSJxEgwSgWOr70xx8BFrCXM08Ax7NUnFEEPq0rn9l1Uh7nAkfvVaIWey5IWjZWbqnkG3OHAHAWU4t2OQMHHm-gmHKoLZSLjeGbcHeIeiN5q4y-cdwzFpsGLukyPtC8sy319hyq3ZvmADzVmRUOQe8KShUBK_LrcUecLZnq_4uFtwURakbgjlgOZOEnziWX8ygVpD3Q3Nla4P1KZpqhIsoPKqw8q60rh_Y4KmhxSRrs1ZLRwvGGqYhamWjmo-u3MxjveHjMprQ5Majk3Z_7GBIjJ6KN5omaAav0BZ2GOviubNQE5tU9_NdN1m02UpwpLfi_hUiAvXmZHhb8p4UUrwVazB8rQN6CHQPNMdvu_RQgtENJKAMw46K-Gs22bTzD29dBkCmLo5GbQZpGgBzquRtKY7jQg5TwHDrtcXBwlC_uvaTx3JZEI4ehDWot-Cn5GRmkylNq4ZobAmRaPO2WsfVhS_p2R7FTvL-ODSKq8fyKaXxwgajqjLeAUvdp5ZNvyhOYHmNNJffBEbWvIgGTPKdPEdiXgVEqiqajKZ645NMKY4nnr3_XQLq46YI3b9xOdhSQGlKR0a5BE5WPt1e4_l9DH20zCy4QM2RPlbuDW36gw5fUCwxdgkCMNBTbGk7SX5I2jqNBgHQ-ejvHjvMp3jj1C1jqrdB3DJ6O8iJB5_9f9bw5Nad3FR5VefqFiB8ehD-N0_BIGuonstasR7zKDouzd8QYxccSGfnP5wZ28jrZpHfyus0xNlXtH3VnL_0000)](https://editor.plantuml.com/uml/ZLL1Jjj05DqZSOSlTMDB5AKae8e8bH1IYWG21TeLaZKUV-CHzemxCtO81-W1UeCkMNJL0rHgRjAJz8zjObDSgOcKsFxlpxpt_flaq5YcRP6bcnlcJiYSQPP1X34hKljUINib7X3U3BlLYTPgCLMfql1gD1ezAt4hIc6NHqf7g1lCtjvUBuRnkrkj2Xark7ZNlVkWnu9bIaOlE-BuRSJxEgwSgWOr70xx8BFrCXM08Ax7NUnFEEPq0rn9l1Uh7nAkfvVaIWey5IWjZWbqnkG3OHAHAWU4t2OQMHHm-gmHKoLZSLjeGbcHeIeiN5q4y-cdwzFpsGLukyPtC8sy319hyq3ZvmADzVmRUOQe8KShUBK_LrcUecLZnq_4uFtwURakbgjlgOZOEnziWX8ygVpD3Q3Nla4P1KZpqhIsoPKqw8q60rh_Y4KmhxSRrs1ZLRwvGGqYhamWjmo-u3MxjveHjMprQ5Majk3Z_7GBIjJ6KN5omaAav0BZ2GOviubNQE5tU9_NdN1m02UpwpLfi_hUiAvXmZHhb8p4UUrwVazB8rQN6CHQPNMdvu_RQgtENJKAMw46K-Gs22bTzD29dBkCmLo5GbQZpGgBzquRtKY7jQg5TwHDrtcXBwlC_uvaTx3JZEI4ehDWot-Cn5GRmkylNq4ZobAmRaPO2WsfVhS_p2R7FTvL-ODSKq8fyKaXxwgajqjLeAUvdp5ZNvyhOYHmNNJffBEbWvIgGTPKdPEdiXgVEqiqajKZ645NMKY4nnr3_XQLq46YI3b9xOdhSQGlKR0a5BE5WPt1e4_l9DH20zCy4QM2RPlbuDW36gw5fUCwxdgkCMNBTbGk7SX5I2jqNBgHQ-ejvHjvMp3jj1C1jqrdB3DJ6O8iJB5_9f9bw5Nad3FR5VefqFiB8ehD-N0_BIGuonstasR7zKDouzd8QYxccSGfnP5wZ28jrZpHfyus0xNlXtH3VnL_0000)

</details>

### 2.1.7 Estructura de archivos 


```text
Proyecto1-Telematica/
│
├── pibl.c              # Proxy inverso y balanceador de carga
├── pibl.h              # Definiciones, constantes y prototipos del PIBL
├── pibl.conf           # Archivo de configuración (puerto, backends, TTL, log)
│
├── tws.c               # Servidor web HTTP/1.1
│
└── www/                # Directorio raíz de contenido estático
    ├── caso1/
    │   ├── img/            # Una imagen
    │   └── index.html
    ├── caso2/
    │   ├── img/            # Cinvo imágenes
    │   └── index.html
    ├── caso3/
    │   ├── index.html
    │   └── archivo.bin     # Archivo binario de 1 MB
    └── caso4/
        ├── css/
        ├── img/
        ├── js/
        └── index.html
```



## 2.2 Implementación

### 2.2.1 Telematics Web Server (TWS)



#### Estructura del servidor y manejo de conexiones

La función `main()` del TWS sigue el patrón (visto en clase):

```text
socket() → setsockopt() → bind() → listen() → bucle de accept()
```

Cada conexión aceptada genera un hilo independiente mediante `pthread_create`, al que se le pasa un bloque de memoria `TwsClientCtx` con el file descriptor del cliente, su dirección IP, el directorio raíz y la ruta del log.

El hilo toma posesión de ese bloque, lo libera con `free()` al inicio de su ejecución, y termina con `pthread_detach` para que el sistema operativo recoja sus recursos automáticamente sin necesidad de un `pthread_join` desde el hilo principal.

Se tomaron dos decisiones de configuración de socket que vale la pena destacar:

1. **SO_REUSEADDR**: sin esta opción, detener y reiniciar el servidor rápidamente falla con `"Address already in use"` porque el kernel mantiene el puerto en estado `TIME_WAIT` hasta por 2 minutos.

2. **SO_RCVTIMEO**: se configuró un timeout de recepción de 5 segundos, que protege contra clientes que abren la conexión TCP pero nunca envían datos, evitando que el hilo quede bloqueado indefinidamente.

---

#### Lectura de la petición HTTP

El problema central al leer una petición HTTP sobre TCP es que el protocolo no define límites de mensaje: `recv()` puede devolver la petición completa, solo los primeros bytes, o cualquier fragmento intermedio.

La función `recv_full_request()` resuelve esto con un bucle que acumula datos en un buffer y busca el marcador:

```text
\r\n\r\n
```

que indica el fin de los headers.

Una vez encontrado, extrae el valor de `Content-Length` con `strcasestr()` para saber si hay un body y cuántos bytes faltan por leer, continuando el loop hasta completarlo.

Esta lógica es idéntica en TWS y en PIBL, ya que ambos necesitan leer peticiones completas antes de procesarlas.

```c
char *end = strstr(buf, "\r\n\r\n");
if (end) {
    header_end = (int)(end - buf) + 4;

    int content_length = 0;
    char *cl = strcasestr(buf, "Content-Length:");
    if (cl) content_length = atoi(cl + 15);

    /* Si no hay body o ya lo recibimos completo, salir */
    int body_received = total - header_end;
    if (content_length <= 0 || body_received >= content_length)
        break;

    /* Seguir leyendo el body hasta completar content_length */
    int body_remaining = content_length - body_received;
    while (body_remaining > 0 && total < buf_size - 1) {
        int to_read = (body_remaining < buf_size-1-total)
                      ? body_remaining : buf_size-1-total;
        int r = recv(fd, buf + total, to_read, 0);
        if (r <= 0) break;
        total += r;
        body_remaining -= r;
    }
    break;
}
```

---

#### Resolución de rutas y tipos MIME

La función `resolve_path()` construye la ruta absoluta en disco concatenando el `doc_root` con la URI limpia.

Antes de hacer esto, elimina el query string (todo lo que viene después de `?`) para evitar que parámetros como:

```text
/search?q=foo
```

intenten abrir un archivo llamado literalmente `search?q=foo`.

Adicionalmente, cualquier URI que termine en `/` se resuelve a `index.html` dentro de ese directorio, de forma que tanto:

```text

/caso1/
/caso4/
```

apunten correctamente al `index.html` correspondiente.

Sin esta lógica, `stat()` encontraría el directorio pero `S_ISREG()` fallaría y el servidor respondería con un `404` incorrecto.

La detección del tipo MIME se hace en `get_mime_type()` comparando la extensión del archivo con una tabla de casos usando `strcasecmp()`, cubriendo los tipos más comunes:

- HTML
- CSS
- JS
- JSON
- PNG
- JPEG
- GIF
- WebP
- SVG
- ICO
- MP4
- WebM
- MP3
- WOFF
- WOFF2
- PDF
- XML

Para extensiones no reconocidas se devuelve:

```text
application/octet-stream
```

lo que le indica al navegador que descargue el archivo en lugar de intentar renderizarlo.

---

#### Caché condicional: 304 Not Modified

El TWS implementa caché condicional mediante el header:

```text
If-Modified-Since
```

Cuando un cliente envía este header, `parse_request()` lo extrae con `strcasestr()` y lo almacena en el campo `if_modified_since` de la estructura `HttpRequest`.

Luego, en `send_file_response()`, se obtiene la fecha de última modificación del archivo con `stat()` y se compara contra el valor del cliente usando `parse_http_date()`, que convierte la fecha HTTP en formato RFC:

```text
Mon, 01 Jan 2024 00:00:00 GMT
```

a un `time_t` mediante `strptime()` con zona GMT usando `timegm()`.

Si el archivo no ha sido modificado desde la fecha que indica el cliente, el servidor responde con:

```text
304 Not Modified
```

sin body, ahorrando el ancho de banda de la transferencia.

```c
if (req->if_modified_since[0] != '\0') {
    time_t client_time = parse_http_date(req->if_modified_since);

    if (client_time != (time_t)-1 && st.st_mtime <= client_time) {
        char resp[256];
        int len = snprintf(resp, sizeof(resp),
            "HTTP/1.1 304 Not Modified\r\n"
            "Last-Modified: %s\r\n"
            "Connection: close\r\n"
            "\r\n", timebuf);
        send(fd, resp, len, 0);
        return;
    }
}
```

Para el método `HEAD`, `send_file_response()` recibe el parámetro:

```c
send_body = 0
```

y retorna después de enviar los headers, sin abrir ni leer el archivo.

Esto garantiza que `HEAD` devuelve exactamente los mismos headers que un `GET` equivalente, cumpliendo con la especificación del RFC 2616 §9.4.

---

#### Dificultades y limitaciones

La principal dificultad durante el desarrollo del TWS fue el manejo correcto de URIs de directorio.

En una primera versión, solo se mapeaba `/` a `index.html`, pero los casos de prueba usaban URIs como:

```text
/caso1/
```

que llegaban como rutas de directorio.

`stat()` las encontraba correctamente, pero `S_ISREG()` devolvía falso porque era un directorio, resultando en un `404` incorrecto.

La solución fue generalizar la lógica: cualquier URI que termine en `/` recibe automáticamente el sufijo `index.html`.

En cuanto a limitaciones, el TWS no implementa un pool de hilos: cada conexión lanza un hilo nuevo sin límite superior. Bajo una carga muy alta esto puede agotar los recursos del sistema, cosa que en este caso no es problema, pero en un producto pensado en la escalibilidad si lo seria. 







## 2.2.2 Proxy Inverso + Balanceador de Carga (PIBL)

#### Configuración y arranque

La configuración del PIBL se lee íntegramente desde `pibl.conf` en la función `load_config()`, que parsea pares `clave=valor` línea por línea con `fgets()` y `sscanf()`.

Esto significa que ningún parámetro operacional está hardcodeado en el binario: el puerto de escucha, la ruta del log, el TTL de la caché y la lista completa de backends se pueden modificar editando el archivo de configuración y reiniciando el proceso, sin recompilar.

Si el archivo no existe, la función no falla sino que aplica valores por defecto razonables:

- Puerto `8080`
- TTL de `30` segundos
- Un backend en `127.0.0.1:8081`

Esto facilita el desarrollo local.

Los backends se almacenan en un arreglo estático de structs `Backend` con capacidad `MAX_BACKENDS`.

Para cada entrada en el `.conf` con formato:

```text
backend=host:puerto
```

`sscanf()` extrae el host y el puerto en una sola operación.

Se usa `snprintf()` en lugar de `strncpy()` para copiar el host porque garantiza *null-termination* sin generar warnings de truncación del compilador.

---

#### Balanceo Round Robin

El algoritmo de Round Robin se implementa en `select_backend()` sobre un índice entero global `current_backend`.

La función adquiere `rr_mutex`, lee el índice actual, lo incrementa módulo `backend_count`, y libera el mutex.

El punto de diseño más importante aquí es que la sección crítica es deliberadamente mínima: protege solo las dos líneas que leen y modifican el índice compartido, no la conexión al backend ni el procesamiento de la petición.

Si el mutex cubriera la conexión completa, todos los hilos quedarían serializados esperando turno, destruyendo el paralelismo del sistema.

```c
Backend *select_backend(void) {
    pthread_mutex_lock(&rr_mutex);

    int idx = current_backend;
    current_backend = (current_backend + 1) % config.backend_count;

    pthread_mutex_unlock(&rr_mutex);

    return &config.backends[idx];
}
```

Con tres backends configurados (`8081`, `8082`, `8083`), la distribución en condiciones de carga uniforme es aproximadamente `1/3` de las peticiones por backend.

En el entorno de AWS del proyecto, cada backend corre en una instancia EC2 independiente, por lo que el Round Robin distribuye la carga efectivamente entre tres máquinas físicamente separadas.

---

#### Escritura atómica de caché

El sistema de caché fue el componente más delicado de implementar debido a las condiciones de carrera.

El escenario problemático es el siguiente:

1. Dos hilos reciben simultáneamente una petición por el mismo recurso que no está en caché.
2. Ambos hacen `is_cache_valid()` y obtienen falso.
3. Ambos van al backend y reciben la respuesta.
4. Ambos intentan escribir al mismo archivo de caché.

Sin protección, el resultado sería un archivo corrupto con contenido mezclado de dos respuestas.

La solución implementada es el patrón:

```text
write-to-temp + rename atómico
```

Cada hilo escribe la respuesta en un archivo `.tmp` único (`<cache_path>.tmp`), y solo cuando la recepción está completa ejecuta `rename()` sobre ese archivo temporal.

En Linux, `rename()` es una operación atómica garantizada por POSIX: desde la perspectiva del sistema de archivos, el archivo nuevo reemplaza al anterior en un solo paso indivisible.

Si dos hilos hacen `rename()` sobre el mismo destino, uno gana y el otro sobreescribe con una respuesta igualmente válida, sin corrupción.

```c
/* Abrir temporal para escritura */
snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", cache_path);
cache_fp = fopen(tmp_path, "wb");

/* Relay: recv del backend → send al cliente → write al .tmp */
while ((n = recv(backend_fd, buf, sizeof(buf), 0)) > 0) {
    send(client_fd, buf, n, 0);

    if (cache_fp)
        fwrite(buf, 1, n, cache_fp);
}

/* Rename atómico cuando la respuesta está completa */
if (cache_fp) {
    fclose(cache_fp);

    if (rename(tmp_path, cache_path) == 0) {
        write_cache_timestamp(cache_path, last_modified);
    } else {
        remove(tmp_path); /* limpiar si rename falla */
    }
}
```

Cada entrada de caché se compone de dos archivos:

1. El archivo `.cache` con el contenido completo de la respuesta HTTP (headers y body).
2. Un archivo `.ts` que contiene:
   - El timestamp Unix de cuando se guardó.
   - El valor de `Last-Modified` del recurso.

La validación de TTL en `is_cache_valid()`:

1. Lee el timestamp del `.ts`.
2. Calcula:

```c
difftime(time(NULL), cached_time)
```

3. Compara el resultado contra `config.cache_ttl`.

Si el TTL expiró, elimina ambos archivos y retorna `0` para forzar una consulta fresca al backend.

---

#### Invalidación por Last-Modified

Además del TTL, el PIBL implementa un mecanismo de invalidación activa basado en el header:

```text
Last-Modified
```

Cuando llega una petición `HEAD`, `relay_response()` detecta el método y extrae el valor de `Last-Modified` del primer chunk de respuesta del backend usando `strcasestr()`.

Luego llama a `comparar_last_modified()`, que lee el `Last-Modified` guardado en el archivo `.ts` y los compara con `strcmp()`.

Si son distintos, el recurso fue modificado en el servidor desde la última vez que se cacheó, y la función elimina tanto el `.cache` como el `.ts`, forzando que la próxima petición `GET` obtenga una copia fresca.

```c
void comparar_last_modified(const char *last_modified_original,
                            const char *cache_path) {

    /* Leer el Last-Modified guardado en el .ts */
    char last_modified_ts[128];

    fscanf(f, "%ld %127[^\r\n]",
           &cached_time,
           last_modified_ts);

    if (strcmp(last_modified_original,
               last_modified_ts) != 0) {

        remove(cache_path);
        remove(ts_path);

        log_message(LOG_INFO,
            "[CACHE INVALIDATED] %s "
            "(Last-Modified cambió)",
            cache_path);
    }
}
```


---

#### Construcción de rutas de caché

La función `build_cache_path()` transforma la URI en un nombre de archivo válido para el sistema de archivos reemplazando cada `/` por `_`.

Así:

```text
/caso1/index.html
```

se convierte en:

```text
cache/_caso1_index.html.cache
```

Se incluye protección contra *path traversal* en los nombres de caché:

- Si se detectan dos puntos consecutivos (`..`), el segundo se reemplaza por `_`.

La URI raíz `/` se mapea al nombre especial:

```text
root
```

para evitar un archivo llamado únicamente `_`.

Una limitación conocida de este esquema es que dos URIs diferentes pueden producir el mismo nombre de archivo de caché.

Por ejemplo:

```text
/a/b
/a_b
```

se mapearían ambas a:

```text
cache/_a_b.cache
```



---

#### Dificultades 

La dificultad más significativa del PIBL fue diseñar correctamente el relay de respuesta en `relay_response()`.

El primer chunk recibido del backend tiene un rol especial: contiene:

- La línea de status (`HTTP/1.1 200 OK`)
- Los headers

A partir de ellos se extrae:

- El código de respuesta para decidir si la respuesta es cacheable.
- El valor de `Last-Modified` para la invalidación.

El resto de la respuesta se recibe en un loop genérico.

Separar el primer `recv()` del loop general permitió inspeccionar los headers sin interrumpir el flujo de relay hacia el cliente.


---




## 3. Conclusiones



---




## 4. Referencias



---



## Apendice A: Pruebas y Evidencias

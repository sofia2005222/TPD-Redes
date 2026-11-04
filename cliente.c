#include "http_ip.h"
#include "proto.h"

#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>

/* Credenciales del grupo, provistas por la catedra */
#define USERNAME "grupo09"
#define SECRET   "Dl9at4WI"

/* Servidor de la catedra (IP y puerto UDP) */
#define SERVER_IP   "167.114.129.206"
#define SERVER_PORT "20262"

/* Tiempo maximo total de espera de la respuesta */
#define TIMEOUT_MS 10000

static long now_ms(void)
/* Devuelve el tiempo actual en milisegundos, medido con un reloj que solo avanza.
Sirve para calcular cuanto tiempo paso.
*/
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int main(void)
{
    char public_ip[16];
    char request[PROTO_MAX_MSG];

    /* Paso 2: obtener la IP publica */
    //es la IP con la que salimos a internet: la que ve el servidor y la que tiene que ir firmada
    if (get_public_ip(public_ip, sizeof(public_ip)) != 0) {
        fprintf(stderr, "Error: no se pudo obtener la IP publica\n");
        return 1;
    }
    printf("IP publica: %s\n", public_ip);
    
    /* Paso 3: armar el pedido */
    //"<usuario>|<ip>|<firma>". El secreto se usa para firmar pero no queda en request.
    if (proto_build_request(USERNAME, public_ip, SECRET, request, sizeof(request)) != 0) {
        fprintf(stderr, "Error: no se pudo armar el pedido\n");
        return 1;
    }
    printf("Pedido: %s\n", request);

    /* Paso 4: crear el socket UDP y mandar el pedido */
    struct addrinfo criterions = {0};
    struct addrinfo *directions = NULL;
    struct sockaddr_in server_addr;

    //queremos una direccion para UDP
    criterions.ai_family = AF_INET;
    criterions.ai_socktype = SOCK_DGRAM; // UDP
    if (getaddrinfo(SERVER_IP, SERVER_PORT, &criterions, &directions) != 0) {
        fprintf(stderr, "Error: direccion del servidor invalida\n");
        return 1;
    }
    //guardo una copia de la direccion del servidor: la uso para enviar y para filtrar respuestas
    memcpy(&server_addr, directions->ai_addr, sizeof(server_addr));
    freeaddrinfo(directions);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        fprintf(stderr, "Error: no se pudo crear el socket\n");
        return 1;
    }

    //como en UDP no hay connect, la direccion de destino va en cada envio
    size_t request_len = strlen(request);
    ssize_t sent = sendto(sock, request, request_len, 0,
                          (struct sockaddr *)&server_addr, sizeof(server_addr));
    //Un datagrama sale entero o no sale: no hace falta un ciclo como con send en TCP.
    if (sent == -1 || (size_t)sent != request_len) {
        fprintf(stderr, "Error: no se pudo enviar el pedido\n");
        close(sock);
        return 1;
    }
    printf("Pedido enviado a %s:%s\n", SERVER_IP, SERVER_PORT);

    /* Paso 5: esperar la respuesta, como mucho 10 segundos en total */
    //el plazo es fijo desde ahora: los datagramas descartados no lo reinician.
    long deadline = now_ms() + TIMEOUT_MS;
    proto_result result = PROTO_DISCARD;
    int tcp_port = 0;
    char error_text[128];

    while (1) {
        //cuanto queda del plazo total
        long remaining = deadline - now_ms();
        if (remaining <= 0) {
            break;
        }

        //poll espera hasta que llegue algo al socket o hasta que pase remaining.
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLIN;
        int ready = poll(&pfd, 1, (int)remaining);
        if (ready == -1) {
            fprintf(stderr, "Error: fallo poll\n");
            close(sock);
            return 1;
        }
        if (ready == 0) {
            break;
        }

        //llego un datagrama: recvfrom ademas nos dice desde que direccion vino
        char buf[PROTO_MAX_MSG];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (n == -1) {
            fprintf(stderr, "Error: fallo recvfrom\n");
            close(sock);
            return 1;
        }

        //filtro por IP de origen: en UDP cualquiera puede mandarnos datagramas
        if (from.sin_addr.s_addr != server_addr.sin_addr.s_addr) {
            continue;
        }

        /* Paso 6: interpretar la respuesta */
        result = proto_parse_response(buf, (size_t)n, SECRET, &tcp_port, error_text, sizeof(error_text));
        if (result == PROTO_OK || result == PROTO_REJECTED) {
            break;
        }
        //PROTO_DISCARD: se ignora en silencio y se sigue esperando lo que queda del plazo.
    }

    close(sock);

    /* Paso 7: mostrar el resultado */
    if (result == PROTO_OK) {
        printf("Autenticacion exitosa. Puerto TCP: %d\n", tcp_port);
        return 0;
    }
    if (result == PROTO_REJECTED) {
        fprintf(stderr, "El servidor rechazo el pedido: %s\n", error_text);
        return 1;
    }
    fprintf(stderr, "Error: no llego una respuesta valida en %d segundos\n", TIMEOUT_MS / 1000);
    return 1;
}
#define _POSIX_C_SOURCE 200809L   /* getaddrinfo, clock_gettime con -std=c11 */

#include "tcp_client.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define RECV_BUF 1024   /* bytes que se piden en cada recv */

static long now_ms(void)
/* Tiempo actual en milisegundos con un reloj que solo avanza (no lo afecta cambiar la hora). */
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int connect_with_timeout(int s, const struct sockaddr *addr, socklen_t addrlen,
                                long deadline)
/* Hace connect sobre el socket s sin esperar mas alla de deadline (en ms de now_ms).
connect comun es bloqueante y puede tardar mucho (minutos) si nadie responde, asi que:
 1. Pone el socket en modo no bloqueante: connect vuelve enseguida con EINPROGRESS
    mientras el handshake de TCP (SYN, SYN-ACK, ACK) sigue en curso.
 2. Espera con poll a que el socket quede listo para escribir (POLLOUT), que es cuando
    el handshake termino, bien o mal.
 3. Lee el resultado real del connect con getsockopt(SO_ERROR): 0 = conectado.
 4. Vuelve a poner el socket en modo bloqueante para usarlo normalmente despues.
Devuelve 0 si conecto, -1 si fallo (errno queda con el motivo).
*/
{
    int flags = fcntl(s, F_GETFL, 0);
    if (flags == -1 || fcntl(s, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }

    if (connect(s, addr, addrlen) == -1) {
        if (errno != EINPROGRESS) {
            return -1;   /* fallo inmediato, por ejemplo "connection refused" en localhost */
        }

        struct pollfd pfd = { .fd = s, .events = POLLOUT };
        int ready;
        do {
            long remaining = deadline - now_ms();
            if (remaining <= 0) {
                errno = ETIMEDOUT;
                return -1;
            }
            ready = poll(&pfd, 1, (int)remaining);
        } while (ready == -1 && errno == EINTR);   /* una senal interrumpio: reintentar */

        if (ready == -1) {
            return -1;
        }
        if (ready == 0) {
            errno = ETIMEDOUT;
            return -1;
        }

        int so_error = 0;
        socklen_t so_len = sizeof(so_error);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, &so_error, &so_len) == -1) {
            return -1;
        }
        if (so_error != 0) {
            errno = so_error;
            return -1;
        }
    }

    return fcntl(s, F_SETFL, flags);   /* restaura el modo bloqueante */
}

int tcp_connect_host(const char *host, int port, int timeout_ms)
/* Procedimiento:
 1. Valida argumentos y pasa el puerto a texto (getaddrinfo lo pide asi).
 2. getaddrinfo devuelve una LISTA de direcciones para host (puede haber varias, IPv4/IPv6).
 3. Prueba cada una: socket + connect con timeout. Se queda con la primera que conecta.
    El plazo es total: si una direccion gasta tiempo, a las siguientes les queda menos.
 4. Libera la lista y devuelve el descriptor conectado, o -1.
*/
{
    if (host == NULL || port <= 0 || port > 65535 || timeout_ms <= 0) {
        fprintf(stderr, "tcp_connect_host: argumentos invalidos\n");
        return -1;
    }

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo criterions = {0};
    struct addrinfo *directions = NULL;
    criterions.ai_family = AF_UNSPEC;       /* IPv4 o IPv6, lo que haya */
    criterions.ai_socktype = SOCK_STREAM;   /* TCP */

    int gai = getaddrinfo(host, port_str, &criterions, &directions);
    if (gai != 0) {
        fprintf(stderr, "tcp_connect_host: getaddrinfo(%s): %s\n", host, gai_strerror(gai));
        return -1;
    }

    long deadline = now_ms() + timeout_ms;
    int s = -1;
    int last_errno = 0;

    for (struct addrinfo *d = directions; d != NULL; d = d->ai_next) {
        s = socket(d->ai_family, d->ai_socktype, d->ai_protocol);
        if (s == -1) {
            last_errno = errno;
            continue;
        }
        if (connect_with_timeout(s, d->ai_addr, d->ai_addrlen, deadline) == 0) {
            break;   /* conectado */
        }
        last_errno = errno;
        close(s);
        s = -1;
    }
    freeaddrinfo(directions);

    if (s == -1) {
        fprintf(stderr, "tcp_connect_host: no se pudo conectar a %s:%d: %s\n",
                host, port, strerror(last_errno));
    }
    return s;
}

int tcp_receive_pdus(int fd, framer *f, pdu_handler on_pdu, void *arg, int idle_timeout_ms)
/* Bucle de recepcion:
 1. poll espera hasta que haya datos, como mucho idle_timeout_ms. Si no llega nada en ese
    tiempo, se abandona (el servidor quedo colgado): devuelve -1.
 2. recv lee lo que haya (un bloque de cualquier tamano: TCP no respeta limites de PDU).
    - n > 0 : se pasa el bloque al framer, que entrega las PDUs completas a on_pdu.
    - n == 0: el servidor cerro la conexion (EOF): termino bien, devuelve 0.
    - n < 0 : error. Si fue EINTR (una senal interrumpio la llamada) se reintenta.
*/
{
    if (fd < 0 || f == NULL || idle_timeout_ms <= 0) {
        fprintf(stderr, "tcp_receive_pdus: argumentos invalidos\n");
        return -1;
    }

    char buf[RECV_BUF];

    while (1) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int ready = poll(&pfd, 1, idle_timeout_ms);
        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("tcp_receive_pdus: poll");
            return -1;
        }
        if (ready == 0) {
            fprintf(stderr, "tcp_receive_pdus: %d ms sin recibir datos\n", idle_timeout_ms);
            return -1;
        }

        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            framer_feed(f, buf, (size_t)n, on_pdu, arg);
        } else if (n == 0) {
            return 0;   /* EOF: el servidor cerro al terminar de mandar */
        } else if (errno != EINTR) {
            perror("tcp_receive_pdus: recv");
            return -1;
        }
    }
}

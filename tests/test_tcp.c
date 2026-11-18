#define _POSIX_C_SOURCE 200809L   /* fork, nanosleep, getsockname con -std=c11 */

/* Prueba de tcp_client contra un servidor TCP falso local (127.0.0.1).
 * El servidor corre en un proceso hijo (fork) y manda PDUs partidas y juntas de forma
 * arbitraria, para ver que conexion + recepcion + framing funcionan juntos de verdad. */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "framing.h"
#include "tcp_client.h"

static int failures = 0;

static void check(int ok, const char *description)
{
    if (ok) {
        printf("OK    %s\n", description);
    } else {
        printf("FALLA %s\n", description);
        failures++;
    }
}

#define MAX_PDUS 10
typedef struct {
    int  count;
    char pdu[MAX_PDUS][FRAME_MAX];
} record;

static void on_pdu(const char *pdu, size_t len, int truncated, void *arg)
{
    (void)truncated;
    record *r = arg;
    if (r->count < MAX_PDUS) {
        memcpy(r->pdu[r->count], pdu, len + 1);
    }
    r->count++;
}

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static int listen_local(int *port)
/* Abre un socket escuchando en 127.0.0.1 con un puerto libre elegido por el sistema. */
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    socklen_t len = sizeof(addr);

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;   /* 0 = que el sistema elija */
    if (s == -1 || bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1 || listen(s, 1) == -1
        || getsockname(s, (struct sockaddr *)&addr, &len) == -1) {
        perror("listen_local");
        exit(1);
    }
    *port = ntohs(addr.sin_port);
    return s;
}

static pid_t fake_server(int ls, const char *chunks[], int n, long silence_ms)
/* Hijo: acepta una conexion, manda cada chunk por separado con una pausa entre medio
(para que lleguen en segmentos TCP distintos), espera silence_ms y cierra. */
{
    pid_t pid = fork();
    if (pid == 0) {
        int c = accept(ls, NULL, NULL);
        for (int i = 0; i < n; i++) {
            send(c, chunks[i], strlen(chunks[i]), 0);
            sleep_ms(50);
        }
        sleep_ms(silence_ms);
        close(c);
        close(ls);
        _exit(0);
    }
    return pid;
}

static void test_arbitrary_split(void)
{
    int port;
    int ls = listen_local(&port);
    /* 3 PDUs repartidas: media PDU, 1,5 PDUs, CR solo, LF + resto, y una a medias al final */
    const char *chunks[] = { "1792303544&Hola", " mundo\r\n17923", "03545&Chau\r",
                             "\n1792303546&Ultima\r\n17", "92303547&a medias" };
    record r = {0};
    framer f;

    printf("\n--- Servidor falso que parte y junta PDUs ---\n");
    pid_t pid = fake_server(ls, chunks, 5, 0);
    close(ls);

    int fd = tcp_connect_host("127.0.0.1", port, 2000);
    check(fd >= 0, "conecta a 127.0.0.1");
    framer_init(&f);
    int rc = tcp_receive_pdus(fd, &f, on_pdu, &r, 2000);
    close(fd);
    waitpid(pid, NULL, 0);

    check(rc == 0, "devuelve 0 cuando el servidor cierra");
    check(r.count == 3, "3 PDUs completas");
    check(r.count == 3 && strcmp(r.pdu[0], "1792303544&Hola mundo") == 0
          && strcmp(r.pdu[1], "1792303545&Chau") == 0
          && strcmp(r.pdu[2], "1792303546&Ultima") == 0, "con el contenido correcto, en orden");
    check(framer_pending(&f) == strlen("1792303547&a medias"),
          "la PDU a medias queda pendiente y no se entrega");
}

static void test_idle_timeout(void)
{
    int port;
    int ls = listen_local(&port);
    const char *chunks[] = { "1&uno\r\n" };
    record r = {0};
    framer f;

    printf("\n--- Servidor que deja de mandar sin cerrar ---\n");
    pid_t pid = fake_server(ls, chunks, 1, 1000);
    close(ls);

    int fd = tcp_connect_host("localhost", port, 2000);
    framer_init(&f);
    int rc = tcp_receive_pdus(fd, &f, on_pdu, &r, 300);
    close(fd);
    waitpid(pid, NULL, 0);

    check(rc == -1, "devuelve -1 por timeout de inactividad");
    check(r.count == 1, "lo recibido antes del timeout se entrego igual");
}

static void test_connect_errors(void)
{
    int port;
    int ls = listen_local(&port);
    close(ls);   /* el puerto queda cerrado: nadie escucha */

    printf("\n--- Errores de conexion (los mensajes de stderr son esperados) ---\n");
    check(tcp_connect_host("127.0.0.1", port, 1000) == -1, "puerto cerrado -> -1");
    check(tcp_connect_host("host.invalido", 80, 1000) == -1, "host inexistente -> -1");
    check(tcp_connect_host("127.0.0.1", 0, 1000) == -1, "puerto 0 -> -1");
    check(tcp_connect_host(NULL, 80, 1000) == -1, "host NULL -> -1");
}

int main(void)
{
    test_arbitrary_split();
    test_idle_timeout();
    test_connect_errors();

    printf("\n%s: %d falla(s)\n", failures == 0 ? "TODO OK" : "HAY FALLAS", failures);
    return failures == 0 ? 0 : 1;
}

#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#include "framing.h"

/* Conecta por TCP a host:port. El puerto queda abierto solo 10 s: llamar apenas
 * llega la respuesta 200. Devuelve el descriptor o -1 (motivo en stderr).
 * timeout_ms es el plazo TOTAL, sumando todas las direcciones que se prueban. */
int tcp_connect_host(const char *host, int port, int timeout_ms);

/* Lee hasta que el servidor cierre y pasa cada bloque a framer_feed.
 * Devuelve 0 si el servidor cerro normalmente, -1 si error o timeout.
 * idle_timeout_ms: maximo tiempo sin recibir nada antes de abandonar.
 * Si el servidor cerro con una PDU a medias, queda en el framer (framer_pending() > 0)
 * y no se entrega: decide quien llama.
 * NO cierra el descriptor. */
int tcp_receive_pdus(int fd, framer *f, pdu_handler on_pdu, void *arg,
                     int idle_timeout_ms);

#endif

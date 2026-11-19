#ifndef PDU_H
#define PDU_H

#include <stddef.h>
#include <time.h>

/* Codigos de estado del parseo de una PDU */
#define PDU_OK                 200
#define PDU_BAD_REQUEST        400
#define PDU_INVALID_TIMESTAMP  405
#define PDU_EXPIRED_TIMESTAMP  406
#define PDU_TIMESTAMP_TOO_LONG 407
#define PDU_INVALID_CHARS      408
#define PDU_MESSAGE_EMPTY      409
#define PDU_MESSAGE_TOO_LONG   410

/* Decisiones abiertas acordadas (plan, seccion 6):
 - En <PDU> del mensaje UDP va la PDU SIN el CR LF final.
 - Orden de chequeo: 400 -> 405 -> 407 -> 406 -> 409 -> 410 -> 408.
 - Timestamp vacio ("&mensaje") -> 405.
 - 406 si la diferencia con la hora actual es mayor a 300 s, hacia atras o hacia adelante.
 - Una PDU truncada por el framer se parsea igual con el comienzo conservado.
 - Se separa en el PRIMER '&'. Un '&' dentro del mensaje es un caracter invalido -> 408. */

/* Valida una PDU ya delimitada (sin CR LF). Usar len, no strlen.
 now: hora actual en segundos Unix, por parametro para poder probar el 406. */
int pdu_parse(const char *pdu, size_t len, time_t now);

/* 1 si la PDU contiene '|' (0x7C): esas NO se reenvian por UDP. */
int pdu_has_pipe(const char *pdu, size_t len);

#define FORWARD_MSG_MAX 1100

/* Arma  <USUARIO>|<PDU>|<CODIGO>|<FIRMA>  con FIRMA = MD5(USUARIO|PDU|CODIGO|SECRETO).
 Devuelve el largo del mensaje (>= 0), -1 si los argumentos son invalidos / el buffer
 es chico / la PDU contiene '\0', y -2 si la PDU contiene '|'. */
int build_forward_msg(const char *user, const char *pdu, size_t pdu_len,
                      int code, const char *secret, char *out, size_t outlen);

#endif

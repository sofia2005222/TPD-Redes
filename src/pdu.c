#include "pdu.h"
#include "proto.h"

#include <stdio.h>
#include <string.h>

/* Limites del formato de PDU (consigna) */
#define TIMESTAMP_MAX_LEN 11
#define MESSAGE_MAX_LEN   100
#define MAX_AGE_SECONDS   300


static int is_valid_message_char(unsigned char c)
/* Indica si un caracter puede ir en el campo MESSAGE.
Params:
 - c: caracter a revisar, como unsigned char para que los bytes >= 0x80 no sean negativos.
Devuelve 1 si esta en 0x20-0x7D y no es '&' (0x26) ni '|' (0x7C). Si no, devuelve 0.
*/
{
    return c >= 0x20 && c <= 0x7D && c != '&' && c != '|';
}


int pdu_parse(const char *pdu, size_t len, time_t now)
/* Valida una PDU ya delimitada (sin CR LF) y devuelve su codigo de estado.
Los chequeos siguen el orden acordado: 400 -> 405 -> 407 -> 406 -> 409 -> 410 -> 408.
Params:
 - pdu: bytes de la PDU. Pueden incluir cualquier byte, incluso '\0'.
 - len: cantidad de bytes de la PDU.
 - now: hora actual en segundos Unix.
Devuelve uno de los codigos PDU_* de pdu.h.
*/
{
    if (pdu == NULL) {
        return PDU_BAD_REQUEST;
    }

    //Busco el primer '&' dentro de los len bytes. memchr usa el largo, no se corta en un '\0'.
    const char *amp = memchr(pdu, '&', len);
    if (amp == NULL) {
        return PDU_BAD_REQUEST;              //No hay 2 campos (incluye la linea vacia)
    }

    //Los dos campos se marcan con puntero + largo, sin copiar nada.
    const char *ts = pdu;
    size_t ts_len = (size_t)(amp - pdu);
    const char *msg = amp + 1;
    size_t msg_len = len - ts_len - 1;

    /* Timestamp */
    if (ts_len == 0) {
        return PDU_INVALID_TIMESTAMP;        //Timestamp vacio
    }
    for (size_t i = 0; i < ts_len; i++) {
        if (ts[i] < '0' || ts[i] > '9') {
            return PDU_INVALID_TIMESTAMP;    //Caracter fuera de 0x30-0x39
        }
    }
    if (ts_len > TIMESTAMP_MAX_LEN) {
        return PDU_TIMESTAMP_TOO_LONG;
    }

    //Recien aca convierto: con 11 digitos como maximo entra en un long long sin desbordar.
    long long ts_value = 0;
    for (size_t i = 0; i < ts_len; i++) {
        ts_value = ts_value * 10 + (ts[i] - '0');
    }
    long long diff = (long long)now - ts_value;
    if (diff > MAX_AGE_SECONDS || diff < -MAX_AGE_SECONDS) {
        return PDU_EXPIRED_TIMESTAMP;
    }

    /* Mensaje */
    if (msg_len == 0) {
        return PDU_MESSAGE_EMPTY;
    }
    if (msg_len > MESSAGE_MAX_LEN) {
        return PDU_MESSAGE_TOO_LONG;
    }
    for (size_t i = 0; i < msg_len; i++) {
        //Atrapa tambien '\r' o '\n' sueltos, '\0' y bytes de UTF-8 como los del '°'.
        if (!is_valid_message_char((unsigned char)msg[i])) {
            return PDU_INVALID_CHARS;
        }
    }

    return PDU_OK;
}


int pdu_has_pipe(const char *pdu, size_t len)
/* Indica si la PDU contiene el caracter '|' (0x7C).
Params:
 - pdu: bytes de la PDU.
 - len: cantidad de bytes de la PDU.
Devuelve 1 si contiene '|', 0 si no.
*/
{
    if (pdu == NULL) {
        return 0;
    }
    return memchr(pdu, '|', len) != NULL;
}


int build_forward_msg(const char *user, const char *pdu, size_t pdu_len,
                      int code, const char *secret, char *out, size_t outlen)
/* Arma el mensaje del desafio 2: "<user>|<pdu>|<code>|<firma>".
La firma es MD5("<user>|<pdu>|<code>|<secret>") y se calcula con proto_sign.
Params:
 - user: nombre del grupo.
 - pdu: bytes de la PDU, sin CR LF.
 - pdu_len: cantidad de bytes de la PDU.
 - code: codigo de estado devuelto por pdu_parse.
 - secret: secreto compartido del grupo.
 - out: espacio donde se guarda el mensaje a enviar.
 - outlen: tamano del espacio, incluye '\0' final.
Devuelve el largo del mensaje si lo arma bien.
Devuelve -2 si la PDU contiene '|' (no se reenvia).
Devuelve -1 si un argumento es invalido, la PDU contiene '\0' o out es muy chico.
*/
{
    char fields[PROTO_MAX_MSG];
    char sig[33];

    if (user == NULL || pdu == NULL || secret == NULL || out == NULL || outlen == 0) {
        return -1;
    }
    //Un '|' romperia el formato: el servidor no sabria donde termina la PDU.
    if (pdu_has_pipe(pdu, pdu_len)) {
        return -2;
    }
    //Un '\0' cortaria el texto al armarlo con snprintf: el mensaje saldria incompleto.
    if (memchr(pdu, '\0', pdu_len) != NULL) {
        return -1;
    }

    //Uno los campos a firmar. %.*s copia exactamente pdu_len bytes (la PDU no trae '\0').
    int n = snprintf(fields, sizeof(fields), "%s|%.*s|%d", user, (int)pdu_len, pdu, code);
    if (n < 0 || (size_t)n >= sizeof(fields)) {
        return -1;
    }

    //Misma firma que en el desafio 1: campos + "|secreto", y el secreto no sale de proto_sign.
    if (proto_sign(fields, secret, sig) != 0) {
        return -1;
    }

    n = snprintf(out, outlen, "%s|%s", fields, sig);
    if (n < 0 || (size_t)n >= outlen) {
        return -1;
    }
    return n;
}

#ifndef FRAMING_H
#define FRAMING_H

#include <stddef.h>

/* Framing del desafio 2: separa el flujo de bytes TCP en PDUs delimitadas por CR LF.
 * No usa sockets: recibe bytes (como lleguen) y entrega lineas completas.
 *
 * TCP no respeta los limites de los mensajes: un recv puede traer media PDU, varias PDUs,
 * o 2,5 PDUs. Por eso hay que acumular hasta ver el delimitador CR LF.
 *
 * Decisiones:
 *  - Solo la secuencia CR LF termina una linea. Un CR o un LF sueltos son parte del contenido
 *    (el parser de la Parte 2 los va a marcar como caracteres invalidos).
 *  - Se conservan como maximo FRAME_MAX - 1 bytes por linea (el ultimo lugar es para el '\0').
 *    Si la linea es mas larga, se entrega el comienzo con truncated = 1 al llegar su CR LF,
 *    y el resto se descarta.
 */

#define FRAME_MAX 512   /* bytes que retiene como maximo por linea */

typedef struct {
    char   buf[FRAME_MAX];
    size_t len;         /* bytes acumulados de la linea en curso */
    int    discarding;  /* 1 = linea demasiado larga: se tira hasta el proximo CR LF
                           (2 = idem, y el ultimo byte descartado fue un CR) */
} framer;

/* Se llama UNA vez por cada PDU delimitada (cada vez que aparece CR LF).
 *   pdu       : texto de la PDU SIN el CR LF final, terminado en '\0'.
 *               Puede contener cualquier byte (incluso '\0'): usar len, no strlen.
 *   len       : largo de pdu, sin contar el '\0' final.
 *   truncated : 1 si la linea paso de FRAME_MAX y solo se conserva el comienzo.
 *   arg       : puntero de usuario recibido en framer_feed.
 * El buffer pdu es valido solo durante la llamada.
 * Las lineas vacias (CR LF solos) tambien se entregan, con len = 0. */
typedef void (*pdu_handler)(const char *pdu, size_t len, int truncated, void *arg);

void   framer_init(framer *f);
void   framer_feed(framer *f, const char *data, size_t len,
                   pdu_handler on_pdu, void *arg);
size_t framer_pending(const framer *f);   /* bytes acumulados sin CR LF */

#endif

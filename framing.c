#include "framing.h"

#include <string.h>

void framer_init(framer *f)
/* Deja el framer vacio: sin bytes acumulados y sin estar descartando. */
{
    memset(f, 0, sizeof(*f));
}

size_t framer_pending(const framer *f)
/* Bytes guardados de una linea que todavia no recibio su CR LF.
Si al cerrar la conexion es > 0, quedo una PDU a medias (no se entrega).
*/
{
    return f->len;
}

static void deliver(framer *f, size_t len, int truncated, pdu_handler on_pdu, void *arg)
/* Entrega los primeros len bytes de buf como una PDU y deja el framer listo para la siguiente.
Pone un '\0' al final para que quien la reciba pueda imprimirla (pero debe usar len).
*/
{
    f->buf[len] = '\0';
    if (on_pdu != NULL) {
        on_pdu(f->buf, len, truncated, arg);
    }
    f->len = 0;
    f->discarding = 0;
}

void framer_feed(framer *f, const char *data, size_t len, pdu_handler on_pdu, void *arg)
/* Procesa un bloque de bytes tal como llego de recv, byte por byte.
El estado (lo acumulado, si se esta descartando) queda guardado en f entre llamadas,
asi una PDU puede venir partida en varios bloques, incluso con el CR al final de un bloque
y el LF al principio del siguiente.

Para cada byte c:
 - Modo normal:
   * si c es LF y el ultimo byte guardado es CR -> fin de linea: se entrega buf sin el CR.
   * si hay lugar (dejando 1 para el '\0') -> se guarda c.
   * si no hay lugar -> la linea es demasiado larga: pasa a modo descarte.
 - Modo descarte (buf ya tiene el comienzo de la linea, que no se toca):
   * si c es LF y el byte anterior fue CR -> fin de linea: se entrega buf con truncated = 1.
   * si no, se tira c, recordando si era CR (discarding = 2) o no (discarding = 1).
*/
{
    for (size_t i = 0; i < len; i++) {
        char c = data[i];

        if (f->discarding) {
            if (c == '\n' && f->discarding == 2) {
                deliver(f, f->len, 1, on_pdu, arg);
            } else {
                f->discarding = (c == '\r') ? 2 : 1;
            }
            continue;
        }

        if (c == '\n' && f->len > 0 && f->buf[f->len - 1] == '\r') {
            /* CR LF: la PDU es todo lo acumulado menos el CR */
            deliver(f, f->len - 1, 0, on_pdu, arg);
        } else if (f->len < FRAME_MAX - 1) {
            f->buf[f->len++] = c;
        } else {
            /* buf lleno: nos quedamos con el comienzo y descartamos hasta el CR LF */
            f->discarding = (c == '\r') ? 2 : 1;
        }
    }
}

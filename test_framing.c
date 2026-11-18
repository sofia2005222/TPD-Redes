#include <stdio.h>
#include <string.h>

#include "framing.h"

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

/* Registro de las PDUs que entrega el framer, para revisarlas despues */
#define MAX_PDUS 10
typedef struct {
    int    count;
    char   pdu[MAX_PDUS][FRAME_MAX];
    size_t len[MAX_PDUS];
    int    truncated[MAX_PDUS];
} record;

static void on_pdu(const char *pdu, size_t len, int truncated, void *arg)
/* Handler de prueba: copia cada PDU entregada en el registro que llega por arg. */
{
    record *r = arg;
    if (r->count < MAX_PDUS) {
        memcpy(r->pdu[r->count], pdu, len + 1);   /* incluye el '\0' */
        r->len[r->count] = len;
        r->truncated[r->count] = truncated;
    }
    r->count++;
}

static int got(const record *r, int i, const char *expected)
/* 1 si la PDU numero i es exactamente expected y no esta truncada. */
{
    return i < r->count && r->len[i] == strlen(expected)
           && memcmp(r->pdu[i], expected, r->len[i]) == 0 && r->truncated[i] == 0;
}

static void feed_str(framer *f, const char *s, record *r)
{
    framer_feed(f, s, strlen(s), on_pdu, r);
}

static void test_byte_by_byte(void)
{
    framer f;
    record r = {0};
    const char *data = "1792303544&Hola\r\n";

    printf("\n--- 1. PDU byte por byte ---\n");
    framer_init(&f);
    for (size_t i = 0; i < strlen(data); i++) {
        framer_feed(&f, data + i, 1, on_pdu, &r);
    }
    check(r.count == 1, "se entrega exactamente 1 vez");
    check(got(&r, 0, "1792303544&Hola"), "completa y sin CR LF");
    check(framer_pending(&f) == 0, "no queda nada pendiente");
}

static void test_three_in_one(void)
{
    framer f;
    record r = {0};

    printf("\n--- 2. Tres PDUs en un solo bloque ---\n");
    framer_init(&f);
    feed_str(&f, "1&uno\r\n2&dos\r\n3&tres\r\n", &r);
    check(r.count == 3, "3 llamadas a on_pdu");
    check(got(&r, 0, "1&uno") && got(&r, 1, "2&dos") && got(&r, 2, "3&tres"), "en orden");
}

static void test_two_and_a_half(void)
{
    framer f;
    record r = {0};

    printf("\n--- 3. 2,5 PDUs y despues el resto ---\n");
    framer_init(&f);
    feed_str(&f, "1&uno\r\n2&dos\r\n3&tr", &r);
    check(r.count == 2, "con el primer bloque se entregan 2");
    check(framer_pending(&f) == 4, "quedan 4 bytes pendientes (\"3&tr\")");
    feed_str(&f, "es\r\n", &r);
    check(r.count == 3, "3 PDUs en total, ninguna duplicada");
    check(got(&r, 0, "1&uno") && got(&r, 1, "2&dos") && got(&r, 2, "3&tres"), "ninguna cortada");
}

static void test_split_crlf(void)
{
    framer f;
    record r = {0};

    printf("\n--- 4. CR al final de un bloque, LF al principio del siguiente ---\n");
    framer_init(&f);
    feed_str(&f, "1&uno\r", &r);
    check(r.count == 0, "con solo el CR todavia no se entrega");
    feed_str(&f, "\n2&dos\r\n", &r);
    check(r.count == 2, "se entregan 2 PDUs");
    check(got(&r, 0, "1&uno") && got(&r, 1, "2&dos"), "una sola PDU por CR LF, sin el CR");
}

static void test_empty_line(void)
{
    framer f;
    record r = {0};

    printf("\n--- 5. Linea vacia ---\n");
    framer_init(&f);
    feed_str(&f, "\r\n", &r);
    check(r.count == 1 && r.len[0] == 0, "se entrega con len = 0");
}

static void test_too_long(void)
{
    framer f;
    record r = {0};
    char big[FRAME_MAX + 100];

    printf("\n--- 6. Linea de mas de FRAME_MAX ---\n");
    framer_init(&f);
    memset(big, 'a', sizeof(big));
    framer_feed(&f, big, sizeof(big), on_pdu, &r);
    check(r.count == 0, "sin CR LF no se entrega nada");
    feed_str(&f, "\r\n1&uno\r\n", &r);
    check(r.count == 2, "se entrega la truncada y la siguiente");
    check(r.truncated[0] == 1 && r.len[0] == FRAME_MAX - 1 && r.pdu[0][0] == 'a',
          "la primera se marca truncada con FRAME_MAX - 1 bytes");
    check(got(&r, 1, "1&uno"), "la siguiente se entrega bien (resincroniza)");

    /* el CR LF de la linea larga llega partido entre dos bloques */
    framer_init(&f);
    r.count = 0;
    framer_feed(&f, big, sizeof(big), on_pdu, &r);
    feed_str(&f, "\r", &r);
    feed_str(&f, "\n2&dos\r\n", &r);
    check(r.count == 2 && r.truncated[0] == 1 && got(&r, 1, "2&dos"),
          "truncada con CR LF partido entre bloques");
}

static void test_pending_on_close(void)
{
    framer f;
    record r = {0};

    printf("\n--- 7. Conexion cerrada con una PDU a medias ---\n");
    framer_init(&f);
    feed_str(&f, "1&uno\r\n2&do", &r);
    check(r.count == 1, "solo se entrega la completa");
    check(framer_pending(&f) > 0, "framer_pending() > 0");
}

static void test_extra(void)
{
    framer f;
    record r = {0};

    printf("\n--- Extra ---\n");
    framer_init(&f);
    feed_str(&f, "1&a\rb\n\r\n", &r);
    check(r.count == 1 && got(&r, 0, "1&a\rb\n"), "CR y LF sueltos quedan dentro de la PDU");

    framer_init(&f);
    r.count = 0;
    framer_feed(&f, "1&a\0b\r\n", 7, on_pdu, &r);
    check(r.count == 1 && r.len[0] == 5 && memcmp(r.pdu[0], "1&a\0b", 5) == 0,
          "un '\\0' en el medio no corta la PDU");
}

int main(void)
{
    test_byte_by_byte();
    test_three_in_one();
    test_two_and_a_half();
    test_split_crlf();
    test_empty_line();
    test_too_long();
    test_pending_on_close();
    test_extra();

    printf("\n%s: %d falla(s)\n", failures == 0 ? "TODO OK" : "HAY FALLAS", failures);
    return failures == 0 ? 0 : 1;
}

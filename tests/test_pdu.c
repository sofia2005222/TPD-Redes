#include <stdio.h>
#include <string.h>

#include "pdu.h"

/* Hora actual de los ejemplos del enunciado */
#define NOW 1792303547

static int failures = 0;

static void check(int ok, const char *description)
/* Imprime el resultado de una prueba y cuenta las fallas.
Params:
 - ok: distinto de 0 si la prueba paso.
 - description: que se esta probando.
*/
{
    if (ok) {
        printf("OK    %s\n", description);
    } else {
        printf("FALLA %s\n", description);
        failures++;
    }
}

static int parse_str(const char *pdu)
/* Atajo para PDUs sin '\0' en el medio: calcula el largo con strlen. */
{
    return pdu_parse(pdu, strlen(pdu), NOW);
}

static void test_examples(void)
{
    printf("\n--- Ejemplos del enunciado (now = %d) ---\n", NOW);
    check(parse_str("1792303544&Temperatura ambiente 11 grados") == PDU_OK,
          "PDU valida -> 200");
    check(parse_str("12345.1&Temperatura ambiente 11 grados") == PDU_INVALID_TIMESTAMP,
          "timestamp con '.' -> 405");
    check(parse_str("1762303544&Temperatura ambiente 11 grados") == PDU_EXPIRED_TIMESTAMP,
          "timestamp viejo -> 406");
    check(parse_str("1111762303544&Temperatura ambiente 11 grados") == PDU_TIMESTAMP_TOO_LONG,
          "timestamp de 13 digitos -> 407");
    check(parse_str("1792303544&Temperatura ambiente 11 \xC2\xB0" "C") == PDU_INVALID_CHARS,
          "mensaje con '\xC2\xB0' (UTF-8) -> 408");
    check(parse_str("1792303544&") == PDU_MESSAGE_EMPTY,
          "mensaje vacio -> 409");
    check(parse_str("1792303544^Temperatura ambiente 11 \xC2\xB0" "C") == PDU_BAD_REQUEST,
          "sin '&' -> 400");
}

static void test_limits(void)
{
    char pdu[300];

    printf("\n--- Limites ---\n");

    //"1792303544&" + 100 'a' -> 200 ; + 101 'a' -> 410
    strcpy(pdu, "1792303544&");
    memset(pdu + 11, 'a', 100);
    pdu[111] = '\0';
    check(parse_str(pdu) == PDU_OK, "mensaje de 100 caracteres -> 200");
    pdu[111] = 'a';
    pdu[112] = '\0';
    check(parse_str(pdu) == PDU_MESSAGE_TOO_LONG, "mensaje de 101 caracteres -> 410");

    check(parse_str("01792303544&hola") == PDU_OK,
          "timestamp de 11 digitos -> no da 407 (200)");
    check(parse_str("001792303544&hola") == PDU_TIMESTAMP_TOO_LONG,
          "timestamp de 12 digitos -> 407");

    check(parse_str("1792303847&hola") == PDU_OK,
          "timestamp 300 s en el futuro -> 200");
    check(parse_str("1792303848&hola") == PDU_EXPIRED_TIMESTAMP,
          "timestamp 301 s en el futuro -> 406");
    check(parse_str("1792303247&hola") == PDU_OK,
          "timestamp 300 s atras -> 200");
    check(parse_str("1792303246&hola") == PDU_EXPIRED_TIMESTAMP,
          "timestamp 301 s atras -> 406");

    check(parse_str("&hola") == PDU_INVALID_TIMESTAMP,
          "timestamp vacio -> 405");
    check(parse_str("1792303544&ho&la") == PDU_INVALID_CHARS,
          "segundo '&' dentro del mensaje -> 408");
    check(parse_str("1792303544&hola|chau") == PDU_INVALID_CHARS,
          "'|' dentro del mensaje -> 408");
    check(parse_str("1792303544&~") == PDU_INVALID_CHARS,
          "'~' (0x7E, fuera de rango) -> 408");
    check(parse_str("1792303544& }") == PDU_OK,
          "' ' (0x20) y '}' (0x7D), los extremos del rango -> 200");
}

static void test_framer_cases(void)
{
    printf("\n--- Casos que llegan del framer (Parte 1) ---\n");

    check(pdu_parse("", 0, NOW) == PDU_BAD_REQUEST,
          "linea vacia (len = 0) -> 400");
    check(parse_str("1792303544&hola\rchau") == PDU_INVALID_CHARS,
          "'\\r' suelto dentro del mensaje -> 408");
    check(parse_str("1792303544&hola\nchau") == PDU_INVALID_CHARS,
          "'\\n' suelto dentro del mensaje -> 408");

    //Un '\0' en el medio: no se puede usar strlen, el largo se cuenta a mano.
    const char with_nul[] = "1792303544&ho\0la";
    size_t with_nul_len = sizeof(with_nul) - 1;
    check(pdu_parse(with_nul, with_nul_len, NOW) == PDU_INVALID_CHARS,
          "'\\0' en el medio del mensaje -> 408 (usa len, no strlen)");

    const char nul_before_amp[] = "17923\0" "03544&hola";
    check(pdu_parse(nul_before_amp, sizeof(nul_before_amp) - 1, NOW) == PDU_INVALID_TIMESTAMP,
          "'\\0' en el medio del timestamp -> 405");

    //PDU truncada: 511 bytes conservados de una linea mas larga.
    char truncated[511];
    memcpy(truncated, "1792303544&", 11);
    memset(truncated + 11, 'a', sizeof(truncated) - 11);
    check(pdu_parse(truncated, sizeof(truncated), NOW) == PDU_MESSAGE_TOO_LONG,
          "PDU truncada de 511 bytes -> se parsea igual (410)");
}

static void test_pipe(void)
{
    char out[FORWARD_MSG_MAX];
    const char *pdu = "1792303544&hola|chau";

    printf("\n--- pdu_has_pipe ---\n");
    check(pdu_has_pipe(pdu, strlen(pdu)) == 1, "PDU con '|' -> 1");
    check(pdu_has_pipe("1792303544&hola", 15) == 0, "PDU sin '|' -> 0");
    check(build_forward_msg("grupo01", pdu, strlen(pdu), PDU_INVALID_CHARS,
                            "secreto01", out, sizeof(out)) == -2,
          "build_forward_msg con '|' -> -2");
}

static void test_forward(void)
{
    char out[FORWARD_MSG_MAX];
    char small[40];
    const char *pdu = "1792303544&Temperatura ambiente 11 grados";
    const char *expected =
        "grupo01|1792303544&Temperatura ambiente 11 grados|200|5b98b089469caff11d3d4491ebf9a9e4";

    printf("\n--- build_forward_msg ---\n");

    int n = build_forward_msg("grupo01", pdu, strlen(pdu), PDU_OK, "secreto01", out, sizeof(out));
    check(n >= 0 && strcmp(out, expected) == 0, "vector de firma del plan");
    check(n == (int)strlen(expected), "devuelve el largo del mensaje");
    check(strstr(out, "secreto01") == NULL, "el secreto no aparece en el mensaje");

    //Buffer chico: tiene que devolver -1 sin escribir fuera. Pongo una marca justo despues.
    char guarded[sizeof(small) + 1];
    guarded[sizeof(small)] = 'X';
    check(build_forward_msg("grupo01", pdu, strlen(pdu), PDU_OK, "secreto01",
                            guarded, sizeof(small)) == -1
          && guarded[sizeof(small)] == 'X',
          "buffer chico -> -1, sin escribir fuera del buffer");

    const char with_nul[] = "1792303544&ho\0la";
    check(build_forward_msg("grupo01", with_nul, sizeof(with_nul) - 1, PDU_INVALID_CHARS,
                            "secreto01", out, sizeof(out)) == -1,
          "PDU con '\\0' -> -1");

    check(build_forward_msg(NULL, pdu, strlen(pdu), PDU_OK, "secreto01", out, sizeof(out)) == -1,
          "usuario NULL -> -1");
    check(build_forward_msg("grupo01", pdu, strlen(pdu), PDU_OK, "secreto01", NULL, 100) == -1,
          "out NULL -> -1");

    n = build_forward_msg("grupo01", "", 0, PDU_BAD_REQUEST, "secreto01", out, sizeof(out));
    check(n >= 0 && strncmp(out, "grupo01||400|", 13) == 0,
          "linea vacia se puede reenviar -> \"grupo01||400|<firma>\"");

    char truncated[511];
    memcpy(truncated, "1792303544&", 11);
    memset(truncated + 11, 'a', sizeof(truncated) - 11);
    n = build_forward_msg("grupo01", truncated, sizeof(truncated), PDU_MESSAGE_TOO_LONG,
                          "secreto01", out, sizeof(out));
    check(n > 0, "PDU truncada de 511 bytes entra en el mensaje");
}

int main(void)
{
    test_examples();
    test_limits();
    test_framer_cases();
    test_pipe();
    test_forward();

    if (failures == 0)
        printf("\nTodos los tests pasaron.\n");
    else
        printf("\n%d test(s) fallaron.\n", failures);
    return failures != 0;
}

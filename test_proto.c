#include <stdio.h>
#include <string.h>

#include "proto.h"

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

static proto_result parse(const char *msg, int *port, char *text, size_t textlen)
/* Llama a proto_parse_response con el secreto del enunciado.
Calcula el largo con strlen, como si el datagrama hubiera llegado con esos bytes.
*/
{
    return proto_parse_response(msg, strlen(msg), "secreto01", port, text, textlen);
}

static void test_sign(void)
{
    char sig[33];

    printf("\n--- proto_sign ---\n");
    check(proto_sign("grupo01|180.11.1.3", "secreto01", sig) == 0
          && strcmp(sig, "f21d11f08cf40377860fd75d8bfd73cb") == 0,
          "firma del pedido del enunciado");
    check(proto_sign("200|Port|17189", "secreto01", sig) == 0
          && strcmp(sig, "6d720e86a353852c268b2071dffaf069") == 0,
          "firma de la respuesta del enunciado");
}

static void test_build_request(void)
{
    char out[PROTO_MAX_MSG];
    char small[20];

    printf("\n--- proto_build_request ---\n");
    check(proto_build_request("grupo01", "180.11.1.3", "secreto01", out, sizeof(out)) == 0
          && strcmp(out, "grupo01|180.11.1.3|f21d11f08cf40377860fd75d8bfd73cb") == 0,
          "pedido del enunciado");
    check(strstr(out, "secreto01") == NULL,
          "el secreto no aparece en el mensaje");
    check(proto_build_request("grupo01", "180.11.1.3", "secreto01", small, sizeof(small)) == -1,
          "buffer chico devuelve -1");
    check(proto_build_request("grupo01", "180.11.1.3", "secreto01", NULL, 100) == -1,
          "out NULL devuelve -1");
}

static void test_parse_200(void)
{
    int port;
    char text[64];

    printf("\n--- proto_parse_response: 200 ---\n");

    port = -1;
    check(parse("200|Port|17189|6d720e86a353852c268b2071dffaf069", &port, text, sizeof(text)) == PROTO_OK
          && port == 17189,
          "200 del enunciado -> OK, puerto 17189");

    port = -1;
    check(parse("200|Port|17189|6d720e86a353852c268b2071dffaf060", &port, text, sizeof(text)) == PROTO_DISCARD
          && port == -1,
          "200 con firma alterada -> DISCARD, no toca el puerto");

    port = -1;
    check(parse("200|Port|17190|6d720e86a353852c268b2071dffaf069", &port, text, sizeof(text)) == PROTO_DISCARD
          && port == -1,
          "200 con puerto alterado -> DISCARD");

    check(parse("200|Port|17189|6D720E86A353852C268B2071DFFAF069", &port, text, sizeof(text)) == PROTO_DISCARD,
          "200 con firma en mayusculas -> DISCARD");
    check(parse("200|Port|17189|00000000000000000000000000000000", &port, text, sizeof(text)) == PROTO_DISCARD,
          "200 con firma de ceros -> DISCARD");
    check(parse("200|Puerto|17189|6d720e86a353852c268b2071dffaf069", &port, text, sizeof(text)) == PROTO_DISCARD,
          "200 con accion incorrecta -> DISCARD");
    check(parse("200|Port|17x89|6d720e86a353852c268b2071dffaf069", &port, text, sizeof(text)) == PROTO_DISCARD,
          "200 con puerto no numerico -> DISCARD");
    check(parse("200|Port|99999|6d720e86a353852c268b2071dffaf069", &port, text, sizeof(text)) == PROTO_DISCARD,
          "200 con puerto fuera de rango -> DISCARD");
    check(parse("200|Port||6d720e86a353852c268b2071dffaf069", &port, text, sizeof(text)) == PROTO_DISCARD,
          "200 con puerto vacio -> DISCARD");
}

static void test_parse_errors(void)
{
    int port = -1;
    char text[64];

    printf("\n--- proto_parse_response: 400 y 401 ---\n");

    text[0] = '\0';
    check(parse("400|Message|Bad format|00000000000000000000000000000000", &port, text, sizeof(text)) == PROTO_REJECTED
          && strcmp(text, "400 Bad format") == 0,
          "400 del enunciado -> REJECTED, texto \"400 Bad format\"");

    text[0] = '\0';
    check(parse("401|Message|Unauthorized|00000000000000000000000000000000", &port, text, sizeof(text)) == PROTO_REJECTED
          && strcmp(text, "401 Unauthorized") == 0,
          "401 del enunciado -> REJECTED, texto \"401 Unauthorized\"");

    check(parse("401|Message|Unauthorized|00000000000000000000000000000000", &port, NULL, 0) == PROTO_REJECTED,
          "401 con text NULL -> REJECTED sin romperse");
    check(parse("401|Message|Unauthorized|0000000000000000000000000000000", &port, text, sizeof(text)) == PROTO_DISCARD,
          "401 con 31 ceros -> DISCARD");
    check(parse("401|Message|Unauthorized|6d720e86a353852c268b2071dffaf069", &port, text, sizeof(text)) == PROTO_DISCARD,
          "401 con firma que no son ceros -> DISCARD");
    check(parse("401|Port|Unauthorized|00000000000000000000000000000000", &port, text, sizeof(text)) == PROTO_DISCARD,
          "401 con accion incorrecta -> DISCARD");
    check(port == -1,
          "los errores no tocan el puerto");
}

static void test_parse_malformed(void)
{
    int port = -1;
    char text[64];
    char big[PROTO_MAX_MSG + 10];
    const char with_nul[] = "200|Port|17189\0|6d720e86a353852c268b2071dffaf069";

    printf("\n--- proto_parse_response: mal formados ---\n");

    check(parse("403|Message|Forbidden|00000000000000000000000000000000", &port, text, sizeof(text)) == PROTO_DISCARD,
          "codigo desconocido -> DISCARD");
    check(parse("200|Port|17189", &port, text, sizeof(text)) == PROTO_DISCARD,
          "3 campos -> DISCARD");
    check(parse("200|Port|17189|6d720e86a353852c268b2071dffaf069|extra", &port, text, sizeof(text)) == PROTO_DISCARD,
          "5 campos -> DISCARD");
    check(parse("|||", &port, text, sizeof(text)) == PROTO_DISCARD,
          "4 campos vacios -> DISCARD");
    check(parse("", &port, text, sizeof(text)) == PROTO_DISCARD,
          "mensaje vacio -> DISCARD");

    /* sizeof - 1 para no contar el '\0' final que agrega C al texto */
    check(proto_parse_response(with_nul, sizeof(with_nul) - 1, "secreto01", &port, text, sizeof(text)) == PROTO_DISCARD,
          "'\\0' en el medio del datagrama -> DISCARD");

    memset(big, 'a', sizeof(big));
    check(proto_parse_response(big, sizeof(big), "secreto01", &port, text, sizeof(text)) == PROTO_DISCARD,
          "datagrama mas grande que el buffer -> DISCARD");

    check(port == -1,
          "ningun mensaje mal formado toca el puerto");
}

int main(void)
{
    test_sign();
    test_build_request();
    test_parse_200();
    test_parse_errors();
    test_parse_malformed();

    if (failures == 0)
        printf("\nTodos los tests pasaron.\n");
    else
        printf("\n%d test(s) fallaron.\n", failures);
    return failures != 0;
}
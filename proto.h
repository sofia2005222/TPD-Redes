#ifndef PROTO_H
#define PROTO_H

#include <stddef.h>

#define PROTO_MAX_MSG 512

typedef enum {
    PROTO_OK,        /* 200 con firma válida -> tenemos puerto */
    PROTO_DISCARD,   /* inválido o firma mala -> ignorar en silencio y seguir esperando */
    PROTO_REJECTED   /* 400 o 401 con firma de ceros -> informar el error */
} proto_result;

int proto_sign(const char *fields, const char *secret, char out[33]);
int proto_build_request(const char *user, const char *ip, const char *secret, char *out, size_t outlen);
proto_result proto_parse_response(const char *msg, size_t len, const char *secret, int *port, char *text, size_t textlen);

#endif
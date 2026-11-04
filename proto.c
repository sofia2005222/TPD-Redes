#include "proto.h"
#include "md5.h"

#include <stdio.h>
#include <string.h>

/* Firma de las respuestas de error: 32 ceros */
static const char ZERO_SIG[] = "00000000000000000000000000000000";

int proto_sign(const char *fields, const char *secret, char out[33])
/* Calcula la firma de un mensaje: md5("<fields>|<secret>") en hex minuscula.
Es el unico lugar del programa donde se usa el secreto.
Params:
 - fields: campos del mensaje ya unidos con '|' (ej: "grupo01|180.11.1.3").
 - secret: secreto compartido del grupo.
 - out: espacio donde se guarda la firma (32 caracteres hex + '\0').
Devuelve 0 si calcula la firma de forma correcta.
Si el texto a firmar no entra en el buffer, devuelve -1.
*/
{
    char buf[PROTO_MAX_MSG];
    int n = snprintf(buf, sizeof(buf), "%s|%s", fields, secret);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        return -1;
    }
    md5_hex_str(buf, out);
    return 0;
}

int proto_build_request(const char *user, const char *ip, const char *secret, char *out, size_t outlen)
/* Arma el pedido de autenticacion del desafio 1: "<user>|<ip>|<firma>".
Params:
 - user: nombre del grupo.
 - ip: IPv4 publica en texto (ej: "180.11.1.3").
 - secret: secreto compartido del grupo.
 - out: espacio donde se guarda el mensaje a enviar.
 - outlen: tamano del espacio, incluye '\0' final.
Devuelve 0 si arma el mensaje de forma correcta.
Si out es NULL o no tiene espacio suficiente, devuelve -1.
*/
{
    char fields[PROTO_MAX_MSG];
    char signature[33];

    if (out == NULL){
        return -1;
    }

    //uno los campos que se van a firmar: "<user>|<ip>"
    int n = snprintf(fields, sizeof(fields), "%s|%s", user, ip);
    if (n < 0 || (size_t)n >= sizeof(fields)){
        return -1;
    }

    //firmo los campos. El secreto se le pasa a proto_sign y no se copia a ningun lado
    if (proto_sign(fields, secret, signature) != 0){
        return -1;
    }

    //mensaje final: los campos + la firma
    n = snprintf(out, outlen, "%s|%s", fields, signature);
    if (n < 0 || (size_t)n >= outlen){
        return -1;
    }
    return 0;
}

static int split_fields(char *buf, char *fields[4])
/* Separa un texto en exactamente 4 campos delimitados por '|'.
Modifica buf: reemplaza cada '|' por '\0', asi cada campo queda como un texto propio.
Params:
 - buf: texto a separar, terminado en '\0'.
 - fields: arreglo donde se guardan los punteros al comienzo de cada campo.
Devuelve 0 si hay exactamente 4 campos.
Si hay mas o menos de 4, devuelve -1.
*/
{
    int count = 0;
    fields[count++] = buf;
    for (char *p = buf; *p != '\0'; p++) {
        if (*p == '|') {
            if (count == 4) {
                return -1;
            }
            *p = '\0';
            fields[count++] = p + 1;
        }
    }
    return (count == 4) ? 0 : -1;
}

static int parse_port(const char *s, int *port)
/* Convierte el texto de un puerto a numero, validando que sea un puerto valido.
Params:
 - s: texto del puerto (ej: "17189").
 - port: espacio donde se guarda el numero.
Devuelve 0 si el texto son solo digitos y el puerto esta entre 1 y 65535.
Si no, devuelve -1 y no modifica port.
*/
{
    int value = 0;

    if (*s == '\0') {
        return -1;
    }
    for (const char *p = s; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        value = value * 10 + (*p - '0');
        if (value > 65535) {
            return -1;
        }
    }
    if (value == 0) {
        return -1;
    }
    *port = value;
    return 0;
}

proto_result proto_parse_response(const char *msg, size_t len, const char *secret, int *port, char *text, size_t textlen)
/* Interpreta la respuesta del servidor al pedido de autenticacion y decide que hacer con ella.
Params:
 - msg: datagrama recibido (bytes crudos, sin '\0' final).
 - len: cantidad de bytes del datagrama.
 - secret: secreto compartido del grupo, para verificar la firma del 200.
 - port: espacio donde se guarda el puerto si la respuesta es un 200 valido.
 - text: espacio donde se guarda el error (ej: "401 Unauthorized"). Puede ser NULL.
 - textlen: tamano del espacio de text, incluye '\0' final.
Devuelve:
 - PROTO_OK si es un 200 con firma valida (escribe port).
 - PROTO_REJECTED si es un 400 o 401 con firma de 32 ceros (escribe text).
 - PROTO_DISCARD en cualquier otro caso: el mensaje se ignora en silencio.
*/
{
    char buf[PROTO_MAX_MSG];
    char *f[4];

    if (msg == NULL || len >= sizeof(buf)) {
        return PROTO_DISCARD;
    }
    memcpy(buf, msg, len);
    buf[len] = '\0';

    if (strlen(buf) != len) {
        return PROTO_DISCARD;
    }

    //separo los campos
    //f[0] = codigo, f[1] = accion, f[2] = texto, f[3] = firma
    if (split_fields(buf, f) != 0) {
        return PROTO_DISCARD;
    }

    /* Caso 200: se verifica la firma */
    if (strcmp(f[0], "200") == 0) {
        char signed_part[PROTO_MAX_MSG];
        char expected[33];
        int p;

        //chequeo si la accion es Port
        if (strcmp(f[1], "Port") != 0) {
            return PROTO_DISCARD;
        }
        //chequeo si el puerto es valido
        if (parse_port(f[2], &p) != 0) {
            return PROTO_DISCARD;
        }

        //rearmo lo que firmo el server
        snprintf(signed_part, sizeof(signed_part), "%s|%s|%s", f[0], f[1], f[2]);
        //calculo firma que deberia haber mandando el server y comparo con la recibida
        if (proto_sign(signed_part, secret, expected) != 0) {
            return PROTO_DISCARD;
        }
        //firma invalida se descarta en silencio
        if (strcmp(expected, f[3]) != 0) {
            return PROTO_DISCARD;
        }

        //escribo el puerto ya con la firma verificada
        *port = p;
        return PROTO_OK;
    }

    /* Casos 400 y 401: el servidor no puede firmar porque no sabe con que secreto */
    if (strcmp(f[0], "400") == 0 || strcmp(f[0], "401") == 0) {
        //cheque si la accion es Message
        if (strcmp(f[1], "Message") != 0) {
            return PROTO_DISCARD;
        }
        //chequea si la firma son los 32 ceros, un error legitimo siempre trae 32 ceros en lugar de firma
        if (strcmp(f[3], ZERO_SIG) != 0) {
            return PROTO_DISCARD;
        }
        /*copia el texto del error*/
        if (text != NULL && textlen > 0) {
            snprintf(text, textlen, "%s %s", f[0], f[2]);
        }
        return PROTO_REJECTED;
    }

    //cualquier otro codigo no pertenece al protocolo
    return PROTO_DISCARD;
}
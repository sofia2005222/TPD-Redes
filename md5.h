#ifndef MD5_H
#define MD5_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[4];   /* A, B, C, D */
    uint64_t bitcount;   /* largo total del mensaje en bits */
    uint8_t buffer[64];  /* bloque parcial pendiente */
    size_t buflen;       /* bytes usados en buffer */
} md5_ctx;

void md5_init(md5_ctx *ctx);
void md5_update(md5_ctx *ctx, const uint8_t *data, size_t len);
void md5_final(md5_ctx *ctx, uint8_t digest[16]);
void md5_hex_str(const char *str, char out[33]);

#endif
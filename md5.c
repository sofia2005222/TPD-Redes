#include "md5.h"

void md5_init(md5_ctx *ctx)
{
    ctx->state[0] = 0x67452301;   // A
    ctx->state[1] = 0xefcdab89;   // B
    ctx->state[2] = 0x98badcfe;   // C
    ctx->state[3] = 0x10325476;   // D
    ctx->bitcount = 0;
    ctx->buflen   = 0;
}

static void load_words(const uint8_t block[64], uint32_t M[16])
{
    for (int i = 0; i < 16; i++) {
        M[i] = (uint32_t)block[i*4]
     | ((uint32_t)block[i*4 + 1] << 8)
     | ((uint32_t)block[i*4 + 2] << 16)
     | ((uint32_t)block[i*4 + 3] << 24);
    }
}

static uint32_t F(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
static uint32_t G(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }
static uint32_t H(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
static uint32_t I(uint32_t x, uint32_t y, uint32_t z) { return y ^ (x | ~z); }

/* Rota x hacia la izquierda n bits: lo que sale por la izquierda vuelve a entrar por la derecha */
static uint32_t ROTL(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

static const uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static const uint8_t S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,   /* ronda 1 */
    5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,   /* ronda 2 */
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,   /* ronda 3 */
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21    /* ronda 4 */
};

/* Procesa un bloque de 64 bytes y actualiza el estado A, B, C, D */
static void md5_block(uint32_t state[4], const uint8_t block[64])
{
    uint32_t M[16];
    load_words(block, M);

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];

    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;

        if (i < 16) {
            f = F(b, c, d);
            g = i;
        } else if (i < 32) {
            f = G(b, c, d);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = H(b, c, d);
            g = (3 * i + 5) % 16;
        } else {
            f = I(b, c, d);
            g = (7 * i) % 16;
        }

        uint32_t tmp = d;
        d = c;
        c = b;
        b = b + ROTL(a + f + K[i] + M[g], S[i]);
        a = tmp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void md5_update(md5_ctx *ctx, const uint8_t *data, size_t len)
{
    ctx->bitcount += (uint64_t)len * 8;

    while (len > 0) {
        size_t space = 64 - ctx->buflen;
        size_t n = len < space ? len : space;
        memcpy(ctx->buffer + ctx->buflen, data, n);
        ctx->buflen += n;
        data += n;
        len -= n;
        if (ctx->buflen == 64) {
            md5_block(ctx->state, ctx->buffer);
            ctx->buflen = 0;
        }
    }
}

void md5_final(md5_ctx *ctx, uint8_t digest[16])
{
    uint64_t bits = ctx->bitcount;   /* guardar el largo ANTES del padding */

    uint8_t pad = 0x80;              /* la marca */
    md5_update(ctx, &pad, 1);

    pad = 0x00;                      /* ceros hasta tener 56 bytes en el bloque */
    while (ctx->buflen != 56)
        md5_update(ctx, &pad, 1);

    uint8_t len_bytes[8];            /* largo original en bits, little-endian */
    for (int i = 0; i < 8; i++)
        len_bytes[i] = (uint8_t)(bits >> (8 * i));
    md5_update(ctx, len_bytes, 8);   /* completa el bloque y lo procesa */

    for (int i = 0; i < 4; i++) {    /* A, B, C, D como bytes, little-endian */
        digest[i * 4]     = (uint8_t)(ctx->state[i]);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i] >> 24);
    }
}

void md5_hex_str(const char *str, char out[33])
{
    static const char hex[] = "0123456789abcdef";
    md5_ctx ctx;
    uint8_t digest[16];

    md5_init(&ctx);
    md5_update(&ctx, (const uint8_t *)str, strlen(str));
    md5_final(&ctx, digest);

    for (int i = 0; i < 16; i++) {
        out[i * 2]     = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    out[32] = '\0';
}
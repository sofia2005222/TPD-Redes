#include <stdio.h>
#include <string.h>

#include "md5.h"

static int failures = 0;

static void check(const char *input, const char *expected)
{
    char out[33];
    md5_hex_str(input, out);
    if (strcmp(out, expected) == 0) {
        printf("OK    md5(\"%s\") = %s\n", input, out);
    } else {
        printf("FALLA md5(\"%s\")\n  obtenido: %s\n  esperado: %s\n",
               input, out, expected);
        failures++;
    }
}

int main(void)
{
    check("", "d41d8cd98f00b204e9800998ecf8427e");
    check("a", "0cc175b9c0f1b6a831c399e269772661");
    check("abc", "900150983cd24fb0d6963f7d28e17f72");
    check("message digest", "f96b697d7cb7938d525a2f31aaf161d0");
    check("abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b");
    check("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
          "d174ab98d277d9f5a5611c2c9f419d9f");
    check("1234567890123456789012345678901234567890"
          "1234567890123456789012345678901234567890",
          "57edf4a22be3c955ac49da2e2107b67a");

    /* Ejemplos del enunciado */
    check("grupo01|180.11.1.3|secreto01", "f21d11f08cf40377860fd75d8bfd73cb");
    check("200|Port|17189|secreto01", "6d720e86a353852c268b2071dffaf069");

    if (failures == 0)
        printf("\nTodos los tests pasaron.\n");
    else
        printf("\n%d test(s) fallaron.\n", failures);
    return failures != 0;
}
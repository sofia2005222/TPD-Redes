#include <stdio.h>
#include "http_ip.h"

int main(void){
    char public_ip[16];
    if(get_public_ip(public_ip, sizeof(public_ip)) == -1){
        printf("No se pudo encontrar la IP pública\n");
        return 1;
    }
    printf("La IP pública es: %s\n",public_ip);
    return 0;
}
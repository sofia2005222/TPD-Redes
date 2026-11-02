#include "http_ip.h"
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <stdio.h>


int get_public_ip(char *out, size_t outlen)
/* Obtiene la IPv4 publica consultando api.ipify.org mediante HTTP y la guarda en out. 
Params:
 - out: espacio donde se guarda la IP texto.
 - outlen: tamano del espacio, incluye '\0' final.
Procedimiento: 
 1. criterions: buscamos direcciones IPv4 para usar TCP.
    getaddrinfo busca direcciones de api.ipify.org puerto 80 y las deja en directions.
 2. Crea socket e intenta conectarse a la primer direccion. 
 3. Envia pedido HTTP/1.0 con Connection: close solicita cierre de conexion al terminar la respuesta.
 4. Recibe y acumula la respuesta hasta llegar al EOF porque se puede entregar en varias lecturas. 
 5. Cierra socket y libera la lista.
 6. Comprueba que el codigo HTTP sea 200 y busca "\r\n\r\n" que separa las cabeceras del body.
 7. Quita espacios y saltos de linea de extremos del body y usa inet_pton para comprobar que contenga IPv4 valida. 
 8. Comprueba que out tenga espacio y copia la IP con su '\0'.
Devuelve 0 si obtiene IP de forma correcta. 
Si falla, devuelve -1.
Cierra el socket y libera la lista. 
*/
{
    struct addrinfo criterions = {0}; 
    struct addrinfo *directions = NULL; 
    // Queremos direcciones IPv4 para una conexion TCP
    criterions.ai_family = AF_INET;
    criterions.ai_socktype = SOCK_STREAM;
    int result = getaddrinfo("api.ipify.org","80",&criterions,&directions);
    if (result != 0) {return -1;} //Fallo busqueda
    // Creo socket: utilixo parametros de la primer direccion encontrada. 
    int s_socket = socket(directions->ai_family,directions->ai_socktype,directions->ai_protocol);
    if (s_socket == -1) { // Fallo creacion
        freeaddrinfo(directions);
        return -1;
    }
    //Establezco conexion:
    int connection_result = connect(s_socket, directions->ai_addr,directions->ai_addrlen);
    if (connection_result == -1) { //Falla:
        close(s_socket);
        freeaddrinfo(directions);
        return -1;
    }
    //Close solicita cerrar al terminar 
    const char request[] = "GET / HTTP/1.0\r\n" "Host: api.ipify.org\r\n" "Connection: close\r\n" "\r\n";
    size_t request_length = strlen(request);
    size_t total_sent = 0;
    //Send puede enviar solo una parte asi que repito hasta enviar todo el pedido. 
    while (total_sent < request_length) {
        ssize_t bytes_sent = send(s_socket, request + total_sent, request_length - total_sent,0);
        if (bytes_sent <= 0){close(s_socket);freeaddrinfo(directions);return -1;}
        total_sent += (size_t)bytes_sent;
    }
    //Recibo respuesta: acumulo la respuesta que puede llegar en varias lecturas. 
    char response[8192];
    size_t total_received = 0;
    while (1) {
        //Reservo un byte para el '\0' final:
        size_t available_space = sizeof(response) - 1 - total_received;
        if (available_space == 0){
            close(s_socket);
            freeaddrinfo(directions);
            return -1; 
        }
        ssize_t bytes_received = recv(s_socket, response + total_received, available_space,0);
        if (bytes_received == -1) {
            close(s_socket);
            freeaddrinfo(directions);
            return -1; //Error 
        }
        if (bytes_received == 0) {
            break; //EOF: Termino lectura, cierra conexion. 
        }
        total_received += (size_t)bytes_received;
    }
    response[total_received] = '\0';
    close(s_socket);
    freeaddrinfo(directions);
    //Compruebo que rta HTTP tenga codigo 200. 
    int http_status;
    if(sscanf(response, "HTTP/%*u.%*u %d", &http_status) != 1 || http_status!=200){
        return -1;
    }
    //Busco linea vacia que separa las cabeceras del body. 
    char *header_end = strstr(response, "\r\n\r\n");
    if(header_end == NULL){
        return -1;
    }
    //Avanzo cuatro bytes del separador hasta el comienzo del body. 
    char *ip_text = header_end + 4;
    //Saltar espacios al principio:
    while(isspace((unsigned char)*ip_text)){
        ip_text++;
    }
    //Buscar el final del texto:
    char *ip_end = ip_text + strlen(ip_text);
    //Retrocede sobre espacios del final: 
    while(ip_end > ip_text && isspace((unsigned char)ip_end[-1])){
        ip_end--;
    }
    *ip_end = '\0';
    //Compruebo que sea IPv4 valida: 
    struct in_addr validated_ip;
    if(inet_pton(AF_INET, ip_text, &validated_ip) != 1){
        return -1;
    }
    size_t ip_length = strlen(ip_text);
    //Compruebo que haya espacio para la IP y el '\0'. 
    if(out==NULL||outlen < ip_length + 1){
        return -1;
    }
    memcpy(out, ip_text, ip_length + 1);
    return 0;
}





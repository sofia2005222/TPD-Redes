# TP D — Redes y Comunicación

El cliente obtiene la IPv4 pública por HTTP, se autentica por UDP y abre el
puerto TCP informado por el servidor. Delimita las PDUs por CR LF y reenvía
cada PDU representable por UDP con su código de validación y su firma MD5.

## Compilar y probar

```sh
make
make test
```

Se usa C11 con `-Wall -Wextra`. `make test` ejecuta las pruebas de MD5,
autenticación, framing, TCP local y parseo y firma de PDUs. El servidor de
prueba escucha en loopback. La consulta HTTP real se prueba por separado
con `./bin/test_ip`. `make clean` borra los binarios generados.

## Ejecutar

Desde Bash o Zsh, se puede ingresar el secreto sin eco ni guardarlo en un archivo.

```sh
printf 'Secreto del grupo: '
IFS= read -rs TP_SECRET
printf '\n'
export TP_SECRET
./bin/cliente
unset TP_SECRET
```

También se puede pasar con `./bin/cliente -k "$TP_SECRET"` si la variable
ya está cargada. El cliente usa el usuario `grupo09` y el servidor configurado
en `src/cliente.c`.

El resumen cuenta las líneas delimitadas recibidas, los datagramas
enviados, los descartes al armar el mensaje y los fallos de envío. Las
PDUs inválidas se reenvían con su código si se pueden representar. El
contrato de reenvío descarta las que contienen `|` o un byte nulo y
procesa el prefijo conservado de una línea truncada. Una PDU sin CR LF
al cierre queda pendiente y se informa por separado.

## Capturar la corrida completa

1. En Wireshark, elegí la interfaz de salida a Internet e iniciá una captura
   con el filtro de captura `host 167.114.129.206 or tcp port 80`.
2. Ejecutá el cliente con el secreto del grupo y esperá el resumen.
3. Detené la captura. Revisá la consulta HTTP, el pedido y la respuesta UDP
   en el puerto 20262, el flujo TCP en el puerto anunciado y los reenvíos
   UDP firmados. Confirmá ambos desafíos en la web de la cátedra.
4. Guardá la captura como `capturas/desafio2_exitoso.pcapng`.
   `capturas/desafio1_exitoso.pcapng` corresponde al primer checkpoint.

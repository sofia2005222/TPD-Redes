CC     = gcc
CFLAGS = -Wall -Wextra -Isrc

SRC   = src
TESTS = tests
BIN   = bin

# Modulos comunes (cada uno es un .c con su .h en src/)
MD5     = $(SRC)/md5.c $(SRC)/md5.h
HTTP_IP = $(SRC)/http_ip.c $(SRC)/http_ip.h
PROTO   = $(SRC)/proto.c $(SRC)/proto.h
FRAMING = $(SRC)/framing.c $(SRC)/framing.h
TCP     = $(SRC)/tcp_client.c $(SRC)/tcp_client.h

all: $(BIN)/cliente $(BIN)/test_md5 $(BIN)/test_ip $(BIN)/test_proto $(BIN)/test_framing $(BIN)/test_tcp

# Crea la carpeta de ejecutables si no existe
$(BIN):
	mkdir -p $(BIN)

$(BIN)/cliente: $(SRC)/cliente.c $(PROTO) $(MD5) $(HTTP_IP) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/cliente.c $(SRC)/proto.c $(SRC)/md5.c $(SRC)/http_ip.c

$(BIN)/test_md5: $(TESTS)/test_md5.c $(MD5) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(TESTS)/test_md5.c $(SRC)/md5.c

$(BIN)/test_ip: $(TESTS)/http_test_ip.c $(HTTP_IP) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(TESTS)/http_test_ip.c $(SRC)/http_ip.c

$(BIN)/test_proto: $(TESTS)/test_proto.c $(PROTO) $(MD5) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(TESTS)/test_proto.c $(SRC)/proto.c $(SRC)/md5.c
	./$@

$(BIN)/test_framing: $(TESTS)/test_framing.c $(FRAMING) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(TESTS)/test_framing.c $(SRC)/framing.c
	./$@

$(BIN)/test_tcp: $(TESTS)/test_tcp.c $(TCP) $(FRAMING) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(TESTS)/test_tcp.c $(SRC)/tcp_client.c $(SRC)/framing.c
	./$@

clean:
	rm -rf $(BIN)

.PHONY: all clean

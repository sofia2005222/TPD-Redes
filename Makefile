CC = gcc
CFLAGS = -Wall -Wextra

all: test_md5 test_ip test_proto

test_md5: test_md5.c md5.c md5.h
	$(CC) $(CFLAGS) -o test_md5 test_md5.c md5.c

test_ip: http_test_ip.c http_ip.c http_ip.h
	$(CC) $(CFLAGS) -o test_ip http_test_ip.c http_ip.c

test_proto: test_proto.c proto.c proto.h md5.c md5.h
	$(CC) $(CFLAGS) -o test_proto test_proto.c proto.c md5.c
	./test_proto

clean:
	rm -f test_md5 test_ip test_proto

.PHONY: all clean
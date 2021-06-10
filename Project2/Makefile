UID=405346443

default: build

build: server.c client.c
	gcc -Wall -Wextra -o server server.c
	gcc -Wall -Wextra -o client client.c

clean:
	rm -rf server client *.o *.tar.gz *.file

dist:
	tar -czvf $(UID).tar.gz server.c client.c common.h Makefile README

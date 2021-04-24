# NAME: WEIKENG YANG
# Email: rickuclacs@163.com
# UID: 405346443
UID=405346443

default:server

server:
	gcc -Wall -Wextra -g -o webserver webserver.c
	./webserver 8081 ./

dist: clean
	tar -czvf $(UID).tar.gz webserver.c Makefile README

clean:
	rm -rf server
	# rm -rf server.dSYM
	# rm $(UID).tar.gaz

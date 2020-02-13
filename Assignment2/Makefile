all: superserver

superserver: superserver.c
	gcc superserver.c -o superserver -Wall -pedantic -fsanitize=address

clean:
	rm superserver
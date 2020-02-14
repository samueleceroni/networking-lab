CFLAGS = -Wall -pedantic

debug: CFLAGS += -fsanitize=address
debug: superserver

release: superserver

superserver: superserver.c
	gcc superserver.c -o superserver $(CFLAGS)

clean:
	rm superserver

CC = gcc
CFLAGS = -Wall -Wextra

all: server client

server: dns-server.c
	$(CC) $(CFLAGS) -o server dns-server.c

client: dns-client.c
	$(CC) $(CFLAGS) -o client dns-client.c

clean:
	rm -f server client

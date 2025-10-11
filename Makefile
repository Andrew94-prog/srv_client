srv_all:
	make -C ./srv all
	make -C ./srv_async all

client:
	$(CC) -O2 client.c -o client

all: srv_all client

clean:
	make -C ./srv clean
	make -C ./srv_async clean
	rm -f client

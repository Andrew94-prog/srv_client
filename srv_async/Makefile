OBJS := srv_async.o srv_conn_ctxt.o srv_routines.o srv_send_recv.o srv_sock.o

CFLAGS=-Wall

%.o: %.c
	$(CC) $(CFLAGS) -O2 -c $<

srv_async: $(OBJS)
	$(CC) $^ -o $@

clean:
	rm -f *.o
	rm -f srv_async

all: srv_async

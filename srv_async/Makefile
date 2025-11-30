OBJS := srv_async.o srv_conn.o srv_queue.o

CFLAGS=

%.o: %.c
	$(CC) $(CFLAGS) -O2 -c $<

srv_async: $(OBJS)
	$(CC) $^ -o $@

clean:
	rm -f *.o
	rm -f srv_async

all: srv_async

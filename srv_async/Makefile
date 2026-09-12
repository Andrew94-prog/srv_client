COBJS := srv_async.o srv_conn_ctxt.o srv_routines.o srv_send_recv.o srv_sock.o srv_config.o

CFLAGS=-Wall

%.o: %.c
	$(CXX) $(CFLAGS) -O2 -c $<

srv_async: $(COBJS)
	$(CXX) $^ -o $@

clean:
	rm -f *.o
	rm -f srv_async

all: srv_async

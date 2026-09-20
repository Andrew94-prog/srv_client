COBJS := srv_async.o srv_conn_ctxt.o srv_sock.o srv_config.o srv_routines.o
CPPOBJS := srv_send_recv.o http_msg.o

CFLAGS=-Wall

%.o: %.c
	$(CXX) $(CFLAGS) -O2 -c $<

%.o: %.cpp
	$(CXX) $(CFLAGS) -O2 -c $<

srv_async: $(COBJS) $(CPPOBJS)
	$(CXX) $^ -o $@

clean:
	rm -f *.o
	rm -f srv_async

all: srv_async

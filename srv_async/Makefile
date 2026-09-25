COBJS := srv_async.o srv_conn_ctxt.o srv_sock.o srv_config.o srv_routines.o
CPPOBJS := srv_send_recv.o http_msg.o

DEPS := $(COBJS:.o=.o.d) $(CPPOBJS:.o=.o.d)

CFLAGS=-Wall

%.o: %.c
	$(CXX) $(CFLAGS) -O2 -c $<
	$(CXX) $(CFLAGS) -MM -MG -MT $@ $< -o $@.d.tmp
	mv $@.d.tmp $@.d

%.o: %.cpp
	$(CXX) $(CFLAGS) -O2 -c $<
	$(CXX) $(CFLAGS) -MM -MG -MT $@ $< -o $@.d.tmp
	mv $@.d.tmp $@.d

srv_async: $(COBJS) $(CPPOBJS)
	$(CXX) $^ -o $@

clean:
	rm -f *.o
	rm -f *.d
	rm -f srv_async

all: srv_async

-include $(DEPS)

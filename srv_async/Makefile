COBJS := srv_async.o srv_conn_ctxt.o srv_sock.o srv_config.o srv_routines.o srv_conn_send_recv.o
CPPOBJS := http/srv_http_send_recv.o http/srv_http_msg.o \
           http/srv_http_request_msg.o http/srv_http_response_msg.o \
           http/srv_http_request_get.o http/srv_http_request_head.o \
           http/srv_http_request_post.o http/srv_http_request_put.o \
           http/srv_http_request_delete.o http/srv_http_request_patch.o

DEPS := $(COBJS:.o=.o.d) $(CPPOBJS:.o=.o.d)

CFLAGS=-Wall -I.
CPPFLAGS=-Wall -std=c++20 -I.

%.o: %.c
	$(CXX) $(CFLAGS) -O2 -c $< -o $@
	$(CXX) $(CFLAGS) -MM -MG -MT $@ $< -o $@.d.tmp
	mv $@.d.tmp $@.d

%.o: %.cpp
	$(CXX) $(CPPFLAGS) -O2 -c $< -o $@
	$(CXX) $(CPPFLAGS) -MM -MG -MT $@ $< -o $@.d.tmp
	mv $@.d.tmp $@.d

srv_async: $(COBJS) $(CPPOBJS)
	$(CXX) $^ -o $@

clean:
	rm -f *.o http/*.o
	rm -f *.d http/*.d
	rm -f srv_async

all: srv_async

-include $(DEPS)

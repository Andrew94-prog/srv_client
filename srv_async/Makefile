COBJS := srv_async.o srv_conn_ctxt.o srv_sock.o srv_config.o srv_routines.o srv_conn_send_recv.o

LDLIBS := -lmysqlcppconn

.DEFAULT_GOAL := all

DEPS := $(COBJS:.o=.o.d)

CFLAGS := -Wall -I.
CPPFLAGS := -Wall -std=c++20 -I.

%.o: %.c
	$(CXX) $(CFLAGS) -O2 -c $< -o $@
	$(CXX) $(CFLAGS) -MM -MG -MT $@ $< -o $@.d.tmp
	mv $@.d.tmp $@.d

clean::
	rm -f $(COBJS) $(DEPS)
	rm -f srv_async

include http/Makefile
include libdb/Makefile

ALLOBJS := $(COBJS) $(HTTP_OBJS) $(DBOBJS)

srv_async: $(ALLOBJS)
	$(CXX) $^ -o $@ $(LDLIBS)

all: srv_async

-include $(DEPS)

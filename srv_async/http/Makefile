HTTPOBJS := srv_http_send_recv.o srv_http_msg.o \
            srv_http_request_msg.o srv_http_response_msg.o \
            srv_http_request_get.o srv_http_request_head.o \
            srv_http_request_post.o srv_http_request_put.o \
            srv_http_request_delete.o srv_http_request_patch.o

HTTP_OBJS := $(addprefix http/,$(HTTPOBJS))
HTTP_DEPS := $(HTTP_OBJS:.o=.o.d)

http/%.o: http/%.cpp
	$(CXX) $(CPPFLAGS) -O2 -c $< -o $@
	$(CXX) $(CPPFLAGS) -MM -MG -MT $@ $< -o $@.d.tmp
	mv $@.d.tmp $@.d

clean::
	rm -f $(HTTP_OBJS) $(HTTP_DEPS)

-include $(HTTP_DEPS)

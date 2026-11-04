DBOBJS := libdb/srv_db.o

DB_DEPS := $(DBOBJS:.o=.o.d)

libdb/%.o: libdb/%.cpp
	$(CXX) $(CPPFLAGS) -O2 -c $< -o $@
	$(CXX) $(CPPFLAGS) -MM -MG -MT $@ $< -o $@.d.tmp
	mv $@.d.tmp $@.d

clean::
	rm -f $(DBOBJS) $(DB_DEPS)

-include $(DB_DEPS)

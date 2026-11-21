FSOBJS := libdb/srv_fs.o

FS_DEPS := $(FSOBJS:.o=.o.d)

libdb/%.o: libfs/%.cpp
	$(CXX) $(CPPFLAGS) -O2 -c $< -o $@
	$(CXX) $(CPPFLAGS) -MM -MG -MT $@ $< -o $@.d.tmp
	mv $@.d.tmp $@.d

clean::
	rm -f $(FSOBJS) $(FS_DEPS)

-include $(FS_DEPS)
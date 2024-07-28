include /opt/fpp/src/makefiles/common/setup.mk

all: libfpp-lor-inputpup.so
debug: all

OBJECTS_fpp_lor_inputpup_so += src/InputPupPlugin.o
LIBS_fpp_lor_inputpup_so += -L/opt/fpp/src -lfpp
CXXFLAGS_src/SerialEventPlugin.o += -I/opt/fpp/src

%.o: %.cpp Makefile
	$(CCACHE) $(CC) $(CFLAGS) $(CXXFLAGS) $(CXXFLAGS_$@) -c $< -o $@

libfpp-lor-inputpup.so: $(OBJECTS_fpp_lor_inputpup_so) /opt/fpp/src/libfpp.so
	$(CCACHE) $(CC) -shared $(CFLAGS_$@) $(OBJECTS_fpp_lor_inputpup_so) $(LIBS_fpp_lor_inputpup_so) $(LDFLAGS) -o $@

clean:
	rm -f libfpp-lor-inputpup.so $(OBJECTS_fpp_lor_inputpup_so)



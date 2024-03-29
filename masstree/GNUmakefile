AR = ar
CC = gcc
CXX = g++
CFLAGS = -g -W -Wall -O2 -fPIC
DEPSDIR := .deps
DEPCFLAGS = -MD -MF $(DEPSDIR)/$*.d -MP
ifeq ($(strip $(MEMMGR)), )
  MEMMGR = -ljemalloc
endif
ifneq ($(strip $(KEYSWAP)), )
  CFLAGS += -DKEYSWAP
endif
ifneq ($(strip $(NOPREFETCH)), )
  CFLAGS += -DNOPREFETCH
endif
ifneq ($(strip $(NOSUPERPAGE)), )
  CFLAGS += -DNOSUPERPAGE
endif
LIBS = -lnuma  -lpthread -lm
LDFLAGS = 

all: mtIndexAPI.a

%.o: %.c config.h $(DEPSDIR)/stamp
	$(CXX) $(CFLAGS) $(DEPCFLAGS) -include config.h -c -o $@ $<

%.o: %.cc config.h $(DEPSDIR)/stamp
	$(CXX) $(CFLAGS) $(DEPCFLAGS) -include config.h -c -o $@ $<

%.S: %.o
	objdump -S $< > $@

libjson.a: json.o string.o straccum.o str.o msgpack.o \
	clp.o kvrandom.o compiler.o kvthread.o
	@/bin/rm -f $@
	$(AR) cru $@ $^

mtIndexAPI.a: mtIndexAPI.o misc.o checkpoint.o $(KVTREES) testrunner.o \
	kvio.o json.o string.o straccum.o str.o msgpack.o \
	clp.o kvrandom.o compiler.o kvthread.o
	$(AR) cru $@ $^

KVTREES = query_masstree.o \
	value_string.o value_array.o value_versioned_array.o \
	string_slice.o

test_string: test_string.o string.o straccum.o compiler.o
	$(CXX) $(CFLAGS) -o $@ $^ $(MEMMGR) $(LDFLAGS) $(LIBS)

jsontest: jsontest.o string.o straccum.o json.o compiler.o
	$(CXX) $(CFLAGS) -o $@ $^ $(MEMMGR) $(LDFLAGS) $(LIBS)

msgpacktest: msgpacktest.o string.o straccum.o json.o compiler.o msgpack.o
	$(CXX) $(CFLAGS) -o $@ $^ $(MEMMGR) $(LDFLAGS) $(LIBS)

config.h: stamp-h

GNUmakefile: GNUmakefile.in config.status
	CONFIG_FILES=$@ CONFIG_HEADERS= $(SHELL) ./config.status

configure config.h.in: configure.ac
	autoreconf -i
	touch config.h.in

config.status: configure
	./configure 

$(DEPSDIR)/stamp:
	mkdir -p $(DEPSDIR)
	touch $@

stamp-h: config.h.in config.status
	CONFIG_FILES= $(SHELL) ./config.status
	echo > stamp-h

clean:
	rm -f test_string *.o libjson.a mtIndexAPI.a config.status
	rm -rf .deps

DEPFILES := $(wildcard $(DEPSDIR)/*.d)
ifneq ($(DEPFILES),)
include $(DEPFILES)
endif

.PHONY: clean all

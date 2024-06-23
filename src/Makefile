.PHONY: all clean

# debug -g -ggdb
# -ffast-math
# -D_POSIX_C_SOURCE -D_XOPEN_SOURCE
CFLAGS = -O2 -fPIC -Wall -std=c11 -D_DEFAULT_SOURCE
export CFLAGS

OFLAGS='-Wl,-rpath,$$ORIGIN,-z,origin'

liblist = ../bin/libastro.so ../bin/libmisc.so ../bin/libfits.so ../bin/libwcs.so
#all: $(liblist) ../bin/csimc ../bin/csimcd ../bin/libsqlitefunctions.so ../bin/telescoped
all: $(liblist) ../bin/csimc ../bin/csimcd ../bin/telescoped

clean:
	@rm -f ../bin/telescoped ../bin/csimc ../bin/csimcd ../bin/libsqlitefunctions.so $(liblist)
	@rm -f csimc/*.o libastro/*.o libmisc/*.o libfits/*.o libwcs/*.o telescoped.csi/*.o

#../bin/libsqlitefunctions.so: extension-functions.c
#	gcc -fPIC -lm -shared extension-functions.c -o ../bin/libsqlitefunctions.so

../bin/csimc: csimc/csimc.c csimc/boot.c csimc/eintrio.c csimc/el.c
	$(CC) $(CFLAGS) $(OFLAGS) -o $@ $^ -lm -Ilibmisc -L../bin -lmisc -lfits -lastro

../bin/csimcd: csimc/csimcd.c
	$(CC) $(CFLAGS) $(OFLAGS) -o $@ $^ -lm -Ilibmisc -L../bin -lmisc -lastro -lfits

../bin/libastro.so:
	$(MAKE) -C libastro

../bin/libmisc.so:
	$(MAKE) -C libmisc

../bin/libfits.so:
	$(MAKE) -C libfits

../bin/libwcs.so:
	$(MAKE) -C libwcs

../bin/telescoped:
	$(MAKE) -C telescoped.csi


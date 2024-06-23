# Makefile for shm and telshow.
# Both are built from one source file, shm.c.

CLDFLAGS =
#CFLAGS := $(CLDFLAGS) -O2 -ffast-math -Wall -I../../libastro -I../../libmisc $(CFLAGS)
CFLAGS := $(CLDFLAGS) -O2 -ffast-math -Wall -I../libastro -I../libmisc $(CFLAGS)
#LDFLAGS = $(CLDFLAGS) -s -L../../build/libs
LDFLAGS = $(CLDFLAGS) -s -L../../bin
XLDFLAGS = $(LDFLAGS) $(MOTIFL)
LIBS = -lm
XLIBS = $(MOTIFLIBS)
ASTROLIBS = -lastro -lmisc -lfits


#all: shm telshow don't really want the xm version
all: telshow

shm: shm.c
	$(CC) $(CFLAGS) $(MOTIFI) -DUSEX -c shm.c
	$(CC) $(XLDFLAGS) -o $@ shm.o $(ASTROLIBS) $(XLIBS) $(LIBS)
	rm shm.o

telshow: shm.c
	$(CC) ${CFLAGS} -c shm.c -o telshow.o
	$(CC) $(LDFLAGS) -o $@ telshow.o $(ASTROLIBS) $(LIBS)
	rm telshow.o

clean:
	touch x.o
	rm -f *.o shm telshow

# For RCS Only -- Do Not Edit
# @(#) $RCSfile: Makefile,v $ $Date: 2006/05/28 01:07:18 $ $Revision: 1.2 $ $Name:  $

CFLAGS += -I../libmisc -I../libfits -I../libastro -I../libfs
LDFLAGS= $(CLDFLAGS)

OBJS =	align2wcs.o \
	delwcsfits.o \
	setwcsfits.o \
	checkwcsfits.o \
	findreg.o \
	findregd.o \
	worldpos.o \
	xyradec.o

../../bin/libwcs.so: $(OBJS)
	gcc -shared -o $@ $(OBJS)


CFLAGS += -I../libmisc -I../libastro -I../libwcs

OBJS =	align2fits.o	\
	fitsbase.o		\
	fitscorr.o	\
	filters.o	\
	fitsip.o

../../bin/libfits.so:	$(OBJS)
	gcc -shared -o $@ $(OBJS)


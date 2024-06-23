CFLAGS += -I../libastro -I../libfits

LIBPATH = ../../bin
BIN = $(LIBPATH)/libmisc.so

OBJS =	catalogs.o 	\
	ccdcamera.o 	\
	ccdshared.o 	\
	ccd_auxcam.o \
	ccd_server.o \
	ccd_ocaas.o \
	cliserv.o 	\
	configfile.o 	\
	crackini.o	\
	csiutil.o 	\
	focustemp.o     \
	funcmax.o	\
	gaussfit.o 	\
	lstsqr.o 	\
	misc.o 		\
	newton.o	\
	photstd.o 	\
	rot.o 		\
	running.o 	\
	scan.o 		\
	strops.o 	\
	telaxes.o	\
	telenv.o	\
	telfits.o	\
	telfifo.o   \
	tts.o

$(BIN):	$(OBJS)
	$(CC) -shared -o $@ $(OBJS)




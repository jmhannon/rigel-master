
CFLAGS += -I../libastro -I../libmisc
LDFLAGS = -L../../bin
LIBS= -lastro -lmisc -lfits -lm
OFLAGS='-Wl,-rpath,$$ORIGIN,-z,origin'

OBJS =	telescoped.o \
	virmc.o \
	axes.o \
	csimc.o \
	dome.o \
	domegeom.o \
	filter.o \
	focus.o \
	lights.o \
	tel.o \
	mountcor.o \
	powerfail.o \
	fifoio.o

../../bin/telescoped: $(OBJS)
	gcc $(CFLAGS) $(OFLAGS) -o $@ $(OBJS) $(LIBS) $(LDFLAGS)


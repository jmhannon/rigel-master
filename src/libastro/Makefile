# Makefile for the basic astronomy routines.

OBJS =	aa_hadec.o \
	aberration.o \
	actan.o \
	airmass.o \
	anomaly.o \
	ap_as.o \
	auxil.o \
	chap95.o \
	chap95_data.o \
	circum.o \
	comet.o \
	dbfmt.o \
	deep.o \
	deltat.o \
	earthsat.o \
	eq_ecl.o \
	eq_gal.o \
	formats.o \
	helio.o \
	libration.o \
	misc.o \
	mjd.o \
	moon.o \
	mooncolong.o \
	nutation.o \
	obliq.o \
	parallax.o \
	plans.o \
	precess.o \
	reduce.o \
	refract.o \
	riset.o \
	riset_cir.o \
	sdp4.o \
	sgp4.o \
	sphcart.o \
	sun.o \
	thetag.o \
	utc_gst.o \
	vsop87.o \
	vsop87_data.o

../../bin/libastro.so:	$(OBJS)
	gcc -shared -o $@ $(OBJS) -lm


clean:
	rm -f *.o ../../bin/libastro.so


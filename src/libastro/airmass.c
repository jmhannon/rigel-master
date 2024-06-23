#include <math.h>

#include "P_.h"
#include "astro.h"

/* given apparent altitude find airmass.
 * R.H. Hardie, 1962, `Photoelectric Reductions', Chapter 8 of Astronomical
 * Techniques, W.A. Hiltner (Ed), Stars and Stellar Systems, II (University
 * of Chicago Press: Chicago), pp178-208.
 */
void
airmass (aa, Xp)
double aa;      /* apparent altitude, rads */
double *Xp;     /* airmasses */
{
    double sm1; /* secant zenith angle, minus 1 */

    /* degenerate near or below horizon */
    if (aa < degrad(3.0))
        aa = degrad(3.0);

    sm1 = 1.0/sin(aa) - 1.0;
    *Xp = 1.0 + sm1*(0.9981833 - sm1*(0.002875 + 0.0008083*sm1));
}


/* STO, March 2011
 * Compute the AIRMASS value using the Kasten-Young approach
 * (Kasten, F., and A. T. Young. 1989. Revised optical air mass tables and approximation formula. Applied Optics 28:4735–4738.)
 *
 *     m = 1.0 / [ cos(Z) + 0.50572 * (96.07995 - Z)^-1.6364 ]
 *
 * This is a more common modern standard, superior to sec(z) or the Hardie methods.
 * There are many other candidates.
 * However, most give pretty much the same result until we reach extreme zenith angles,
 * at which point, it's really a crapshoot anyway for most real astronomical purposes.
 * This Kasten-Young approach seems like the best conservative choice given in my quick review.
 *
 *
 * Returns a values > 1 (typically 1-38).  Values < 1 (e.g. 0) are in error and should not be used.
 */
double
computeAirmass(double aa)
{
    double z = 90.0 - raddeg (aa);

    if (z == 90.0) return 1.0;
    if (z > 89.95) return 38.0;

    double x = 96.07995 - z;
    x = pow(x, -1.6364);
    x *= 0.50572;
    x += cos(degrad(z));
    return 1.0 / x;
}



/* For RCS Only -- Do Not Edit */
static char *rcsid[2] = {(char *)rcsid, "@(#) $RCSfile: airmass.c,v $ $Date: 2001/04/19 21:12:13 $ $Revision: 1.1.1.1 $ $Name:  $"};

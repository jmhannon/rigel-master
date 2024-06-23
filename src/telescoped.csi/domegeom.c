#include <stdio.h>
#include <string.h>
#include <math.h>

/* Module parameters */
double _offsetNorth;
double _offsetEast;
double _offsetHeight;
double _opticalOffset;
double _domeRadius;

/*
offsetNorth: The North-South distance from E-W centerline of dome to the
             E-W centerline of the mount's RA-Dec axis intersection point.
             Positive values indicate a mount that is South(!) of center.
             Equivalent to -Ym in AutomaDome
        
offsetEast: The East-West distance from the N-S centerline of the dome
            to the center of the mount's RA-Dec axis intersection point.
            Positive values indicate a mount that is East of center.
            Equivalent to Xm in AutomaDome(?)
        
offsetHeight: Height above the dome equator of the RA-Dec axis
              intersection point.
              Negative = below equator.
              Equivalent to Zm in AutomaDome
        
opticalOffset: Distance from the mount's RA-Dec axis intersection point
               to the telescopes's Dec-Optical axis intersection point.
               Positive values imply that OTA is west of mount
               when the telescope is pointed slightly east of Zenith,
               or Above mount when pointed to the eastern horizon.
               Equivalent to -Xt in AutomaDome
           
domeRadius: Radius of the dome

All four parameters should be expressed in the same units of distance.
*/
int 
setDomeGeometry(double offsetNorth,
                double offsetEast,
                double offsetHeight,
                double opticalOffset,
                double domeRadius,
                char *outErrMsg)
{
    _offsetNorth = offsetNorth;
    _offsetEast = offsetEast;
    _offsetHeight = offsetHeight;
    _opticalOffset = opticalOffset;
    _domeRadius = domeRadius;
    
    if (domeRadius <= 0) {
        if (outErrMsg != NULL) {
            strcpy(outErrMsg, "Dome radius must be lager than 0");
        }
        return 0; // Error
    }
    return 1; // Success
}
            
        
/*
Based on code in Dsync.bas, from the ASCOMDome source.

haDegs: Hour Angle, in degrees. Positive = West of meridian
decDegs: Declination, in degrees
latDegs: Site Latitude, in degrees

Output values: outAlt, outAz
    where outAlt is in degrees (0 = horizon, 90 = zenith)
    and outAz is in degrees (0 = north, 90 = east)
*/
void 
domeAltAz(double haDegs, double decDegs, double latDegs, double *outAlt, double *outAz)
{
    double ha = haDegs*M_PI/180;
    double dec = decDegs*M_PI/180;
    double lat = latDegs*M_PI/180;
    
    double A = -_offsetNorth + _opticalOffset*cos(lat - M_PI/2)*sin(ha-M_PI);
    double B = _offsetEast + _opticalOffset*cos(ha-M_PI);
    double C = _offsetHeight - _opticalOffset*sin(lat - M_PI/2)*sin(ha-M_PI);
    double D = cos(lat - M_PI/2)*cos(dec)*cos(-ha) + sin(lat - M_PI/2)*sin(dec);
    double E = cos(dec)*sin(-ha);
    double F = -sin(lat - M_PI/2)*cos(dec)*cos(-ha) + cos(lat - M_PI/2)*sin(dec);

    double k = (-(A*D + B*E + C*F) + sqrt( pow(A*D + B*E + C*F, 2) + (D*D + E*E + F*F)*(_domeRadius*_domeRadius - A*A - B*B - C*C))) / (D*D+E*E+F*F);

    double Xdome = A + D*k;
    double Ydome = B + E*k;
    double Zdome = C + F*k;

    /*
    printf("%f\n", ha);
    printf("%f\n", dec);
    printf("%f\n", lat);
    printf("%f\n", A);
    printf("%f\n", B);
    printf("%f\n", C);
    printf("%f\n", D);
    printf("%f\n", E);
    printf("%f\n", F);
    printf("%f\n", k);
    printf("%f\n", Xdome);
    printf("%f\n", Ydome);
    printf("%f\n", Zdome);
    */

    *outAlt = asin(Zdome / _domeRadius)*180/M_PI;
    *outAz = -atan2(Ydome, Xdome)*180/M_PI;

    *outAz += 180;
}

void testDomeGeom()
{
    double alt, az, ha, dec;

    setDomeGeometry(2, -4, 3, 0, 16.5, NULL);

    double lat = 30;

    for (ha = -90; ha < 90; ha += 5) {
        for (dec = -89; dec < 89; dec += 5) {
            domeAltAz(ha, dec, lat, &alt, &az);
            printf("ha= %.4f, dec= %.4f, alt= %.4f, az= %.4f\n", ha, dec, alt, az);
        }
    }
}


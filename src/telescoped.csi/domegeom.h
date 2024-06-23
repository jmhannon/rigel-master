int setDomeGeometry(double offsetNorth,
                double offsetEast,
                double offsetHeight,
                double opticalOffset,
                double domeRadius,
                char *outErrMsg);

void domeAltAz(double haDegs, double decDegs, double latDegs, double *outAlt, double *outAz);


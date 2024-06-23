
/*
    FLI Filter wheel support functions for filter.c

    Note: This file is brought into filter.c via #include
    Not meant to be added directly to project or compiled directly.
    See the use of flags controlling use of this code in the Makefile
*/

#include <unistd.h>
#include <string.h>

#include "libfli.h"

/* STO 3/3/07 - FLI filter functions; */

typedef struct
{
    flidomain_t domain;
    char *dname;
    char *name;
    char *deviceModel;
} filt_t;

int         numfilts = 0;
filt_t *    filt = NULL;
flidev_t    dev;

static int nextHomeCountdown = 0; // How many more filter moves before we rehome?


void findfilts(flidomain_t domain, filt_t **filt)
{
    long r;
    char **tmplist;

    r = FLIList(domain | FLIDEVICE_FILTERWHEEL, &tmplist);
    if (r != 0)
    {
        fifoWrite(Filter_Id, 0, "FLIList library call failed (%d)", r);
    }
    else
    {
        if (tmplist != NULL && tmplist[0] != NULL)
        {
            int i, filts = 0;

            for (i = 0; tmplist[i] != NULL; i++)
                filts++;

            if ((*filt = realloc(*filt, (numfilts + filts) * sizeof(filt_t))) == NULL)
            {
                fifoWrite(Filter_Id, 0, "FLI findFilter realloc() failed");
                return;
            }

            for (i = 0; tmplist[i] != NULL; i++)
            {
                int j;
                filt_t *tmpfilt = *filt + i;
                char *deviceModel = "";

                for (j = 0; tmplist[i][j] != '\0'; j++)
                {
                    if (tmplist[i][j] == ';')
                    {
                        tmplist[i][j] = '\0';
                        deviceModel = &(tmplist[i][j+1]);
                        break;
                    }
                }

                tmpfilt->domain = domain;
                switch (domain)
                {
                    case FLIDOMAIN_PARALLEL_PORT:
                        tmpfilt->dname = "parallel port";
                        break;

                    case FLIDOMAIN_USB:
                        tmpfilt->dname = "USB";
                        break;

                    case FLIDOMAIN_SERIAL:
                        tmpfilt->dname = "serial";
                        break;

                    case FLIDOMAIN_INET:
                        tmpfilt->dname = "inet";
                        break;

                    default:
                        tmpfilt->dname = "Unknown domain";
                        break;
                }
                tmpfilt->name = strdup(tmplist[i]);
                tmpfilt->deviceModel = strdup(deviceModel);
            }

            numfilts += filts;
        }

        FLIFreeList(tmplist);
    }
}
/*
    "ExtFilt" functions.  These are have similar interfaces between different devices.

        Each returns true or false (non-zero or zero)
        On error, these functions call error functions that use fifoWrite directly to
        output status and errors to the display and logs.
        Regardless of output, Talon will output a final error message upon error of
        any of these functions (i.e a FALSE return value)
*/

/* Close an SBIG connection; return 0 if error, 1 if success */
int fli_shutdown()
{
    int i;

    if (filt)
    {
        long r = FLIClose(dev);
        if (r)
        {
            fifoWrite(Filter_Id,0,"Error return closing FLI (%d)",r);
        }

        for (i = 0; i < numfilts; i++)
            free(filt[i].name);

        free(filt);
        filt = NULL;
    }
    return 1;
}

/* Connect to SBIG driver and camera; return 0 if error, 1 if success */
int fli_reset()
{
    long        r;
    char        libver[1024];
    long        tmp1;
    int         connected = 0;

    fli_shutdown();

    /*
    r = FLISetDebugLevel(NULL, FLIDEBUG_ALL);
    if(r) fifoWrite(Filter_Id, 0, "Failed to set debugging mode (%d)", r);
    */

    r = FLIGetLibVersion(libver, sizeof(libver));
    if (!r)
    {
        fifoWrite(Filter_Id, 0, "FLI Library version '%s'", libver);
    }
    else
    {
        fifoWrite(Filter_Id, 0, "FLI GetLibVersion failure (%d)",r);
        return 0;
    }

    // Find fli filters on USB
    findfilts(FLIDOMAIN_USB, &filt);

    if (numfilts == 0)
    {
        fifoWrite(Filter_Id,0,"No filter wheels found.");
    }
    else
    {
        int deviceIndex = -1;
        int i;

        for (i = 0; i < numfilts; i++) {
            char lowerDeviceName[1024];
            int j;

            fifoWrite(Filter_Id,0,"Examining '%s'...", filt[i].deviceModel);

            // Create lowercase copy of name for case-insensitive comparison
            strncpy(lowerDeviceName, filt[i].deviceModel, 1023);
            for (j = 0; j < 1023 && lowerDeviceName[j] != 0; j++) {
                lowerDeviceName[j] = tolower(lowerDeviceName[j]);
            }

            if (strstr(lowerDeviceName, "cfw") != NULL || strstr(lowerDeviceName, "filter") != NULL) {
                fifoWrite(Filter_Id,0,"Found filter wheel!");
                deviceIndex = i;
                break;
            }
        }

        if (deviceIndex == -1) {
            fifoWrite(Filter_Id,0,"No filter wheels found.");
        }
        else {
            fifoWrite(Filter_Id, 0, "Trying filter wheel '%s' from %s domain", filt[deviceIndex].name, filt[deviceIndex].dname);

            r = FLIOpen(&dev, filt[deviceIndex].name, FLIDEVICE_FILTERWHEEL | filt[deviceIndex].domain);
            if (r)
            {
                fifoWrite(Filter_Id, 0, "Unable to open (%d)",r);
            }
            else
            {
#define BUFF_SIZ (1024)
                char buff[BUFF_SIZ];

                r = FLIGetModel(dev, buff, BUFF_SIZ);
                if (!r) fifoWrite(Filter_Id, 0, "  Model: %s", buff);
                r = FLIGetHWRevision(dev, &tmp1);
                if (!r) fifoWrite(Filter_Id, 0, "  Hardware Rev: %ld", tmp1);
                r = FLIGetFWRevision(dev, &tmp1);
                if (!r) fifoWrite(Filter_Id, 0, "  Firmware Rev: %ld", tmp1);

                connected = 1;
            }
        }
    }

    if (!connected) fli_shutdown();

    return (filt != NULL);
}

/* Re-align filter wheel */
int fli_home()
{
    long r;
    long filterStatus = 0xFFFFFFFF;

    r = FLIHomeDevice(dev);
    if (r) {
        fifoWrite(Filter_Id, 0, "FLI error finding home");
        return 0;
    }

    while ((filterStatus & FLI_FOCUSER_STATUS_MOVING_MASK) != 0) {
        usleep(100000);

        r = FLIGetDeviceStatus(dev, &filterStatus);
        if (r) {
            fifoWrite(Filter_Id, 0, "FLIGetDeviceStatus error: %s\n", strerror(-r));
            return 0;
        }
    }

    nextHomeCountdown = FLI_REHOME_AFTER_MOVES;

    return 1;
}


/* Select a filter by number (0 based) */
int fli_select(int position)
{
    long r;

    if (nextHomeCountdown <= 0) {
        fifoWrite(Filter_Id, 0, "Rehoming FLI wheel to prevent drift");
        if (! fli_home()) {
            return 0; // Error
        }
        fifoWrite(Filter_Id, 0, "Rehoming complete");
    }

    r = FLISetFilterPos(dev, position);
    if (r)
    {
        fifoWrite(Filter_Id, 0, "FLI error setting filter number %d; code = %d", position, r);
        return 0;
    }
    nextHomeCountdown--;

    return 1;
}


/* Connect this implementation via our function pointers */
void set_for_fli()
{
    extFilt_reset_func = fli_reset;
    extFilt_shutdown_func = fli_shutdown;
    extFilt_home_func = fli_home;
    extFilt_select_func = fli_select;
}


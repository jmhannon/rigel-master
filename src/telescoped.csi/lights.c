/* handle the Lights channel */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include "P_.h"
#include "astro.h"
#include "circum.h"
#include "configfile.h"
#include "strops.h"
#include "telstatshm.h"
#include "running.h"
#include "misc.h"
#include "telenv.h"
#include "csiutil.h"
#include "cliserv.h"
#include "tts.h"

#include "teled.h"

static void lights_reset (void);
static void lights_set (int intensity);

static void l_set (int intensity);

static int MAXFLINT;
static int lfd;

/* called when we receive a message from the Lights fifo.
 * if msg just ignore.
 */
/* ARGSUSED */
void
lights_msg (msg)
char *msg;
{
    int intensity;

    /* no polling required */
    if (!msg)
        return;

    /* do reset before checking for `have' to allow for new config file */
    if (strncasecmp (msg, "reset", 5) == 0)
    {
        lights_reset();
        return;
    }

    /* ignore if none */
    if (telstatshmp->lights < 0)
        return;

    /* setup? */
    if (!lfd)
    {
        tdlog ("Lights command before initial Reset: %s", msg?msg:"(NULL)");
        return;
    }

    /* crack command -- including some oft-expected pseudnyms */
    if (sscanf (msg, "%d", &intensity) == 1)
        lights_set (intensity);
    else if (strncasecmp (msg, "stop", 4) == 0)
        lights_set (0);
    else if (strncasecmp (msg, "off", 3) == 0)
        lights_set (0);
    else
        fifoWrite (Lights_Id, -1, "Unknown command: %.20s", msg);
}

static void
lights_set (int intensity)
{
    if (intensity < 0)
        fifoWrite (Lights_Id, -2, "Bogus intensity setting: %d", intensity);
    else if (telstatshmp->lights < 0)
        fifoWrite (Lights_Id, -3, "No lights configured");
    else
    {
        if (intensity > MAXFLINT)
        {
            l_set (MAXFLINT);
            telstatshmp->lights = MAXFLINT;
            fifoWrite (Lights_Id, 0, "Ok, but %d reduced to max of %d",
                       intensity, MAXFLINT);
        }
        else
        {
            l_set (intensity);
            telstatshmp->lights = intensity;
            fifoWrite (Lights_Id, 0, "Ok, intensity now %d", intensity);
        }

        if (intensity == 0)
            toTTS ("The lights are now off.");
        else
            toTTS ("The light intensity is now %d.", intensity);
    }
}

static void
lights_reset()
{
#define NTDCFG  (sizeof(tdcfg)/sizeof(tdcfg[0]))
    static int LADDR;
    static CfgEntry tdcfg[] =
    {
        {"MAXFLINT",  CFG_INT, &MAXFLINT},
        {"LADDR",     CFG_INT, &LADDR},
    };
    int n;

    /* read params */
    n = readCfgFile (1, tdcfn, tdcfg, NTDCFG);
    if (n != NTDCFG)
    {
        cfgFileError (tdcfn, n, (CfgPrFp)tdlog, tdcfg, NTDCFG);
        die();
    }

    /* close if open */
    telstatshmp->lights = -1;
    if (lfd)
    {
        l_set (0);      /* courtesy off */
        csiClose (lfd);
        lfd = 0;
    }

    /* (re)open if have lights */
    if (MAXFLINT <= 0 || LADDR < 0)
    {
        fifoWrite (Lights_Id, 0, "Not installed");
        return;
    }
    lfd = csiOpen (LADDR);
    if (lfd < 0)
    {
        tdlog ("Error opening lights: %s", strerror(errno));
        exit(1);
    }

    /* initially off */
    telstatshmp->lights = 0;
    l_set (0);

    fifoWrite (Lights_Id, 0, "Reset complete");
}

/* low level light control */
static void
l_set (int i)
{
    if (lfd)
        csi_w (lfd, "lights(%d);", i);
}

/* For RCS Only -- Do Not Edit */
static char *rcsid[2] = {(char *)rcsid, "@(#) $RCSfile: lights.c,v $ $Date: 2001/04/19 21:12:09 $ $Revision: 1.1.1.1 $ $Name:  $"};

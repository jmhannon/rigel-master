/* show the telstatshm shared memory area in a shell periodicially,
 * or once on stdout. select which based on #ifdef USEX.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#ifdef USEX
#include <Xm/Xm.h>
#include <X11/keysym.h>
#include <Xm/PushB.h>
#include <Xm/Text.h>
#include <Xm/ToggleB.h>
#include <Xm/RowColumn.h>
#include <Xm/Form.h>
#endif /* USEX */

#include "P_.h"
#include "astro.h"
#include "circum.h"
#include "misc.h"
#include "running.h"
// #include "running.h"
#include "strops.h"
#include "telenv.h"
#include "scan.h"
#include "telstatshm.h"
#include "configfile.h"
#include "cliserv.h"

/* build up the screen in a 2-d array */
#define MAXC    80  /* widest possible line -- ok if too big */
#define MAXR    60  /* most number of rows -- ok if too long */
typedef struct
{
    char mem[MAXC*MAXR+1];/* the image, plus sentinel which stays 0 */
    int r, c;       /* max size in use */
} ScreenImage;

static void initOps (int ac, char *av[]);
static void usage(char *pname);
static void initShm(void);
static void initBanner(void);
static void fillSI(void);
static void showQueue (void);
static void wprintf (char *fmt, ...);
static void showSI(void);

static TelStatShm *telstatshmp;

static char tscfn[] = "archive/config/telsched.cfg";
static ScreenImage si;
static int want_sections;
static int want_queue;
static char *blankline = " ";
static char BANNER[MAXC-1];
static int banner_len;

#ifdef USEX
#define POLL_DELAY  100     /* polling period, ms */
static void timerCB (XtPointer client, XtIntervalId *id);
static void makeMain (void);

char *myclass = "SHM";
XtAppContext telstat_app;
Widget toplevel_w;

static String fallbacks[] =
{
    "SHM*highlightThickness: 0",
    "SHM*marginHeight: 4",
    "SHM*marginWidth: 4",
    "SHM*foreground: #11f",
    "SHM*background: wheat",
    "SHM*fontList: -*-lucidatypewriter-medium-r-*-*-12-*-*-*-*-*-*-*",
    NULL
};

/* fill text widget based on differences in current and prior ScreenImage*/
static Widget text_w;
static Widget hold_w;
static ScreenImage si_prior;
#endif /* USEX */

int
main (ac, av)
int ac;
char *av[];
{
#ifdef USEX
    char *vp, title[128];
    Arg args[20];
    int n;

    vp = strchr ("$Revision: 1.1.1.1 $", ':') + 1;
    sprintf (title, "Shm Engineering Status -- Version %.5s", vp);

    n = 0;
    XtSetArg (args[n], XmNiconName, myclass);
    n++;
    XtSetArg (args[n], XmNallowShellResize, True);
    n++;
    XtSetArg (args[n], XmNtitle, title);
    n++;
    toplevel_w = XtAppInitialize (&telstat_app, myclass, NULL, 0,
                                  &ac, av, fallbacks, args, n);

    initOps (ac, av);
    telOELog(basenm(av[0]));    /* after init so usage() shows */

    makeMain();
    initShm();
    initBanner();

    XtAppAddTimeOut (telstat_app, POLL_DELAY, timerCB, 0);

    XtRealizeWidget(toplevel_w);
    XtAppMainLoop (telstat_app);
#else
    initOps (ac, av);
    initShm();
    initBanner();
    fillSI();
    showSI();
#endif /* USEX */

    return (0);
}

/* look for our options.
 * print usage and exit if bad
 */
static void
initOps (int ac, char *av[])
{
    char *pname = av[0];

    while ((--ac > 0) && ((*++av)[0] == '-'))
    {
        char *s;
        for (s = av[0]+1; *s != '\0'; s++)
            switch (*s)
            {
                case 's':
                    want_sections++;
                    break;
                case 'q':
                    want_queue++;
                    break;
                default:
                    usage(pname);
            }
    }

    /* ac remaiing args starting at av[0] */
    if (ac > 0)
        usage(pname);
}

static void
usage(char *pname)
{
    fprintf (stderr, "%s: [-sq]\n", pname);
    fprintf (stderr, "  -s:  break and label each basic section\n");
    fprintf (stderr, "  -q:  include info on observing queue\n");

    exit (1);
}

static void
initShm()
{
    if (open_telshm(&telstatshmp) < 0)
    {
        perror ("shmem");
        exit (1);
    }
}

static void
initBanner()
{
    if (read1CfgEntry (0, tscfn, "BANNER", CFG_STR, BANNER, sizeof(BANNER)))
        strcpy (BANNER, "(Anon)");
    banner_len = strlen (BANNER);
}

/* fill si */
static void
fillSI()
{
    Now *np = &telstatshmp->now;
    Scan *sp = &telstatshmp->scan;
    TelAxes *tap = &telstatshmp->tax;
    char buf1[128], buf2[128], buf3[128];
    int joffset = telstatshmp->jogging_ison;
    int soffset = (sp->running && (sp->rao || sp->deco));
    MotorInfo *mip;
    double tmp;
    int m, id, y;
    double d;
    int t;

    /* init size */
    si.r = 0;
    si.c = 0;

    if (want_sections)
    {
        /* leave two blank lines, center banner in first when know width */
        wprintf (blankline);
        wprintf (blankline);
    }

    /* time and date */
    mjd_cal (mjd, &m, &d, &y);
    id = floor(d);
    wprintf ("   Date :  MDY =  %2d/%02d/%4d   JD = %.5f", m,id,y,mjd+MJD0);

    fs_sexa (buf1, utc_now(np), 2, 3600);
    now_lst(np, &tmp);
    fs_sexa (buf2, tmp, 2, 3600);
    wprintf ("   Time :  LST =  %s    UTC =  %s", buf2, buf1);

    /* loc and wx conditions */
    fs_sexa (buf1, raddeg(lat), 3, 3600);
    fs_sexa (buf2, -raddeg(lng), 4, 3600); /* +W */
    wprintf ("    Loc : NLat = %s  WLong =%s   El = %6.1fm", buf1, buf2,
             elev*ERAD);

    if (time(NULL) - telstatshmp->wxs.updtime < 30)
    {
        WxStats *wp = &telstatshmp->wxs;

        wprintf ("%s : Wind =%3dKPH @ %-3d  Air = %5.1fC %3d%%RH %4.0fmB %5.1fmm",
                 wp->alert ? "WXALERT" : "Weather", wp->wspeed, wp->wdir,
                 temp, wp->humidity, pressure, wp->rain/10.);

        if (wp->auxtmask)
        {
            t = sprintf (buf1, "AuxTemp : ");
            if (wp->auxtmask & 0x1)
                t += sprintf (buf1+t, "Aux0 = %6.2fC     ", wp->auxt[0]);
            else
                t += sprintf (buf1+t, "                   ");
            if (wp->auxtmask & 0x2)
                t += sprintf (buf1+t, "Aux1 = %6.2fC   ", wp->auxt[1]);
            else
                t += sprintf (buf1+t, "                 ");
            if (wp->auxtmask & 0x4)
                t += sprintf (buf1+t, "Aux2 = %6.2fC", wp->auxt[2]);
            wprintf ("%s", buf1);
        }
    }
    else
        wprintf ("WX(Def) : Wind =  0KPH @   0  Air = %5.1fC   0%%RH %4.0fmB   0.0mm",
                 temp, pressure);

    if (want_sections)
    {
        wprintf (blankline);
        wprintf ("Telescope coordinates:");
    }

    /* tel coords */

    fs_sexa (buf1, raddeg(telstatshmp->Calt), 3, 3600);
    fs_sexa (buf2, raddeg(telstatshmp->Caz), 3, 3600);
    fs_sexa (buf3, raddeg(telstatshmp->CPA), 4, 3600);
    wprintf ("Horizon :  Alt = %s     Az = %s   PA =%s", buf1, buf2, buf3);

    fs_sexa (buf1, radhr(telstatshmp->CJ2kRA), 3, 36000);
    fs_sexa (buf2, raddeg(telstatshmp->CJ2kDec), 3, 3600);
    wprintf ("  J2000 :   RA = %s  Dec = %s", buf1, buf2);

    fs_sexa (buf1, radhr(telstatshmp->CARA), 3, 36000);
    fs_sexa (buf2, raddeg(telstatshmp->CADec), 3, 3600);
    fs_sexa (buf3, radhr(telstatshmp->CAHA), 3, 36000);
    wprintf ("    EOD :   RA = %s  Dec = %s   HA = %s", buf1, buf2, buf3);


    /* Eq target, if running */
    switch (telstatshmp->telstate)
    {
        case TS_HUNTING:    /* FALLTHRU */
        case TS_TRACKING:   /* FALLTHRU */
        case TS_SLEWING:
            fs_sexa (buf1, radhr(telstatshmp->DARA), 3, 36000);
            fs_sexa (buf2, raddeg(telstatshmp->DADec), 3, 3600);
            fs_sexa (buf3, radhr(telstatshmp->DAHA), 3, 36000);
            wprintf ("%7.7s :%c  RA = %s  Dec = %s   HA = %s",
                     telstatshmp->scan.obj.o_name,
                     joffset || soffset ? '*' : ' ',
                     buf1, buf2, buf3);

            /* find the next error */
            tmp = telstatshmp->CARA - telstatshmp->DARA;
            fs_sexa (buf1, radhr(tmp), 3, 36000);
            fs_sexa (buf3, -radhr(tmp), 3, 36000);
            tmp = telstatshmp->CADec - telstatshmp->DADec;
            fs_sexa (buf2, raddeg(tmp), 3, 3600);
            wprintf ("   Diff :  dRA = %s dDec = %s  dHA = %s", buf1,buf2,buf3);

            break;

        default:
            break;
    }

    /* offsets */

    if (want_sections)
    {
        wprintf (blankline);
        wprintf ("Offsets being applied:");
    }

    fs_sexa (buf1, -radhr(telstatshmp->mdha), 3, 36000);
    fs_sexa (buf2, raddeg(telstatshmp->mddec), 3, 3600);
    fs_sexa (buf3, radhr(telstatshmp->mdha), 3, 36000);
    wprintf ("   Mesh :  dRA = %s dDec = %s  dHA = %s", buf1, buf2, buf3);

    if (joffset)
    {
        fs_sexa (buf1, -radhr(telstatshmp->jdha), 3, 36000);
        fs_sexa (buf2, raddeg(telstatshmp->jddec), 3, 3600);
        fs_sexa (buf3, radhr(telstatshmp->jdha), 3, 36000);
    }
    else
    {
        fs_sexa (buf1, 0.0, 3, 36000);
        fs_sexa (buf2, 0.0, 3, 3600);
        fs_sexa (buf3, 0.0, 3, 36000);
    }
    wprintf ("Jogging :%c dRA = %s dDec = %s  dHA = %s",
             joffset ? '*' : ' ', buf1, buf2, buf3);

    if (sp->running)
    {
        fs_sexa (buf1, radhr(sp->rao), 3, 36000);
        fs_sexa (buf2, raddeg(sp->deco), 3, 3600);
        fs_sexa (buf3, -radhr(sp->rao), 3, 36000);
    }
    else
    {
        fs_sexa (buf1, 0.0, 3, 36000);
        fs_sexa (buf2, 0.0, 3, 3600);
        fs_sexa (buf3, 0.0, 3, 36000);
    }
    wprintf ("  Sched :%c dRA = %s dDec = %s  dHA = %s",
             soffset ? '*' : ' ', buf1, buf2, buf3);

    /* raw info */

    if (want_sections)
    {
        wprintf (blankline);
        wprintf ("Raw axis values:");
    }

    mip = HMOT;
    fs_sexa (buf1, raddeg(mip->cpos), 4, 3600);
    fs_sexa (buf2, raddeg(mip->dpos), 4, 3600);
    fs_sexa (buf3, raddeg(mip->cvel), 4, 36000);
    wprintf ("  Az/HA :  Enc =%s   Targ =%s  Vel =%s", buf1, buf2, buf3);
    fs_sexa (buf1, raddeg(mip->neglim), 4, 3600);
    fs_sexa (buf2, raddeg(mip->poslim), 4, 3600);
    wprintf ("        : NLim =%s   PLim =%s", buf1, buf2);

    mip = DMOT;
    fs_sexa (buf1, raddeg(mip->cpos), 4, 3600);
    fs_sexa (buf2, raddeg(mip->dpos), 4, 3600);
    fs_sexa (buf3, raddeg(mip->cvel), 4, 36000);
    wprintf ("Alt/Dec :  Enc =%s   Targ =%s  Vel =%s", buf1, buf2, buf3);
    fs_sexa (buf1, raddeg(mip->neglim), 4, 3600);
    fs_sexa (buf2, raddeg(mip->poslim), 4, 3600);
    wprintf ("        : NLim =%s   PLim =%s", buf1, buf2);

    mip = RMOT;
    if (mip->have)
    {
        fs_sexa (buf1, raddeg(mip->cpos), 4, 3600);
        fs_sexa (buf2, raddeg(mip->dpos), 4, 3600);
        fs_sexa (buf3, raddeg(mip->cvel), 4, 36000);
        wprintf (" FldRot :  Mot =%s   Targ =%s  Vel =%s", buf1, buf2,buf3);
        fs_sexa (buf1, raddeg(mip->neglim), 4, 3600);
        fs_sexa (buf2, raddeg(mip->poslim), 4, 3600);
        wprintf ("        : NLim =%s   PLim =%s", buf1, buf2);
    }

    mip = IMOT;
    if (mip->have)
    {
        fs_sexa (buf1, raddeg(mip->cpos), 4, 3600);
        fs_sexa (buf2, raddeg(mip->dpos), 4, 3600);
        fs_sexa (buf3, raddeg(mip->cvel), 4, 36000);
        wprintf (" Filter :%c Mot =%s   Targ =%s  Vel =%s",
                 telstatshmp->filter, buf1, buf2, buf3);
        fs_sexa (buf1, raddeg(mip->neglim), 4, 3600);
        fs_sexa (buf2, raddeg(mip->poslim), 4, 3600);
        wprintf ("        : NLim =%s   PLim =%s", buf1, buf2);
    }

    mip = OMOT;
    if (mip->have)
    {
        tmp = mip->step/((2*PI)*mip->focscale); /* microns/rad */
        wprintf ("  Focus :%c Mot = %9.1f   Targ = %9.1f  Vel = %8.1f %cm",
                 telstatshmp->autofocus ? 'A' : ' ',
                 tmp*mip->cpos, tmp*mip->dpos, tmp*mip->cvel,
#ifdef USEX
                 XK_mu);
#else
                 'u');
#endif
        wprintf ("        : NLim = %9.1f   PLim = %9.1f",
                 tmp*mip->neglim, tmp*mip->poslim);
    }

    /* reference frame */

    if (want_sections)
    {
        wprintf (blankline);
        wprintf ("Telescope reference frame:");
    }

    fs_sexa (buf1, radhr(telstatshmp->tax.HT), 3, 36000);
    fs_sexa (buf2, raddeg(telstatshmp->tax.DT), 3, 3600);
    fs_sexa (buf3, raddeg(telstatshmp->tax.NP), 3, 3600);
    wprintf (" T Pole :   HA = %s  Dec = %s NonP = %s", buf1, buf2, buf3);
    fs_sexa (buf1, raddeg(telstatshmp->tax.XP), 4, 3600);
    fs_sexa (buf2, raddeg(telstatshmp->tax.YC), 3, 3600);
    fs_sexa (buf3, raddeg(telstatshmp->tax.R0), 4, 3600);
    wprintf (" Ax Ref : XPol =%s   YHom = %s   R0 =%s", buf1, buf2, buf3);

    if (want_sections)
    {
        wprintf (blankline);
        wprintf ("Device status:");
    }

    /* telscoped status */

    switch (telstatshmp->telstate)
    {
        case TS_STOPPED:
            strcpy (buf1, "STOPPED ");
            break;
        case TS_SLEWING:
            strcpy (buf1, "SLEWING ");
            break;
        case TS_HUNTING:
            strcpy (buf1, "HUNTING ");
            break;
        case TS_TRACKING:
            strcpy (buf1, "TRACKING");
            break;
        case TS_HOMING:
            strcpy (buf1, "HOMING  ");
            break;
        case TS_LIMITING:
            strcpy (buf1, "LIMITS  ");
            break;
        default:
            strcpy (buf1, "<UNKNOWN>");
            break;
    }
    if (tap->ZENFLIP)
        strcat (buf1, " ZENFLIP");
    if (tap->GERMEQ)
    {
        strcat (buf1, " GERMEQ");
        if (tap->GERMEQ_FLIP)
            strcat (buf1, "-Flip");
        else
            strcat (buf1, "-NoFlip");
    }
    wprintf (" 'Scope : %s", buf1);

    /* dome azimuth, target, status */
    if (telstatshmp->domestate != DS_ABSENT)
    {
        tmp = telstatshmp->domeaz;
        range (&tmp, 2*PI);
        fs_sexa (buf1, raddeg(tmp), 3, 60);
        tmp = telstatshmp->dometaz;
        range (&tmp, 2*PI);
        fs_sexa (buf2, raddeg(tmp), 3, 60);

        switch (telstatshmp->domestate)
        {
            case DS_STOPPED:
                strcpy (buf3, "STOPPED  ");
                break;
            case DS_ROTATING:
                strcpy (buf3, "ROTATING ");
                break;
            case DS_HOMING:
                strcpy (buf3, "HOMING ");
                break;
            default:
                strcpy (buf3, "<UNKNOWN>");
                break;
        }
        if (telstatshmp->autodome)
            strcat (buf3, " AUTO");

        wprintf ("   Dome :   Az = %s    Target = %s %s", buf1, buf2, buf3);
    }

    /* dome shutter */
    if (telstatshmp->shutterstate != SH_ABSENT)
    {
        switch (telstatshmp->shutterstate)
        {
            case SH_ABSENT:
                strcpy (buf1, "<NONE>");
                break;
            case SH_IDLE:
                strcpy (buf1, "IDLE");
                break;
            case SH_OPENING:
                strcpy (buf1, "OPENING");
                break;
            case SH_CLOSING:
                strcpy (buf1, "CLOSING");
                break;
            case SH_OPEN:
                strcpy (buf1, "OPEN");
                break;
            case SH_CLOSED:
                strcpy (buf1, "CLOSED");
                break;
            default:
                strcpy (buf1, "<UNKNOWN>");
                break;
        }
        wprintf ("Shutter : %s", buf1);
    }

    /* camera status and temp */
    switch (telstatshmp->camstate)
    {
        case CAM_IDLE:
            strcpy (buf1, "IDLE     ");
            break;
        case CAM_EXPO:
            strcpy (buf1, "EXPOSING ");
            break;
        case CAM_READ:
            strcpy (buf1, "READING  ");
            break;
        default:
            strcpy (buf1, "<UNKNOWN>");
            break;
    }
    t = telstatshmp->camtemp;   /* C */
    sprintf (buf2, "%3dC = %3dF", t, t*9/5 + 32);
    if (telstatshmp->lights >= 0)
        sprintf (buf3, "Lights = %d", telstatshmp->lights);
    else
        buf3[0] = '\0';
    wprintf (" Camera : Temp = %s Targ = %3dC  %s %s", buf2,
             telstatshmp->camtarg, buf1, buf3);

    /* current work queue, if desired */
    if (want_queue)
        showQueue();

    /* go back and add banner centered */
    if (want_sections)
        memcpy (&si.mem[(si.c-banner_len)/2], BANNER, banner_len);
}

static void
showQueue ()
{
    Scan *sp = &telstatshmp->scan;
    int telrunok;

    if (want_sections)
    {
        wprintf (blankline);
        wprintf ("Batch Observing Queue:");
    }

    telrunok = !testlock_running ("telrun");

    /* if not running just say so -- no point in showing Q */
    if (!telrunok)
    {
        wprintf (" Telrun : not running.. observing queue is disabled.");
        return;
    }

    /* show queue, implies telrun is ok */
    if (sp->running || sp->starttm)
    {
        char *timstr = asctime (gmtime (&sp->starttm));
        timstr[strlen(timstr)-1] = '\0';    /* no \n */

        wprintf ("  %s : %s UTC", sp->running ? "      Began" : "Next begins",
                 timstr);
        wprintf ("       Source : %s", sp->obj.o_name);
        wprintf ("  Start delta : %d secs", sp->startdt);
        wprintf ("        Title : %s", sp->title);
        wprintf ("      Schedfn : %s", sp->schedfn);
        wprintf ("     Observer : %s", sp->observer);
        wprintf ("    ImageFile : %s", sp->imagefn);
        wprintf ("      Comment : %s", sp->comment);
        wprintf ("  Compression : %d", sp->compress);
        wprintf ("  sx/sy/sw/sh : %d/%d/%d/%d",sp->sx,sp->sy,sp->sw,sp->sh);
        wprintf ("    binx/biny : %d/%d", sp->binx, sp->biny);
        wprintf ("     Duration : %g secs", sp->dur);
        wprintf ("      Shutter : %s", ccdSO2Str(sp->shutter));
        wprintf ("     CCDCalib : %s", ccdCalib2Str(sp->ccdcalib));

    }
    else
    {
        wprintf ("  Queue : enabled but empty.");
    }
}

/* add a row to si */
static void
wprintf (char *fmt, ...)
{
    char *row = &si.mem[MAXC*si.r++];
    char buf[2048];
    va_list ap;
    int l;

    if (si.r > MAXR)
    {
        fprintf (stderr, "Too many rows. Max is %d\n", MAXR);
        exit(1);
    }

    /* guard against long lines by staging in buf[] */
    va_start (ap, fmt);
    l = vsprintf (buf, fmt, ap);
    va_end (ap);
    strncpy (row, buf, MAXC-1);
    if (l > MAXC-1)
        l = MAXC-1;

    if (l > si.c)
        si.c = l;

    /* pad with blanks and add nl */
    while (l < MAXC-1)
        row[l++] = ' ';
    row[l] = '\n';
}

#ifdef USEX

/* fill text_w with difference in si and si_prior, then save in prior */
static void
showSI()
{
    int r;

    if (si.r != si_prior.r || si.c != si_prior.c)
        XtVaSetValues (text_w,
                       XmNcolumns, si.c,
                       XmNrows, si.r,
                       NULL);

    for (r = 0; r < si.r; r++)
    {
        int ri = r*MAXC;
        char *rsip = &si.mem[ri];
        int c;

        for (c = 0; c < si.c; c++)
        {
            int c0 = c;

            while (rsip[c] != si_prior.mem[ri+c])
                c++;

            if (c > c0)
            {
                char save = rsip[c];
                rsip[c] = '\0';
                XmTextReplace (text_w, ri+c0, ri+c, &rsip[c0]);
                rsip[c] = save;
            }
        }
    }

    si_prior = si;
}

static void
timerCB (client, id)
XtPointer client;
XtIntervalId *id;
{
    static int holdtbwason;
    XmTextPosition left, right;
    int holdtbison;
    int sel;

    /* turning off the button clears the selection...
     * but making a selection also turns on button...
     */

    sel = XmTextGetSelectionPosition(text_w, &left, &right) && right > left;
    holdtbison = XmToggleButtonGetState(hold_w);

    if (!holdtbison && holdtbwason && sel)
    {
        XmTextClearSelection (text_w, CurrentTime);
        sel = 0;
    }

    if (sel && !holdtbison)
    {
        XmToggleButtonSetState (hold_w, 1, True);
        holdtbison = 1;
    }

    if (!sel && !holdtbison)
    {
        fillSI();
        showSI();
    }

    holdtbwason = holdtbison;

    XtAppAddTimeOut (telstat_app, POLL_DELAY, timerCB, 0);
}

/* set truth of int pointed to by client, unless NULL */
static void
toggleCB (Widget w, XtPointer client, XtPointer call)
{
    int *ip = (int *)client;

    if (ip)
        *ip = XmToggleButtonGetState (w);

    /* force effect immediately unless on hold */
    if (!XmToggleButtonGetState(hold_w))
    {
        fillSI();
        showSI();
    }
}

static void
closeCB (Widget w, XtPointer client, XtPointer call)
{
    exit (0);
}

static void
makeMain ()
{
    Widget f_w, cb_w;
    Arg args[20];
    Widget w;
    int n;

    n = 0;
    XtSetArg (args[n], XmNfractionBase, 13);
    n++;
    f_w = XmCreateForm (toplevel_w, "SHMF", args, n);
    XtManageChild (f_w);

    n = 0;
    XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM);
    n++;
    XtSetArg (args[n], XmNbottomOffset, 10);
    n++;
    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION);
    n++;
    XtSetArg (args[n], XmNleftPosition, 1);
    n++;
    XtSetArg (args[n], XmNset, want_sections);
    n++;
    w = XmCreateToggleButton (f_w, "Sections", args, n);
    XtAddCallback (w, XmNvalueChangedCallback, toggleCB,
                   (XtPointer)(&want_sections));
    XtManageChild (w);

    n = 0;
    XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM);
    n++;
    XtSetArg (args[n], XmNbottomOffset, 10);
    n++;
    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION);
    n++;
    XtSetArg (args[n], XmNleftPosition, 4);
    n++;
    XtSetArg (args[n], XmNset, want_queue);
    n++;
    w = XmCreateToggleButton (f_w, "Queue", args, n);
    XtAddCallback (w, XmNvalueChangedCallback, toggleCB,
                   (XtPointer)(&want_queue));
    XtManageChild (w);

    n = 0;
    XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM);
    n++;
    XtSetArg (args[n], XmNbottomOffset, 10);
    n++;
    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION);
    n++;
    XtSetArg (args[n], XmNleftPosition, 7);
    n++;
    hold_w = XmCreateToggleButton (f_w, "Freeze", args, n);
    XtAddCallback (hold_w, XmNvalueChangedCallback, toggleCB, NULL);
    XtManageChild (hold_w);

    n = 0;
    XtSetArg (args[n], XmNbottomAttachment, XmATTACH_FORM);
    n++;
    XtSetArg (args[n], XmNbottomOffset, 10);
    n++;
    XtSetArg (args[n], XmNleftAttachment, XmATTACH_POSITION);
    n++;
    XtSetArg (args[n], XmNleftPosition, 10);
    n++;
    XtSetArg (args[n], XmNrightAttachment, XmATTACH_POSITION);
    n++;
    XtSetArg (args[n], XmNrightPosition, 12);
    n++;
    cb_w = XmCreatePushButton (f_w, "Quit", args, n);
    XtAddCallback (cb_w, XmNactivateCallback, closeCB, NULL);
    XtManageChild (cb_w);

    n = 0;
    XtSetArg (args[n], XmNtopAttachment, XmATTACH_FORM);
    n++;
    XtSetArg (args[n], XmNleftAttachment, XmATTACH_FORM);
    n++;
    XtSetArg (args[n], XmNrightAttachment, XmATTACH_FORM);
    n++;
    XtSetArg (args[n], XmNbottomAttachment, XmATTACH_WIDGET);
    n++;
    XtSetArg (args[n], XmNbottomWidget, cb_w);
    n++;
    XtSetArg (args[n], XmNbottomOffset, 10);
    n++;
    XtSetArg (args[n], XmNautoShowCursorPosition, False);
    n++;
    XtSetArg (args[n], XmNcursorPositionVisible, False);
    n++;
    XtSetArg (args[n], XmNwordWrap, False);
    n++;
    XtSetArg (args[n], XmNeditable, False);
    n++;
    XtSetArg (args[n], XmNeditMode, XmMULTI_LINE_EDIT);
    n++;
    XtSetArg (args[n], XmNblinkRate, 0);
    n++;
    text_w = XmCreateText (f_w, "Text", args, n);
    XtManageChild (text_w);
}
#else

/* print si to stdout */
static void
showSI()
{
    int r;

    for (r = 0; r < si.r; r++)
    {
        char *rip = &si.mem[r*MAXC];
        int c;
        for (c = MAXC; --c >= 0 && isspace(rip[c]); )
            rip[c] = '\0';
        printf ("%s\n", rip);
    }
}

#endif /* USEX */

/* For RCS Only -- Do Not Edit */
static char *rcsid[2] = {(char *)rcsid, "@(#) $RCSfile: shm.c,v $ $Date: 2001/04/19 21:12:00 $ $Revision: 1.1.1.1 $ $Name:  $"};

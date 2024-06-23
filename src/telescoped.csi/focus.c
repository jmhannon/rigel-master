/* handle the Focus channel */

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
#include "csiutil.h"
#include "virmc.h"
#include "telenv.h"
#include "cliserv.h"
#include "tts.h"
#include "focustemp.h"

#include "teled.h"

// now a configuration setting... so is MAXINTERP
double MINAFDT  = 2.0; /* minimum autofocus temp change to cause a move */

// config setting for enabling/disabling temperature based "autofocus"
int OUSETEMPFOC = 1; // default to on

// during setup, allow a reprieve if we don't have step set yet
static int noOffsetOnHome = 0;

// allow specification of an offset from the current "auto" position.
// seems like this is the sensible way to handle FOCUSOFF from telrun
// to avoid problems like continual accumulation/drift of offsets
// and inability to return back to normal auto position
static double autoFocusOffset = 0;

// If the focuser has a built-in temperature sensor, poll the
// sensor every few seconds. Keep track of that here
static int nextExtTemperatureRefreshTime = 0; // Normally holds result of time(NULL)

/* the current activity, if any */
static void (*active_func) (int first, ...);

/* one of these... */
static void focus_poll(void);
static void focus_reset(int first);
static void focus_home(int first, ...);
static void focus_limits(int first, ...);
static void focus_stop(int first, ...);
static void focus_auto(int first, ...);
static void focus_offset(int first, ...);
static void focus_jog(int first, ...);

/* helped along by these... */
static void initCfg(void);
static void stopFocus(int fast);
void readFocus (void);
static void autoFocus (void);
static double targetPosition (FilterInfo *fip, double newtemp);
static void refreshExtFocusTemp();
static double focusTemp(void);

static double OJOGF;
static int OSHAREDNODE;

// moved from inside autofocus code to here
static char last_filter;    /* filter we last checked */
static double last_temp;    /* temp we last read */
static double lastAutoFocusOffset;    /* last offset we used */

// Support for external focusers (FLI for now)
static int OFLIFOCUS = 0;

// Support for disabling FocusTemp.dat and using filter.cfg temperatures instead
static int ONOFOCUSTEMPDAT = 0;

// Returns true if any of the external focus drivers have been configured
#define EXTFOCUS (OFLIFOCUS)

// External focus driver implementation calls.

int (*extFocus_reset_func)();         // Returns 1 on success; 0 on error
int (*extFocus_shutdown_func)();      // Returns 1 on success; 0 on error
int (*extFocus_home_func)(int first); // Returns 1 if in progress, 0 if done, -1 if error
int (*extFocus_read_position_func)(int *outPos); // Returns 1 on success and populate outPos; 0 on error
int (*extFocus_stop_func)();          // Returns 1 on success; 0 on error
int (*extFocus_goto_func)(int first, int target); // Returns 1 if in progress, 0 if done, -1 if error
int (*extFocus_jog_func)(int cmd);    // cmd=1 to jog positive, -1 to jog negative, 0 to monitor. Returns 1 on success, 0 on error
int (*extFocus_gettemp_func)(double *tempOut) = NULL;    // Returns 1 on success and populates tempOut (in celsius); 0 on error

// Helpful wrappers for calling these funcs
#define extFocus_reset (*extFocus_reset_func)
#define extFocus_shutdown (*extFocus_shutdown_func)
#define extFocus_home (*extFocus_home_func)
#define extFocus_read_position (*extFocus_read_position_func)
#define extFocus_stop (*extFocus_stop_func)
#define extFocus_goto (*extFocus_goto_func)
#define extFocus_jog  (*extFocus_jog_func)
#define extFocus_gettemp  (*extFocus_gettemp_func)

// Bring in device-specific code if compiler is told to do so
#ifdef USE_FLI
    #include "fli_focus.c"
#endif


/* called when we receive a message from the Focus fifo.
 * if !msg just update things.
 */
/* ARGSUSED */
void
focus_msg (msg)
char *msg;
{
    char jog[10];
	double tmpAutoFocusOffset = 0;

    /* do reset before checking for `have' to allow for new config file */
    if (msg && strncasecmp (msg, "reset", 5) == 0)
    {
        focus_reset(1);
        return;
    }

    if (!OMOT->have)
    {
        if (msg)
            fifoWrite (Focus_Id, 0, "Ok, but focuser not really installed");
        return;
    }

    /* setup? */
    if (!virtual_mode && !EXTFOCUS)
    {
        if (!MIPCFD(OMOT))
        {
            tdlog ("Focus command before initial Reset: %s", msg?msg:"(NULL)");
            return;
        }
    }

    if (!msg)
        focus_poll();
    else if (strncasecmp (msg, "home", 4) == 0)
        focus_home(1);
    else if (strncasecmp (msg, "stop", 4) == 0)
        focus_stop(1);
    else if (strncasecmp (msg, "limits", 6) == 0)
        focus_limits(1);
    else if (strncasecmp (msg, "auto", 4) == 0)
        focus_auto(1);
    else if (sscanf (msg, "j%1[0+-]", jog) == 1)
        focus_jog (1, jog[0]);
    else if (sscanf (msg, "ao%lf", &tmpAutoFocusOffset) == 1) {
		// KMI 11/2/2012 -- new command to assist FOCUSOFF with Rigel spectrometer
		autoFocusOffset = tmpAutoFocusOffset;
        focus_auto(1);
	}
    else if (strncasecmp (msg, "aoreset", 7) == 0) {
		// KMI 11/2/2012 -- reset the focus offset without changing auto-focus state
		autoFocusOffset = 0;
        fifoWrite(Focus_Id, 0, "Auto focus offset reset complete");
	}
    else
        focus_offset (1, atof(msg));
}

/* no new messages.
 * goose the current objective, if any.
 */
static void
focus_poll()
{
    if (time(NULL) > nextExtTemperatureRefreshTime) {
        refreshExtFocusTemp();
        nextExtTemperatureRefreshTime = time(NULL) + 5; // Every 5 seconds
    }

    if (virtual_mode)
    {
        MotorInfo *mip = OMOT;
        vmcService(mip->axis);
    }
    if (active_func)
        (*active_func)(0);
    else if (telstatshmp->autofocus)
        autoFocus();
    /* TODO: monitor while idle? */
}

/* stop and reread config files */
static void
focus_reset(int first)
{
    MotorInfo *mip = OMOT;
    int had = mip->have;

    initCfg();

    /* TODO: for some reason focus behaves badly if you just close/reopen.
     * N.B. "had" relies on telstatshmp being zeroed when telescoped starts.
     */
    if (mip->have)
    {
        if (virtual_mode)
        {
            if (vmcSetup(mip->axis,mip->maxvel,mip->maxacc,mip->step,mip->sign))
            {
                mip->ishomed = 0;
            }
            vmcReset(mip->axis);
        }
        else if (EXTFOCUS)
		{
			extFocus_reset(); // (Disconnect and) connect to external focuser
		}
		else
        {
            if (!had) csiiOpen (mip);

            // STO 2007-01-20
            // This is a concession to the implementation that places a dome on the
            // same CSIMC board as the focuser.  If this is done, we must defer
            // initialization until the dome code can handle it.
            if (!OSHAREDNODE)    csiSetup(mip);
        }
        if (!OSHAREDNODE)
        {
            stopFocus(0);
            readFocus ();
            fifoWrite (Focus_Id, 0, "Reset complete");
        }
        else
        {
            fifoWrite(Focus_Id, 0, "Reset deferred on Dome shared node");
        }
    }
    else // Focuser was removed from config file, or never there to begin with
    {
        if (!virtual_mode)
        {
			if (EXTFOCUS) {
				extFocus_shutdown(); // disconnect external focuser
			}
			else { // CSI
				if (had) csiiClose (mip);
			}
        }
        fifoWrite (Focus_Id, 0, "Not installed");
    }
}

/* seek the home position */
static void
focus_home(int first, ...)
{
    MotorInfo *mip = OMOT;
    double ugoal, unow, newtemp;
    FilterInfo *fip;


    // First, handle External Focuser case
    // Lots of duplicated code here right now, unfortunately
    if (EXTFOCUS)
    {
        if (first) {
            mip->ishomed = 0;
            if (extFocus_home(1) < 0) {
                active_func = NULL;
                return;
            }
            mip->homing = 1;

            // Homing successfully kicked off. Come back later to monitor
            active_func = focus_home;
            toTTS ("The focus motor is seeking the home position.");
        }

        switch (extFocus_home(0))
        {
            case -1:
                stopFocus(1);
                mip->homing = 0;
                active_func = NULL;
                return;
            case 1:
                // Still going. Refresh position readout
                readFocus();
                return;
            case 0:
                active_func = NULL;
                fip = findFilter ('\0');
                newtemp = focusTemp();
                ugoal = targetPosition (fip, newtemp);

                fifoWrite (Focus_Id,1,"Homing complete. Now going to %.1fum",ugoal);
                toTTS ("The focus motor has found home and is now going to the initial position.");
                readFocus();
                unow = mip->cpos*mip->step/(2*PI*mip->focscale);
                mip->ishomed = 1;
                mip->homing = 0;
                focus_offset (1, ugoal - unow);
                return;
        }
        return;
    }


    // At this point, assume CSI focuser
    if (first)
    {
        stopFocus(0);
        if (axis_home (mip, Focus_Id, 1) < 0)
        {
            active_func = NULL;
            return;
        }

        /* new state */
        active_func = focus_home;
        toTTS ("The focus motor is seeking the home position.");
    }

    switch (axis_home (mip, Focus_Id, 0))
    {
        case -1:
            stopFocus(1);
            active_func = NULL;
            return;
        case  1:
            break;
        case  0:
            active_func = NULL;
            fip = findFilter ('\0');
            newtemp = focusTemp();
            ugoal = targetPosition (fip, newtemp);

            if (noOffsetOnHome)
            {
                fifoWrite (Focus_Id,0,"Homing complete.");
            }
            else
            {
                fifoWrite (Focus_Id,1,"Homing complete. Now going to %.1fum",ugoal);
            }
            toTTS ("The focus motor has found home and is now going to the initial position.");
            readFocus();
            unow = mip->cpos*mip->step/(2*PI*mip->focscale);
            mip->cvel = 0;
            mip->homing = 0;
            if (!noOffsetOnHome) focus_offset (1, ugoal - unow);
            break;
    }
}

static void
focus_limits(int first, ...)
{
    MotorInfo *mip = OMOT;

    /* maintain cpos and raw */
    // readFocus();

    if (EXTFOCUS)
    {
        fifoWrite(Focus_Id, 0, "Find Limits not currently supported for external focus drivers. Please configure manually.");
        stopFocus(1);
        active_func = NULL;
        return;
    }

    if (first)
    {
        mip->enchome = 1; // hack flag that we are limiting focus
        if (axis_limits (mip, Focus_Id, 1) < 0)
        {
            stopFocus(1);
            active_func = NULL;
            return;
        }

        /* new state */
        active_func = focus_limits;
        toTTS ("The focus motor is seeking both limit positions.");
    }

    switch (axis_limits (mip, Focus_Id, 0))
    {
        case -1:
            stopFocus(1);
            active_func = NULL;
            mip->limiting = mip->enchome = 0;
            return;
        case  1:
            break;
        case  0:
            stopFocus(0);
            active_func = NULL;
            initCfg();      /* read new limits */
            fifoWrite (Focus_Id, 0, "Limits found");
            toTTS ("The focus motor has found both limit positions.");
            mip->limiting = mip->enchome = 0;
            mip->ishomed = 1; // we really are homed
            break;
    }
}

static void
focus_stop(int first, ...)
{
    MotorInfo *mip = OMOT;
    int cfd = MIPCFD(mip);

    /* stay current */
    readFocus();

    if (first)
    {
        /* issue stop */
        stopFocus(0);
        active_func = focus_stop;
    }

    /* wait for really stopped */
    if (EXTFOCUS) {
        // Assume that an external focuser stops more or less
        // immediately, and fall through to the bottom code
    }
    else if (virtual_mode)
    {
        if (vmcGetVelocity(mip->axis) != 0) return;
    }
    else
    {
        if (csi_rix (cfd, "=mvel;") != 0) return;
    }

    /* if get here, it has really stopped */
    active_func = NULL;
    readFocus();
    fifoWrite (Focus_Id, 0, "Stop complete");
}

static void
focus_auto(int first, ...)
{
    /* set mode and might as well get started right away */
    stopFocus(0);
    telstatshmp->autofocus = 1;
    autoFocus();

    /* if still on, report success */
    if (telstatshmp->autofocus)
        fifoWrite (Focus_Id, 0, "Auto-focus enabled");
}

/* handle a relative focus move, in microns */
static void
focus_offset(int first, ...)
{
    static int rawgoal;
    MotorInfo *mip = OMOT;
    int cfd = MIPCFD(mip);


    /* maintain current info */
    readFocus();

    if (first)
    {
        va_list ap;
        double delta, goal;
        char buf[128];

        // make sure we're homed to begin with
        if (axisHomedCheck(mip, buf))
        {
            active_func = NULL;
            stopFocus(0);
            fifoWrite (Focus_Id, -1, "Focus error: %s", buf);
            toTTS ("Focus error: %s", buf);
            return;
        }

        /* fetch offset, in microns, canonical direction */
        va_start (ap, first);
        delta = va_arg (ap, double);
        va_end (ap);

        /* compute goal, in rads from home; check against limits */
        goal = mip->cpos + (2*PI)*delta*mip->focscale/mip->step;
        if (goal > mip->poslim)
        {
            fifoWrite (Focus_Id, -1, "Move is beyond positive limit");
            active_func = NULL;
            return;
        }
        if (goal < mip->neglim)
        {
            fifoWrite (Focus_Id, -2, "Move is beyond negative limit");
            active_func = NULL;
            return;
        }

        /* ok, go for the gold, er, goal */
        if (EXTFOCUS) {
            rawgoal = (int)floor(mip->sign*mip->step*goal/(2*PI) + 0.5);
            extFocus_goto(1, rawgoal);
        }
        else if (virtual_mode)
        {
            rawgoal = (int)floor(mip->sign*mip->step*goal/(2*PI) + 0.5);
            vmcSetTargetPosition(mip->axis, rawgoal);
        }
        else
        {
            if (mip->haveenc)
            {
                rawgoal = (int)floor(mip->esign*mip->estep*goal/(2*PI) + 0.5);
                csi_w (cfd, "etpos=%d;", rawgoal);
            }
            else
            {
                rawgoal = (int)floor(mip->sign*mip->step*goal/(2*PI) + 0.5);
                csi_w (cfd, "mtpos=%d;", rawgoal);
            }
        }
        mip->cvel = mip->maxvel;
        mip->dpos = goal;
        active_func = focus_offset;
        telstatshmp->autofocus = 0;
    }

    /* done when we reach goal */
    int hasReachedGoal;
    if (EXTFOCUS) {
        int result = extFocus_goto(0, rawgoal);
        if (result == -1) {
            // Error
            active_func = NULL;
            fifoWrite(Focus_Id, -1, "Focus offset failed");
            toTTS("Focus offset failed");
            return;
        }
        else if (result == 1) {
            // Slew in progress
            hasReachedGoal = 0;
        }
        else {
            // Slew complete
            hasReachedGoal = 1;
        }
    }
    else {
        hasReachedGoal = (
                (mip->haveenc && abs(mip->raw-rawgoal) < 2
                 && csi_rix(cfd,"=working;")==0 )
                || (!mip->haveenc && mip->raw == rawgoal)
                );
    }

    if (hasReachedGoal)
    {
        active_func = NULL;
        stopFocus(0);
        fifoWrite (Focus_Id, 0, "Focus offset complete");
        toTTS ("The focus motor is in position.");
    }
}

/* handle a joystick jog command */
static void
focus_jog(int first, ...)
{
    MotorInfo *mip = OMOT;
    int cfd = MIPCFD(mip);
    char buf[1024];

    /* maintain current info */
    readFocus();
    mip->dpos = mip->cpos;  /* just for looks */

    if (first)
    {
        va_list ap;
        char dircode;

        /* fetch offset, in microns, canonical direction */
        va_start (ap, first);
        //dircode = va_arg (ap, char);
        dircode = va_arg (ap, int); // char is promoted to int, so pass int...
        va_end (ap);

        /* certainly no auto any more */
        telstatshmp->autofocus = 0;

        /* crack the code */
        switch (dircode)
        {
            case '0':   /* stop */
                focus_stop (1);     /* gentle and reports accurately */
                return;

            case '+':   /* go canonical positive */
                if (mip->cpos >= mip->poslim)
                {
                    fifoWrite (Focus_Id, -4, "At positive limit");
                    return;
                }
                if (EXTFOCUS) {
                    extFocus_jog(1);
                }
                else if (virtual_mode)
                {
                    vmcJog(mip->axis,(long)(mip->sign*MAXVELStp(mip)*OJOGF));
                }
                else
                {
                    csi_w (cfd, "mtvel=%.0f;", mip->sign*MAXVELStp(mip)*OJOGF);
                }
                mip->cvel = mip->maxvel*OJOGF;
                active_func = focus_jog;
                fifoWrite (Focus_Id, 1, "Paddle command in");
                break;

            case '-':   /* go canonical negative */
                if (mip->cpos <= mip->neglim)
                {
                    fifoWrite (Focus_Id, -5, "At negative limit");
                    return;
                }
                if (EXTFOCUS) {
                    extFocus_jog(-1);
                }
                else if (virtual_mode)
                {
                    vmcJog(mip->axis,0 - (long) (mip->sign*MAXVELStp(mip)*OJOGF));
                }
                else
                {
                    csi_w (cfd, "mtvel=%.0f;", -mip->sign*MAXVELStp(mip)*OJOGF);
                }
                mip->cvel = -mip->maxvel*OJOGF;
                active_func = focus_jog;
                fifoWrite (Focus_Id, 2, "Paddle command out");
                break;

            default:
                tdlog ("focus_jog(): bogus dircode: %c 0x%x", dircode, dircode);
                active_func = NULL;
                return;
        }
    }

    /* this is under user control -- about all we can do is watch for lim */
    if (axisLimitCheck (mip, buf) < 0)
    {
        stopFocus(1);
        active_func = NULL;
        fifoWrite (Focus_Id, -7, "%s", buf);
    }

    /* Check up on external focuser, which may need additional monitoring */
    if (EXTFOCUS)
    {
        extFocus_jog(0);
    }
}



static void
initCfg()
{
#define NOCFG   (sizeof(ocfg)/sizeof(ocfg[0]))
#define NHCFG   (sizeof(hcfg)/sizeof(hcfg[0]))

    static int OHAVE, OHASLIM, OAXIS;
    static int OSTEP, OSIGN, OPOSSIDE, OHOMELOW;
    static int OHAVEENC, OESIGN, OESTEP;
    //static int OSHAREDNODE;
    // defined above for direct access by init code
    static double OMAXVEL, OMAXACC, OSLIMACC, OSCALE;

    static CfgEntry ocfg[] =
    {
        {"OAXIS",       CFG_INT, &OAXIS},
        {"OHAVE",       CFG_INT, &OHAVE},
        {"OHASLIM",     CFG_INT, &OHASLIM},
        {"OPOSSIDE",    CFG_INT, &OPOSSIDE},
        {"OHOMELOW",    CFG_INT, &OHOMELOW},
        {"OSTEP",       CFG_INT, &OSTEP},   // if we have an encoder, this is encoder steps
        {"OSIGN",       CFG_INT, &OSIGN},   // if we have an encoder, this is encoder sign
        {"OMAXVEL",     CFG_DBL, &OMAXVEL},
        {"OMAXACC",     CFG_DBL, &OMAXACC},
        {"OSLIMACC",    CFG_DBL, &OSLIMACC},
        {"OSCALE",      CFG_DBL, &OSCALE},
        {"OJOGF",       CFG_DBL, &OJOGF},
    };
    static int maxInterp;
    static CfgEntry ocfg2[] =
    {
        {"MAXINTERP",   CFG_INT,  &maxInterp},
        {"MINAFDT",     CFG_DBL,  &MINAFDT},
    };

    static CfgEntry ocfg3[] =
    {
        {"OHAVEENC",    CFG_INT,  &OHAVEENC},
        {"OUSETEMPFOC", CFG_INT,  &OUSETEMPFOC},
    };

    static CfgEntry ocfg4[] =
    {
        {"OSHAREDNODE", CFG_INT, &OSHAREDNODE},
    };

    static CfgEntry ocfg5[] =
    {
        {"OFLIFOCUS", CFG_INT, &OFLIFOCUS},
        {"ONOFOCUSTEMPDAT", CFG_INT, &ONOFOCUSTEMPDAT},
    };

    static double OPOSLIM, ONEGLIM;

    static CfgEntry hcfg[] =
    {
        {"OPOSLIM",     CFG_DBL, &OPOSLIM},
        {"ONEGLIM",     CFG_DBL, &ONEGLIM},
    };

    // if we have an encoder, read MOTOR step and sign from home.cfg
    static CfgEntry hcfg2[] =
    {
        {"OSTEP",   CFG_INT, &OSTEP},
        {"OSIGN",   CFG_INT, &OSIGN},
    };

    MotorInfo *mip = OMOT;
    int n;
    int oldhomed = mip->ishomed;

    n = readCfgFile (1, ocfn, ocfg, NOCFG);
    if (n != NOCFG)
    {
        cfgFileError (ocfn, n, (CfgPrFp)tdlog, ocfg, NOCFG);
        die();
    }

    n = readCfgFile (1, hcfn, hcfg, NHCFG);
    if (n != NHCFG)
    {
        cfgFileError (hcfn, n, (CfgPrFp)tdlog, hcfg, NHCFG);
        die();
    }

    // read the optional MAXINTERP and MINAFDT keywords
    maxInterp = 0; // zero is not allowed -- use this to test setting/validity
    readCfgFile (1, ocfn, ocfg2, sizeof(ocfg2)/sizeof(ocfg2[0]));
    if (maxInterp) focusPositionSetMaxInterp(maxInterp);

    // read the optional OHAVEENC keyword.
    // If set, then read the sign,step, and home values
    // Also read the autofocus disable option flag here
    OHAVEENC = 0;
    OUSETEMPFOC = 1; // default to true
    OESTEP = 0;
    OESIGN = 1;
    readCfgFile(1, ocfn, ocfg3, 2);
    if (OHAVEENC)
    {
        // if we have an encoder, sign/step refer to the encoder
        OESIGN = OSIGN;
        OESTEP = OSTEP;
        // get the motor sign/step from home.cfg
        // on error, defaults will == encoder value
        // subsequent Find Limits operation will write correct values
        n = readCfgFile(1,hcfn,hcfg2,sizeof(hcfg2)/sizeof(hcfg2[0]));
        if (n != sizeof(hcfg2)/sizeof(hcfg2[0]))
        {
            noOffsetOnHome = 1; // don't try to move to first position  if we don't have steps set yet
        }
    }

    // read the optional OSHAREDNODE keyword, meaning we share this csimc board with the dome control
    // Note that this is incompatible with OHAVEENC
    OSHAREDNODE = 0;
    readCfgFile(1, ocfn, ocfg4, 1);
    if (OSHAREDNODE && OHAVEENC)
    {
        fifoWrite(Focus_Id,-1,"Configuration error -- See Log");
        tdlog("OSHAREDNODE is not compatible with OHAVEENC\n");
        die();
    }

    // read the optional OFLIFOCUS keyword, meaning we use the FLI
    // driver rather than CSI. Also read the optional ONOFOCUSTEMPDAT,
    // which disables the use of FocusTemp.dat and uses values from filter.cfg instead
    OFLIFOCUS = 0;
    ONOFOCUSTEMPDAT = 0;
    readCfgFile(1, ocfn, ocfg5, 2);

    #if USE_FLI
        if (OFLIFOCUS) register_fli_focuser();
    #endif


    memset ((void *)mip, 0, sizeof(*mip));

    mip->axis = OAXIS;
    mip->have = OHAVE;
    mip->haveenc = OHAVEENC;
    mip->enchome = 0;
    mip->estep = OESTEP;
    mip->havelim = OHASLIM;
    mip->posside = OPOSSIDE ? 1 : 0;
    mip->homelow = OHOMELOW ? 1 : 0;
    mip->step = OSTEP;

    if (abs(OSIGN) != 1)
    {
        tdlog ("OSIGN must be +-1\n");
        die();
    }
    mip->sign = OSIGN;

    if (abs(OESIGN) != 1)
    {
        tdlog ("OESIGN must be +-1\n");
        die();
    }
    mip->esign = OESIGN;

    mip->limmarg = 0;
    mip->maxvel = fabs(OMAXVEL);
    mip->maxacc = OMAXACC;
    mip->slimacc = OSLIMACC;
    mip->poslim = OPOSLIM;
    mip->neglim = ONEGLIM;

    mip->focscale = OSCALE;

    // Read in the focus position table
    focusPositionReadData();

    mip->ishomed = oldhomed;

#undef NOCFG
#undef NHCFG
}


static void
stopFocus(int fast)
{
    MotorInfo *mip = OMOT;

    if (EXTFOCUS) {
        extFocus_stop();
    }
    else if (virtual_mode)
    {
        vmcStop(mip->axis);
    }
    else
    {
        csiStop (mip, fast);
    }

    telstatshmp->autofocus = 0;
    OMOT->homing = 0;
    OMOT->limiting = 0;
    OMOT->cvel = 0;

    //STO: 20010523 Focus stop (red light) visual bug due to position mismatch on stop
    OMOT->dpos = OMOT->cpos;

    // STO: 2002-06-28
    // Reset the filter and temperature used for autofocus to force first autofocus to find position
    last_filter = 0;
    last_temp = 0;
    lastAutoFocusOffset = 0;
}

/* read the raw value */
void
readFocus ()
{
    MotorInfo *mip = OMOT;

    if (!mip->have)
        return;

    if (virtual_mode)
    {
        mip->raw = vmc_rix (mip->axis, "=mpos;");
        mip->cpos = (2*PI) * mip->sign * mip->raw / mip->step;
    }
	else if (EXTFOCUS) {
		int success, raw;
		success = extFocus_read_position(&raw);
		if (success) {
			mip->raw = raw;
			mip->cpos = (2*PI) * mip->sign * raw / mip->step;
		}
	}
    else // CSI
    {
        if (mip->haveenc)
        {
            double draw;
            int    raw;

            /* just change by half-step if encoder changed by 1 */
            raw = csi_rix (MIPSFD(mip), "=epos;");
            draw = abs(raw - mip->raw)==1 ? (raw + mip->raw)/2.0 : raw;
            mip->raw = raw;
            mip->cpos = (2*PI) * mip->esign * draw / mip->estep;
        }
        else
        {
            mip->raw = csi_rix (MIPSFD(mip), "=mpos;");
            mip->cpos = (2*PI) * mip->sign * mip->raw / mip->step;
        }
    }
}

/* keep an eye on the focus and insure it tracks scan.filter (if scan.running,
 * else filter) and temperature. as to which temp to use, use highest defined
 * WxStats.auxt else now.n_temp.
 * if trouble, write to Focus_id and turn autofocus back off.
 * N.B. be graceful if not enough temp info and during filter changes.
 */
static void
autoFocus()
{
    MotorInfo *mip = OMOT;
    int cfd = MIPCFD(mip);
    FilterInfo *fip;
    double newtemp;
    double ugoal, goal, baseugoal;
    int rawgoal;
    char newfilter;
    char buf[128];

    static int extFocusCurrentGoal = 0;

    // if we've disabled it, never mind
    if (!OUSETEMPFOC) return;

    /* if under way, just check for success or hard fail */
    if (EXTFOCUS && mip->cvel != 0)
    {
        readFocus();

        // Keep nudging external focuser along
        int status = extFocus_goto(0, extFocusCurrentGoal);
        if (status == 1) { // In progress
            return;
        }
        else if (status == 0) {
            mip->cvel = 0; // Finished! Continue monitoring temperature
        }
        else {
            // Error
            return;
        }
    }
    else if (mip->cvel) // CSI method for checking if slew ended
    {
        readFocus();
        if (fabs(mip->cpos - mip->dpos) <= 2*(2*PI)/mip->step)
            mip->cvel = 0;
        else
            return;
    }

    // make sure we're homed to begin with
    if (axisHomedCheck(mip, buf))
    {
        telstatshmp->autofocus = 0;
        fifoWrite (Focus_Id, -1, "Focus error: %s", buf);
        toTTS ("Focus error: %s", buf);
        return;
    }

    /* find expected filter */
    newfilter = telstatshmp->scan.starttm ? telstatshmp->scan.filter
                : telstatshmp->filter;

    if (!isalnum (newfilter))
        return; /* turning? */
    if (islower (newfilter))
        newfilter = toupper (newfilter);

    /* get focus temp */
    newtemp = focusTemp();

    /* nothing to do if same filter and about same temp again */
    if (newfilter == last_filter &&
        autoFocusOffset == lastAutoFocusOffset &&
        fabs(newtemp-last_temp) <= MINAFDT
        )
    {
        return;
    }

    /* find the entry for this filter */
    fip = findFilter (newfilter);
    if (!fip)
    {
        fifoWrite (Focus_Id, -8, "Autofocus failed: no filter named %c",
                   newfilter);
        telstatshmp->autofocus = 0;
        return;
    }

    /* interpolate temperatures to find new focus position. */

    baseugoal = targetPosition (fip, newtemp);
    ugoal = baseugoal + autoFocusOffset;

    /* file contains canonical microns, we want canonical rads */
    goal = ugoal * (2*PI)*mip->focscale/mip->step;

    /* clamp goals to within limits */
    if (goal > mip->poslim)
    {
        fifoWrite (Focus_Id, -3,
                   "Auto move hits positive limit for %s at %.1fC",
                   fip->name, newtemp);
        goal = mip->poslim;
    }
    if (goal < mip->neglim)
    {
        fifoWrite (Focus_Id, -4,
                   "Auto move hits negative limit for %s at %.1fC",
                   fip->name, newtemp);
        goal = mip->neglim;
    }

    /* go */
    if (EXTFOCUS)
    {
        rawgoal = mip->sign*(int)floor(mip->step*goal/(2*PI) + 0.5);
        extFocus_goto(1, rawgoal);
        extFocusCurrentGoal = rawgoal;
    }
    else if (virtual_mode)
    {
        rawgoal = mip->sign*(int)floor(mip->step*goal/(2*PI) + 0.5);
        vmcSetTargetPosition(mip->axis, rawgoal);
    }
    else
    {
        if (mip->haveenc)
        {
            rawgoal = (int)floor(mip->esign*mip->estep*goal/(2*PI) + 0.5);
            csi_w (cfd, "etpos=%d;", rawgoal);
        }
        else
        {
            rawgoal = (int)floor(mip->sign*mip->step*goal/(2*PI) + 0.5);
            csi_w (cfd, "mtpos=%d;", rawgoal);
        }

    }
    mip->cvel = mip->maxvel * (goal > mip->cpos ? 1 : -1);
    mip->dpos = goal;

    fifoWrite (Focus_Id, 4, "Auto moving to %.1fum (%.1f base + %.1f offset) for %s at %.1fC",
            ugoal,
            baseugoal,
            autoFocusOffset,
            fip->name,
            newtemp);

    /* remember new goals */
    last_temp = newtemp;
    last_filter = newfilter;
    lastAutoFocusOffset = autoFocusOffset;
}

/* given a temperature and a fip, find the interpolated position, in microns,
 * canonical direction.
 */
static double
targetPosition (FilterInfo *fip, double newtemp)
{
    double ugoal;

    tdlog("ONOFOCUSTEMPDAT = %d\n", ONOFOCUSTEMPDAT);
    if (ONOFOCUSTEMPDAT) {
        tdlog("Interpolating temperature %.1f@%.1f - %.1f@%.1f\n", fip->f0, fip->t0, fip->f1, fip->t1);
        // Use old filter.cfg method
        if (fip->t1 != fip->t0)
            ugoal = (newtemp - fip->t0)*(fip->f1 - fip->f0)/(fip->t1 - fip->t0)
                                        + fip->f0;
        else
            ugoal = fip->f0;    // pick one
    }
    else {
        // Use FocusTemp.cfg
        if (focusPositionFind(fip->name[0],newtemp,&ugoal) < 0)
        {
            ugoal = fip->f0;
        }
    }

    return ugoal;

}

/* If the focuser has a built-in temp sensor, read the temperature
 * and populate the first aux temp sensor value. */
static void
refreshExtFocusTemp()
{
    double newtemp = -1;
    WxStats *wxp = &telstatshmp->wxs;

    if (EXTFOCUS) {
        // Check if our external focuser has built in
        // temperature monitoring
        if (extFocus_gettemp_func != NULL && extFocus_gettemp(&newtemp) == 1) {
            wxp->auxt[0] = newtemp;
            wxp->auxtmask = 1;
            wxp->updtime = time(NULL);
            //tdlog("Focuser temperature = %f\n", newtemp);
        }
    }
}

/* get the temp to use to set focus.
 * first aux sensor takes priority over ambient
 */
static double
focusTemp()
{
    double newtemp = telstatshmp->now.n_temp;
    WxStats *wxp = &telstatshmp->wxs;
    int i;

    for (i = MAUXTP; --i >= 0; )
    {
        if (wxp->auxtmask & (1<<i))
        {
            newtemp = wxp->auxt[i];
            break;
        }
    }

    return (newtemp);
}

/* For RCS Only -- Do Not Edit */
static char *rcsid[2] = {(char *)rcsid, "@(#) $RCSfile: focus.c,v $ $Date: 2007/06/09 10:08:50 $ $Revision: 1.15 $ $Name:  $"};

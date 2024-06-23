/* This process listens to several FIFO pairs for generic telescope, dome,
 * filter and focus commands and manipulates CSIMCs accordingly.
 * The fifo names all end with ".in" and ".out" which are with respect to us,
 * the server. Several config files establish several options and parameters.
 *
 * All commands and responses are ASCII strings. All commands will get at
 * least one response. Responses are in the form of a number, a space, and
 * a short description. Numbers <0 indicate fatal errors; 0 means the command
 * is complete; numbers >0 are intermediate progress messages. When a response
 * number <= 0 is returned, there will be no more. It is up to the clients to
 * wait for a completion response; if they just send another command the
 * previous command and its responses will be dropped forever.
 *
 * FIFO pairs:
 *   Tel    telescope axes, field rotator
 *   Filter desired filter, first char of name as setup in filter.cfg.
 *   Focus  desired focus motion, microns as per focus.cfg
 *   Dome   dome and shutter controls
 *   Power  for powerfail/powerok messages
 *
 * v0.1 10/28/93 First draft: Elwood C. Downey
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <libgen.h>

#include "P_.h"
#include "astro.h"
#include "circum.h"
#include "configfile.h"
#include "strops.h"
#include "telstatshm.h"
#include "running.h"
#include "csiutil.h"
#include "misc.h"
#include "telenv.h"
#include "teled.h"

TelStatShm *telstatshmp; /* shared telescope info */
int virtual_mode;        /* non-zero for virtual mode enabled */

char tscfn[] = "config/telsched.cfg";
char tdcfn[] = "config/telescoped.cfg";
char hcfn[] = "config/home.cfg";
char ocfn[] = "config/focus.cfg";
char icfn[] = "config/filter.cfg";
char dcfn[] = "config/dome.cfg";

static void usage(void);
static void init_all(void);
static void allreset(void);
static void init_shm(void);
static void init_tz(void);
static void on_sig(int fake);
static void main_loop(void);

static char *progname;

// Global values read from config
double STOWALT, STOWAZ;
char STOWFILTER[32] = "";

int main(int ac, char *av[])
{
    char *str;

    progname = basenm(av[0]);

    // make the app dir our cwd, so we can find it easy
    char *tmp = strdup(av[0]);
    char *tmp2 = dirname(tmp);
    chdir(tmp2);
    free(tmp);

    /* crack arguments */
    for (av++; --ac > 0 && *(str = *av) == '-'; av++)
    {
        char c;
        while ((c = *++str) != '\0')
            switch (c)
            {
            case 'h': /* no hardware: legacy syntax */
            case 'v': /* same thing, but mnemonic to new name */
                virtual_mode = 1;
                break;
            default:
                usage();
                break;
            }
    }

    /* now there are ac remaining args starting at av[0] */
    if (ac > 0)
        usage();

    /* only ever one */
    if (lock_running(progname) < 0)
    {
        printf("%s: Already running", progname);
        exit(0);
    }

    // daemonizes the program may want to disable this for debugging
    /*if (daemon(1, 0) == -1)
    {
        printf("failed to daemonize\n");
        exit(1);
    }
    telOELog("telescoped"); */

    /* init all subsystems once */
    init_all();

    /* go */

    main_loop();

    /* should never get here */
    return (1);
}

/* write a log message to syslog with a time stamp.
 */
void tdlog(char *fmt, ...)
{
    // Scan *sp = telstatshmp ? &telstatshmp->scan : NULL; /* maybe not yet */
    // char buf[1024];
    va_list ap;
    // FILE *fp;
    // int l;

    /* set up fp as per-schedule log file if appropriate */
    /*
    if (sp && sp->running)
    {
        sprintf (buf, "%s/%s", logdir, sp->schedfn);
        l = strlen (buf);
        if (l >= 4 && strcasecmp (&buf[l-4], ".sch") == 0)
            strcpy (&buf[l-4], ".log"); // change .sch to .log
        else
            strcat (buf, ".log");       // or just append it
        fp = telfopen (buf, "a+");
    }
    else
        fp = NULL;
    */

    /* start with time stamp */
    // l = sprintf (buf, "%s: ", timestamp(time(NULL)));

    /* format the message */
    va_start(ap, fmt);
    // l += vsprintf (buf+l, fmt, ap);
    daemonLog(fmt, ap);
    va_end(ap);

    /* add \n if not already */
    /*if (l > 0 && buf[l-1] != '\n')
    {
        buf[l++] = '\n';
        buf[l] = '\0';
    }
    */

    /* log to stdout */
    // fputs (buf, stdout);
    // fflush (stdout);

    /* and to fp if possible */
    /*if (fp)
    {
        fputs (buf, fp);
        fclose (fp);
    }
    */
}

/* stop the telescope then exit */
void die()
{
    tdlog("die()!");
    allstop();
    close_fifos();
    unlock_running(progname, 0);
    exit(0);
}

/* tell everybody to stop */
void allstop()
{
    tel_msg("Stop");
    filter_msg("Stop");
    focus_msg("Stop");
    dome_msg("Stop");
    lights_msg("Stop");
}

/* read the config files for variables we use here */
void init_cfg()
{
#define NTSCFG (sizeof(tscfg) / sizeof(tscfg[0]))
    static double LONGITUDE, LATITUDE, TEMPERATURE, PRESSURE, ELEVATION;
    static CfgEntry tscfg[] =
        {
            {"STOWALT", CFG_DBL, &STOWALT},
            {"STOWAZ", CFG_DBL, &STOWAZ},
            {"STOWFILTER", CFG_STR, STOWFILTER, sizeof(STOWFILTER)},
            {"LONGITUDE", CFG_DBL, &LONGITUDE},
            {"LATITUDE", CFG_DBL, &LATITUDE},
            {"TEMPERATURE", CFG_DBL, &TEMPERATURE},
            {"PRESSURE", CFG_DBL, &PRESSURE},
            {"ELEVATION", CFG_DBL, &ELEVATION},
        };

    Now *np = &telstatshmp->now;
    int n;

    n = readCfgFile(1, tscfn, tscfg, NTSCFG);
    if (n != NTSCFG)
    {
        cfgFileError(tscfn, n, (CfgPrFp)tdlog, tscfg, NTSCFG);
        // Don't die...     die();
    }

    /* basic defaults if no GPS or weather station */
    lng = -LONGITUDE;        /* we want rads +E */
    lat = LATITUDE;          /* we want rads +N */
    temp = TEMPERATURE;      /* we want degrees C */
    pressure = PRESSURE;     /* we want mB */
    elev = ELEVATION / ERAD; /* we want earth radii*/

#undef NTSCFG
}

static void
main_loop()
{
    while (1)
        chk_fifos();
}

/* tell everybody to reset */
static void
allreset()
{
    tel_msg("Reset");
    filter_msg("Reset");
    focus_msg("Reset");
    dome_msg("Reset");
    lights_msg("Reset");
    init_cfg(); /* even us */
}

/* print a usage message and exit */
static void
usage()
{
    fprintf(stderr, "%s: [options]\n", progname);
    fprintf(stderr, " -v: (or -h) run in virtual mode w/o actual hardware attached.\n");
    exit(1);
}

/* initialize all the various subsystems.
 * N.B. call this only once.
 */
static void
init_all()
{
    /* connect to the telstatshm segment */
    init_shm();

    /* divine timezone */
    init_tz();

    /* always want local apparent place */
    telstatshmp->now.n_epoch = EOD;

    /* no guiding */
    telstatshmp->jogging_ison = 0;

    /* init csimcd */
    csiInit();

    /* connect the signal handlers */
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    signal(SIGHUP, on_sig);

    /* don't get signal if write to fifo fails */
    signal(SIGPIPE, SIG_IGN);

    /* create the fifos to announce we are fully ready */
    init_fifos();

    /* initialize config files and hardware subsystem  */
    allreset();
}

/* create the telstatshmp shared memory segment */
static void
init_shm()
{
    int len = sizeof(TelStatShm);
    int shmid;
    long addr;
    // int new;

    /* open/create */
    // new = 0;
    shmid = shmget(TELSTATSHMKEY, len, 0664);
    if (shmid < 0)
    {
        if (errno == ENOENT)
            shmid = shmget(TELSTATSHMKEY, len, 0664 | IPC_CREAT);
        if (shmid < 0)
        {
            tdlog("shmget: %s", strerror(errno));
            exit(1);
        }
        // new = 1;
    }

    /* connect */
    addr = (long)shmat(shmid, (void *)0, 0);
    if (addr == -1)
    {
        tdlog("shmat: %s", strerror(errno));
        exit(1);
    }

    /* always zero when we start */
    memset((void *)addr, 0, len);

    /* handy */
    telstatshmp = (TelStatShm *)addr;
}

static void
init_tz()
{
    Now *np = &telstatshmp->now;
    time_t t = time(NULL);
    struct tm *gtmp, *ltmp;
    double gmkt, lmkt;

    gtmp = gmtime(&t);
    gtmp->tm_isdst = 0; /* _should_ always be 0 already */
    gmkt = (double)mktime(gtmp);

    ltmp = localtime(&t);
    ltmp->tm_isdst = 0; /* let mktime() figure out zone */
    lmkt = (double)mktime(ltmp);

    tz = (gmkt - lmkt) / 3600.0;
}

static void
on_sig(int signo)
{
    tdlog("Received signal %d", signo);
    die();
}

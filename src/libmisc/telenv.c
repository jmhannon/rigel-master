/* functions to support the TELHOME env variable, and logging.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>'
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>

#include "telenv.h"
#include "strops.h"

// static char *telhome;
// static char telhome_def[] = "/usr/local/telescope";

// static void getTELHOME(void);

/* just like fopen()
 */

FILE *
telfopen(char *name, char *how)
{
    FILE *fp;

    syslog(LOG_INFO, "telfopen(%s)", name);

    fp = fopen(name, how);
    return fp;

    /*if (!(fp = fopen (name, how)) && name[0] != '/')
    {
        char envname[1024];
        telfixpath (envname, name);
        fp = fopen (envname, how);
    }
    return (fp);*/
}

/* just like fopen()
 */

int telopen(char *name, int flags, ...)
{
    // char envname[1024];
    int ret;

    if (flags & O_CREAT)
    {
        va_list ap;
        int mode;

        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
        ret = open(name, flags, mode);
        /*
        if (ret < 0 && name[0] != '/')
        {
            telfixpath (envname, name);
            ret = open (envname, flags, mode);
        }
        */
    }
    else
    {
        ret = open(name, flags);
        /*
        if (ret < 0 && name[0] != '/')
        {
            telfixpath (envname, name);
            ret = open (envname, flags);
        }
        */
    }

    return (ret);
}

/* convert the old path to the new path, allowing for TELHOME.
 * this is for cases when a pathname is used for other than open, such as
 * opendir, unlink, mknod, etc etc.
 * it is ok for caller to use the same buffer for each.
 */

void telfixpath(char *newp, char *old)
{
    // char tmp[1024];

    strcpy(newp, old);
    /*
     * getTELHOME();
    if (telhome && old[0] != '/')
        (void) sprintf (tmp, "%s/%s", telhome, old);
    else
        (void) strcpy (tmp, old);
    (void) strcpy (newp, tmp);
    */
}

/* use syslog, like a good daemon
 * return 0
 */
int telOELog(const char *appName)
{
    openlog(appName, LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "Program startup");

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
    {
        syslog(LOG_INFO, "Current working dir: %s", cwd);
    }
    else
    {
        syslog(LOG_INFO, "getcwd() error");
    }

    return 0;
}

/* return a pointer to a static string of the form YYYYMMDDHHMMSS
 * based on the given UTC time_t value.
 */
/*
char *
timestamp(time_t t)
{
    static char str[15];
    struct tm *tmp = gmtime (&t);

    if (!tmp)
        sprintf (str, "gmtime failed!");    //  N.B. same length
    else
        sprintf (str, "%04d%02d%02d%02d%02d%02d", tmp->tm_year+1900,
                 tmp->tm_mon+1, tmp->tm_mday, tmp->tm_hour,
                 tmp->tm_min, tmp->tm_sec);

    return (str);
}
*/

/* rather like printf but prepends timestamp().
 * also appends \n if not in result.
 */
void daemonLog(char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsyslog(LOG_INFO, fmt, ap);
    va_end(ap);
}

/*
static void
getTELHOME()
{
    if (telhome)
        return;
    telhome = getenv ("TELHOME");
    if (!telhome)
        telhome = telhome_def;
}
*/

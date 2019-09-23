#define _BSD_SOURCE
#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/statvfs.h>


#include <X11/Xlib.h>

char *tzparis = "Europe/Paris";

static Display *dpy;

char *
smprintf(char *fmt, ...)
{
	va_list fmtargs;
	char *buf = NULL;

	va_start(fmtargs, fmt);
	if (vasprintf(&buf, fmt, fmtargs) == -1){
		fprintf(stderr, "malloc vasprintf\n");
		exit(1);
    }
	va_end(fmtargs);

	return buf;
}

void
settz(char *tzname)
{
	setenv("TZ", tzname, 1);
}

char *
mktimes(char *fmt, char *tzname)
{
	char buf[129];
	time_t tim;
	struct tm *timtm;

	memset(buf, 0, sizeof(buf));
	settz(tzname);
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL) {
		perror("localtime");
		exit(1);
	}

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		exit(1);
	}

	return smprintf(buf);
}

void
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

char *
loadavg(void)
{
	double avgs[3];

	if (getloadavg(avgs, 3) < 0) {
		perror("getloadavg");
		exit(1);
	}

	return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
}


char *
up() {
    struct sysinfo info;
    int h,m = 0;
    sysinfo(&info);
    h = info.uptime/3600;
    m = (info.uptime - h*3600 )/60;
    return smprintf("%dh%dm",h,m);
}


int runevery(time_t *ltime, int sec){
    /* return 1 if sec elapsed since last run
     * else return 0 
    */
    time_t now = time(NULL);
    
    if ( difftime(now, *ltime ) >= sec)
    {
        *ltime = now;
        return(1);
    }
    else 
        return(0);
}

int
main(void)
{
	char *status = NULL;
	char *tmprs = NULL;
	char *avgs = NULL;
    time_t count60 = 0;
    char *uptm = NULL;
    
	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	for (;;sleep(1)) {
	    /* checks every minutes */
	    if ( runevery(&count60, 60) )
        {
            free(tmprs);
            free(uptm);
            tmprs = mktimes("%d/%m/%y %H:%M", tzparis);
            uptm = up();
        }
        /* checks every second */
		avgs = loadavg();

		status = smprintf("%s | Up:%s | %s",
				 avgs, uptm, tmprs);
		setstatus(status);
		free(avgs);
		free(status);
	}

	XCloseDisplay(dpy);

	return 0;
}


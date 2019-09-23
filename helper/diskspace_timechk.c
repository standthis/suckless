#define _BSD_SOURCE
#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
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
readfile(char *base, char *file)
{
	char *path, line[513];
	FILE *fd;

	memset(line, 0, sizeof(line));

	path = smprintf("%s/%s", base, file);
	fd = fopen(path, "r");
	if (fd == NULL)
		return NULL;
	free(path);

	if (fgets(line, sizeof(line)-1, fd) == NULL)
		return NULL;
	fclose(fd);

	return smprintf("%s", line);
}

/*
 * Linux seems to change the filenames after suspend/hibernate
 * according to a random scheme. So just check for both possibilities.
 */
char *
getbattery(char *base)
{
	char *co;
	int descap, remcap;

	descap = -1;
	remcap = -1;

	co = readfile(base, "present");
	if (co == NULL || co[0] != '1') {
		if (co != NULL) free(co);
		return smprintf("?");
	}
	free(co);

	co = readfile(base, "charge_full_design");
	if (co == NULL) {
		co = readfile(base, "energy_full_design");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &descap);
	free(co);

	co = readfile(base, "charge_now");
	if (co == NULL) {
		co = readfile(base, "energy_now");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &remcap);
	free(co);

	if (remcap < 0 || descap < 0)
		return smprintf("invalid");

	return smprintf("%.0f", ((float)remcap / (float)descap) * 100);
}

int
parse_netdev(unsigned long long int *receivedabs, unsigned long long int *sentabs)
{
	char *buf;
	char *eth0start;
	static int bufsize;
	FILE *devfd;

	buf = (char *) calloc(255, 1);
	bufsize = 255;
	devfd = fopen("/proc/net/dev", "r");

	// ignore the first two lines of the file
	fgets(buf, bufsize, devfd);
	fgets(buf, bufsize, devfd);

	while (fgets(buf, bufsize, devfd)) {
	    if ((eth0start = strstr(buf, "wlan0:")) != NULL) {

		// With thanks to the conky project at http://conky.sourceforge.net/
		sscanf(eth0start + 6, "%llu  %*d     %*d  %*d  %*d  %*d   %*d        %*d       %llu",\
		       receivedabs, sentabs);
		fclose(devfd);
		free(buf);
		return 0;
	    }
	}
    fclose(devfd);
	free(buf);
	return 1;
}

char *
get_netusage()
{
	unsigned long long int oldrec, oldsent, newrec, newsent;
	double downspeed, upspeed;
	char *downspeedstr, *upspeedstr;
	char *retstr;
	int retval;

	downspeedstr = (char *) malloc(15);
	upspeedstr = (char *) malloc(15);
	retstr = (char *) malloc(42);

	retval = parse_netdev(&oldrec, &oldsent);
	if (retval) {
	    fprintf(stdout, "Error when parsing /proc/net/dev file.\n");
	    exit(1);
	}

	sleep(1);
	retval = parse_netdev(&newrec, &newsent);
	if (retval) {
	    fprintf(stdout, "Error when parsing /proc/net/dev file.\n");
	    exit(1);
	}

	downspeed = (newrec - oldrec) / 1024.0;
	if (downspeed > 1024.0) {
	    downspeed /= 1024.0;
	    sprintf(downspeedstr, "%.3f MB/s", downspeed);
	} else {
	    sprintf(downspeedstr, "%.2f KB/s", downspeed);
	}

	upspeed = (newsent - oldsent) / 1024.0;
	if (upspeed > 1024.0) {
	    upspeed /= 1024.0;
	    sprintf(upspeedstr, "%.3f MB/s", upspeed);
	} else {
	    sprintf(upspeedstr, "%.2f KB/s", upspeed);
	}
	sprintf(retstr, "D: %s U: %s", downspeedstr, upspeedstr);

	free(downspeedstr);
	free(upspeedstr);
	return retstr;
}

char *get_nmail(char *directory, char *label)
{
    /* directory : Maildir path 
    * return label : number_of_new_mails
    */
    
    int n = 0;
    DIR* dir = NULL;
    struct dirent* rf = NULL;

    dir = opendir(directory); /* try to open directory */
    if (dir == NULL)
        perror("");

    while ((rf = readdir(dir)) != NULL) /*count number of file*/
    {
        if (strcmp(rf->d_name, ".") != 0 &&
            strcmp(rf->d_name, "..") != 0)
            n++;
    }
    closedir(dir);

    if (n == 0) 
       return smprintf("");
    else 
       return smprintf("%s%d",label, n);

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

char *get_freespace(char *mntpt){
    struct statvfs data;
    double total, used = 0;

    if ( (statvfs(mntpt, &data)) < 0){
		fprintf(stderr, "can't get info on disk.\n");
		return("?");
    }
    total = (data.f_blocks * data.f_frsize);
    used = (data.f_blocks - data.f_bfree) * data.f_frsize ;
    return(smprintf("%.0f", (used/total*100)));
}

int
main(void)
{
	char *status = NULL;
	char *avgs = NULL;
	char *tmprs = NULL;
    char *bat = NULL;
    char *netstats = NULL;
    char *mail_laposte = NULL;
    char *mail_fac = NULL;
    char *mail_lavabit = NULL;
    char *mail_tl = NULL;
    char *rootfs = NULL;
    char *homefs = NULL;
    time_t count5min = 0;
    time_t count60 = 0;
    
	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	for (;;sleep(1)) {
	    /* checks every minutes */
	    if ( runevery(&count60, 60) )
        {
            free(tmprs);
            free(bat);
            free(rootfs);
            free(homefs);
            tmprs = mktimes("%d/%m/%y %H:%M", tzparis);
            bat = getbattery("/sys/class/power_supply/BAT0/");
            homefs = get_freespace("/home");
            rootfs = get_freespace("/");
        }
        /* checks mail every 5 minutes */
        if (runevery(&count5min, 300) )
        {
            free(mail_laposte);
            free(mail_fac);
            free(mail_lavabit);
            free(mail_tl);
            mail_laposte = get_nmail("/home/xavier/Maildir/fac/new", " Fac:");
            mail_fac = get_nmail("/home/xavier/Maildir/lavabit/new", " Lavabit:");
            mail_lavabit = get_nmail("/home/xavier/Maildir/toilelibre/new", " TL:");
            mail_tl = get_nmail("/home/xavier/Maildir/laposte/new", " Laposte:");
        }
        /* checks every second */
		avgs = loadavg();
        netstats = get_netusage();

		status = smprintf("%s%s%s%s | %s | /:%s% /home:%s% | B:%s% | %s | %s",
				 mail_tl, mail_fac, mail_lavabit, mail_laposte,
				 netstats, rootfs, homefs, bat, avgs, tmprs);
		setstatus(status);
		free(avgs);
        free(netstats);
		free(status);
	}

	XCloseDisplay(dpy);

	return 0;
}


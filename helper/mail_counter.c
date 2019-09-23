//  Fichier: dwmstatus.c                   
//  Crée le 10 déc. 2012 22:28:11
//  Dernière modification: 21 déc. 2012 16:25:16

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
#include <sys/wait.h>
#include <dirent.h>

#include <X11/Xlib.h>

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
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
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

int
main(void)
{
	char *status;
    char *newmails;
    
	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	for (;;sleep(60)) {
        newmails = get_nmail("/home/xavier/Maildir/laposte/new", "Mails:");


		status = smprintf("%s",newmails);
		setstatus(status);
		free(newmails);
		free(status);
	}

	XCloseDisplay(dpy);

	return 0;
}


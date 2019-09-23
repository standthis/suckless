#define _BSD_SOURCE
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

#include <mpd/client.h>
#include <mpd/tag.h>

#include <X11/Xlib.h>

#define MPDHOST "localhost"
#define MPDPORT 6600

char *tzbuc = "Europe/Bucharest";
char *iface = "eth0";

static Display *dpy;

char *
smprintf(char *fmt, ...)
{
    va_list fmtargs;
    char *ret;
    int len;

    va_start(fmtargs, fmt);
    len = vsnprintf(NULL, 0, fmt, fmtargs);
    va_end(fmtargs);

    ret = malloc(++len);
    if (ret == NULL) {
        perror("malloc");
        exit(1);
    }

    va_start(fmtargs, fmt);
    vsnprintf(ret, len, fmt, fmtargs);
    va_end(fmtargs);

    return ret;
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

    bzero(buf, sizeof(buf));
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

    return smprintf("%s", buf);
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
getbattery(char *base)
{
    //TODO: return NULL if plugged in
    char *path, line[513];
    FILE *fd;
    int descap, remcap;

    descap = -1;
    remcap = -1;

    path = smprintf("%s/info", base);
    fd = fopen(path, "r");
    if (fd == NULL) {
        free(path);
        /*perror("fopen");*/
        /*exit(1);*/
        return NULL;
    }
    free(path);
    while (!feof(fd)) {
        if (fgets(line, sizeof(line)-1, fd) == NULL)
            break;

        if (!strncmp(line, "present", 7)) {
            if (strstr(line, " no")) {
                descap = 1;
                break;
            }
        }
        if (!strncmp(line, "design capacity", 15)) {
            if (sscanf(line+16, "%*[ ]%d%*[^\n]", &descap))
                break;
        }
    }
    fclose(fd);

    path = smprintf("%s/state", base);
    fd = fopen(path, "r");
    if (fd == NULL) {
        free(path);
        perror("fopen");
        exit(1);
    }
    free(path);
    while (!feof(fd)) {
        if (fgets(line, sizeof(line)-1, fd) == NULL)
            break;

        if (!strncmp(line, "present", 7)) {
            if (strstr(line, " no")) {
                remcap = 1;
                break;
            }
        }
        if (!strncmp(line, "remaining capacity", 18)) {
            if (sscanf(line+19, "%*[ ]%d%*[^\n]", &remcap))
                break;
        }
    }
    fclose(fd);

    if (remcap < 0 || descap < 0)
        return NULL;

    return smprintf("%.0f", ((float)remcap / (float)descap) * 100);
}

/**
 * Get the current RAM usage as a percentage
 *
 * @return float a number representing what proportion of the RAM is in use
 * eg: 42.3 meaning that 42.3% of the RAM is used
 */
float getram(){
    int total, free, buffers, cached;
    FILE *f;

    f = fopen("/proc/meminfo", "r");

    if(f == NULL){
        perror("fopen");
        exit(1);
    }

    fscanf(f, "%*s %d %*s" // mem total
              "%*s %d %*s" // mem free
              "%*s %*d %*s" // mem available
              "%*s %d %*s" // buffers
              "%*s %d", //cached
              &total, &free, &buffers, &cached);
    fclose(f);

    return (float)(total-free-buffers-cached)/total * 100;
}

struct cpuusage{
    long int total, used;
};

struct cpuusage getcpu(){
    long int user, nice, system, idle, iowait, irq, softirq;
    struct cpuusage usage;

    FILE *f;
    f = fopen("/proc/stat", "r");

    if(f == NULL){
        perror("fopen");
        exit(1);
    }

    fscanf(f, "%*s %ld %ld %ld %ld %ld %ld %ld", &user, &nice, &system, &idle, &iowait, &irq, &softirq);

    usage.used = user + nice + system + irq +softirq;
    usage.total = user + nice + system + idle + iowait + irq +softirq;

    fclose(f);

    return usage;
}

/**
 * Get the current swap usage as a percentage
 *
 * @return float a number representing what proportion of the swap is in use
 * eg: 42.3 meaning that 42.3% of the swap is used
 */
float getswap(){
    char line[513];
    int total = -1, free = -1;
    FILE *f;

    f = fopen("/proc/meminfo", "r");

    if(f == NULL){
        perror("fopen");
        exit(1);
    }

    while(!feof(f) && fgets(line, sizeof(line)-1, f) != NULL
            && (total == -1 || free == -1)){
        if(strstr(line, "SwapTotal")){
            sscanf(line, "%*s %d", &total);
        }

        if(strstr(line, "SwapFree")){
            sscanf(line, "%*s %d", &free);
        }
    }

    fclose(f);

    return (float)(total-free)/total * 100;
}


/**
 * Replace a string with a formatted one
 *
 * @param char **str the string to be replaced
 * @param char *fmt the format of the replacement string
 * @param ... a list of variables to be formatted
 *
 * @return int the number characters in the final string or a negative if errors
 * occured (in which case str is unchanged)
 */
int srprintf(char **str, char *fmt, ...){
    va_list fmtargs;
    char *replacement;
    int len;

    va_start(fmtargs, fmt);
    len = vsnprintf(NULL, 0, fmt, fmtargs);
    va_end(fmtargs);

    // tried realloc here, but since it can resize the memory block whithout
    // changing the location the original contents of the str could be lost if
    // str itself was sent as an argument to be formatted (ie: in fmtargs)
    replacement = (char *) malloc(++len);

    if(replacement == NULL){
        perror("malloc");
        return -1;
    }

    va_start(fmtargs, fmt);
    vsnprintf(replacement, len, fmt, fmtargs);
    va_end(fmtargs);

    free(*str);
    *str = replacement;

    return len;
}

int remove_ext(char *str){
    char *dot = strrchr(str, '.');

    if(dot){
        *dot = '\0';
        return dot-str+1;
    }

    return -1;
}

char *get_filename(const char *str){
    char *dir_sep = strrchr(str, '/');

    if(dir_sep == NULL){
        return (char *)str;
    }

    return dir_sep+1;
}

char *
getmpd() {
    struct mpd_connection *conn;
    struct mpd_song *song;
    struct mpd_status *status;
    const char *artist;
    const char *title;
    const char *name;
    char *retval;

    conn = mpd_connection_new(MPDHOST,MPDPORT, 1000);

    if(mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS){
        fprintf(stderr, "%s", mpd_connection_get_error_message(conn));
        mpd_connection_free(conn);

        return NULL;
    }

    status = mpd_run_status(conn);

    if(status == NULL){
        fprintf(stderr, "Cannot get mpd status: %s\n", mpd_status_get_error(status));

        mpd_status_free(status);
        mpd_connection_free(conn);
        return NULL;
    }

    enum mpd_state state = mpd_status_get_state(status);

    if(state == MPD_STATE_STOP || state == MPD_STATE_UNKNOWN){
        mpd_status_free(status);
        mpd_connection_free(conn);

        return NULL;
    }
    else if(state == MPD_STATE_PAUSE){
        mpd_status_free(status);
        mpd_connection_free(conn);
        return smprintf("paused");
    }

    song = mpd_run_current_song(conn);

    if(song == NULL){
        fprintf(stderr, "Error fetching current song!\n");
        mpd_song_free(song);
        mpd_status_free(status);
        mpd_connection_free(conn);

        return NULL;
    }

    artist = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
    title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
    name = mpd_song_get_tag(song, MPD_TAG_NAME, 0);

    mpd_status_free(status);
    mpd_connection_free(conn);

    if(title){
        if(artist){
            retval = smprintf("%s - %s", artist, title);
        }
        else{
            retval = smprintf("%s", title);
        }

        mpd_song_free(song);
        return retval;
    }

    if(name){
        retval = smprintf("%s", name);

        mpd_song_free(song);
        return retval;
    }

    const char *uri = mpd_song_get_uri(song);

    remove_ext((char *)uri);
    uri = get_filename(uri);

    retval = smprintf("%s", uri);

    mpd_song_free(song);
    return retval;
}

struct netusage{
    long int in, out;
};

struct netusage getnet(const char *iface){
    FILE *f;
    char line[513];
    struct netusage usage;

    f = fopen("/proc/net/dev", "r");

    if(f == NULL){
        perror("fopen");
        exit(1);
    }

    while(!feof(f) && fgets(line, sizeof(line)-1, f) != NULL ){
        if(strstr(line, iface)){
            sscanf(line, "%*s %ld %*d %*d %*d %*d %*d %*d %*d %ld",
                    &usage.in, &usage.out);
        }
    }

    fclose(f);

    return usage;
}

int
main(void)
{
    //TODO: what happens with avgs, bat, etc if I exit at an exit(1) aka: FREE
    //THEM!
    //TODO: weather stats, computer temperature, battery, check: https://code.google.com/p/dwm-hacks/
    //
    /**
     * I'm planning on a big design change which will make the parts of the
     * program independent and asynchronous.
     *
     * First, main() will update the status at a given interval.
     * Secondly, each part will be able to request an update whenever an event
     * happens, for example the weather part can minutely check the change of
     * weather and when the weather changes it can request a status update even
     * though the main program is not scheduled to run one soon.
     *
     * These program "parts" will run in their own thread and will set a
     * variable that can be read from the main program, so when a status update
     * is due that variable is read by the updating function, the same happens
     * for unscheduled updates, whenever there is an update from a program part,
     * the variable is updated and then the updating function is called. This
     * way even if other parts don't have an update the old one can be read from
     * the variable.
     * This variable will be read by a function which acts as an interface.
     *
     * Also the program parts will be initialized at dwmstatus startup, this is
     * when all the threads are started.
     *
     * Long term: allow the user to script this using python, the user will
     * write just the core of one module in python, for example is easier to
     * write a weather script in python and run it from a module of dwmstatus
     * instead of writing the processing part in C.
     */
    char *status;
    char *bat;
    char *tmbuc;
    char *mpd;

    float swap;

    if (!(dpy = XOpenDisplay(NULL))) {
        fprintf(stderr, "dwmstatus: cannot open display.\n");
        return 1;
    }

    struct cpuusage cpu_i_usage = getcpu();
    struct cpuusage cpu_f_usage;

    double cpu_used, cpu_total;

    struct netusage net_i_usage = getnet(iface);
    struct netusage net_f_usage;

    float net_in, net_out;

    char *unit_in = "kb", *unit_out = "kb";

    for (;;sleep(1)) {
        cpu_f_usage = getcpu();
        net_f_usage = getnet(iface);
        bat = getbattery("/proc/acpi/battery/BAT0");
        tmbuc = mktimes("%d-%m-%Y %R", tzbuc);
        mpd = getmpd();
        swap = getswap();

        net_in = (float)(net_f_usage.in - net_i_usage.in)/1024; //kilobytes
        net_out = (float)(net_f_usage.out - net_i_usage.out)/1024; //kilobytes
        unit_in = "kb";
        unit_out = "kb";

        if(net_in > 1024){
            net_in /= 1024; // megabytes
            unit_in = "mb";
        }

        if(net_out > 1024){
            net_out /= 1024; // megabytes
            unit_out = "mb";
        }

        status = smprintf("[");

        if(mpd != NULL){
            srprintf(&status, "%s%s • ", status, mpd);
        }

        cpu_used = cpu_f_usage.used - cpu_i_usage.used;
        cpu_total = cpu_f_usage.total - cpu_i_usage.total;

        srprintf(&status, "%sram: %0.f%% • cpu: %.0f%% • down: %.0f %s/s • up: %.0f %s/s",
                status, getram(), cpu_used/cpu_total*100, net_in, unit_in, net_out, unit_out);

        if(swap >= 1){
            srprintf(&status, "%s • swap: %.0f%%", status, swap);
        }

        if(bat != NULL){
            srprintf(&status, "%s • bat: %s%%", status, bat);
        }

        srprintf(&status, "%s • %s]", status, tmbuc);

        setstatus(status);

        free(bat);
        free(tmbuc);
        free(mpd);
        free(status);

        net_i_usage = net_f_usage;
        cpu_i_usage = cpu_f_usage;
    }

    XCloseDisplay(dpy);

    return 0;
}

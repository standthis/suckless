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
#include <X11/Xlib.h>

#include <alsa/asoundlib.h>

char *tz= "Africa/Johannesburg";

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

	settz(tzname);
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL)
		return smprintf("");

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		return smprintf("");
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

	if (getloadavg(avgs, 3) < 0)
		return smprintf("");

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
	free(path);
	if (fd == NULL)
		return NULL;

	if (fgets(line, sizeof(line)-1, fd) == NULL)
		return NULL;
	fclose(fd);

	return smprintf("%s", line);
}

char *
getbattery(char *base)
{
	char *co, status;
	int descap, remcap;

	descap = -1;
	remcap = -1;

	co = readfile(base, "present");
	if (co == NULL)
		return smprintf("");
	if (co[0] != '1') {
		free(co);
		return smprintf("not present");
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

	co = readfile(base, "status");
	if (!strncmp(co, "Discharging", 11)) {
		status = '-';
	} else if(!strncmp(co, "Charging", 8)) {
		status = '+';
	} else {
		status = '=';
	}

	if (remcap < 0 || descap < 0)
		return smprintf("invalid");

	return smprintf("%.0f%%%c", ((float)remcap / (float)descap) * 100, status);
}

char *
gettemperature(char *base, char *sensor)
{
	char *co;

	co = readfile(base, sensor);
	if (co == NULL)
		return smprintf("");
	return smprintf("%02.0f°C", atof(co) / 1000);
}

/**
 * Get the current RAM usage as a percentage
 *
 * @return float a number representing what proportion of the RAM is in use
 * eg: 42.3 meaning that 42.3% of the RAM is used
 */
float 
getram(){
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

int
get_vol_perc(void)
{
	snd_mixer_t* handle;
	snd_mixer_elem_t* elem;
	snd_mixer_selem_id_t* sid;
	static const char* mix_name = "Master";
	static const char* card = "default";
	static int mix_index = 0;
	snd_mixer_selem_id_alloca(&sid);
	snd_mixer_selem_id_set_index(sid, mix_index);
	snd_mixer_selem_id_set_name(sid, mix_name);
	if ((snd_mixer_open(&handle, 0)) < 0) {
		return -1;
	}
	if ((snd_mixer_attach(handle, card)) < 0) {
		snd_mixer_close(handle);
		return -2;
	}
	if ((snd_mixer_selem_register(handle, NULL, NULL)) < 0) {
		snd_mixer_close(handle);
		return -3;
	}
	int ret = snd_mixer_load(handle);
	if (ret < 0) {
		snd_mixer_close(handle);
		return -4;
	}
	elem = snd_mixer_find_selem(handle, sid);
	if (!elem) {
		snd_mixer_close(handle);
	return -5;
	}

	long minv, maxv, outvol;

	snd_mixer_selem_get_playback_volume_range (elem, &minv, &maxv);

	if(snd_mixer_selem_get_playback_volume(elem, 0, &outvol) < 0) {
		snd_mixer_close(handle);
		return -6;
	}

	/* make the value bound to 100 */
	outvol -= minv;
	maxv -= minv;
	minv = 0;
	outvol = 100 * (outvol) / maxv; // make the value bound from 0 to 100

	snd_mixer_close(handle);
	return outvol;
}

int
main(void)
{
	char *status;
	char *avgs;
	char *bat;
	char *tmmsc;
	char *t0, *t1, *t2;
	int vol;
        float ram;
        
        //struct cpuusage cpu_i_usage = getcpu();
        //struct cpuusage cpu_f_usage;
        //double cpu_used, cpu_total;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	for (;;sleep(60)) {
                //cpu_f_usage = getcpu();
		avgs = loadavg();
		bat = getbattery("/sys/class/power_supply/BAT0");
		//tmmsc = mktimes("%a %d %b %H:%M %Z %Y", tzmoscow);
		tmmsc = mktimes("%U %a %m.%d %H:%M", tz);
		t0 = gettemperature("/sys/devices/platform/coretemp.0/hwmon/hwmon3/", "temp1_input");
		t1 = gettemperature("/sys/devices/platform/thinkpad_hwmon/hwmon/hwmon2/", "temp1_input");
		t2 = gettemperature("/sys/devices/virtual/thermal/thermal_zone0/hwmon0/", "temp1_input");
		vol = get_vol_perc();
                ram = getram();
		//vol = get_vol();

                //cpu_used = cpu_f_usage.used - cpu_i_usage.used;
                //cpu_total = cpu_f_usage.total - cpu_i_usage.total;
		status = smprintf(" %s %s %s | %s | M %0.f%% | %d | %s | %s",
				t0, t1, t2, avgs, ram, vol, bat, tmmsc);
		setstatus(status);

		free(t0);
		free(t1);
		free(t2);
		free(avgs);
		free(bat);
		free(tmmsc);
		free(status);
	}
	XCloseDisplay(dpy);
	return 0;
}


/* 
 * send anything in dwm status bar with fifo
 * example : echo "hello" >> /tmp/dwm.fifo
 * Author:  Xavier Cartron (XC), thuban@yeuxdelibad.net
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define FIFO "/tmp/dwm.fifo"

char *
snotif()
{
    char buf[BUFSIZ];
    int len = 0;

    int f = open(FIFO, O_NONBLOCK | O_RDWR);
    if (f == -1){
        return smprintf("%s","");
    }

    len = read(f, buf, sizeof(buf));
    if (len == -1){
        perror("fifo read");
        close(f);
        return smprintf("%s","");
    }
    close(f);

    buf[len-1] = ' ';

    return smprintf("%s",buf);
}

int
main(void)
{
    int ret = 0;
    ret = mkfifo(FIFO, ACCESSPERMS);
    if (ret == -1) perror("fifo creation");
    
    // your code

	return 0;
}

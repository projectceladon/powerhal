#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <syscall.h>
#include <libgen.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/sysmacros.h>

#include "earlyapp.h"
#include "loglevel.h"

int earlyapp_start()
{
    pid_t ioc_pid;
    int ret;
    const char * ioc_args[] = {
        "/vendor/bin/ioc_slcand",
        NULL
    };

    ioc_pid = fork();
    if(ioc_pid == 0) {
        /* child process */
        if (execv(ioc_args[0], (char**)ioc_args) == -1) {
            mprintf("Lauch ioc_slcand failed\n");
        }

        /* If exec returns, it fails. */
        exit(0);
    } else if (ioc_pid == -1) {
        mprintf("ioc_slcand fork failure\n");
        return -1;
    }

    /* necessary dev for earlyEvs */
    ret = mkdir("/dev/dri", 0755);
    ret = mknod("/dev/dri/card0", S_IFCHR | 0600, makedev(226, 0));
    ret = mknod("/dev/dri/renderD128", S_IFCHR | 0600, makedev(226, 128));
    ret = mknod("/dev/media0", S_IFCHR | 0660, makedev(250, 0));
    ret = mknod("/dev/video32", S_IFCHR | 0660, makedev(81, 32));
    ret = mknod("/dev/v4l-subdev4", S_IFCHR | 0660, makedev(81, 49));
    ret = mknod("/dev/v4l-subdev8", S_IFCHR | 0660, makedev(81, 53));
    ret = mknod("/dev/v4l-subdev13", S_IFCHR | 0660, makedev(81, 58));
    ret = mknod("/dev/v4l-subdev14", S_IFCHR | 0660, makedev(81, 59));

    pid_t evs_pid;
    const char * evs_args[] = {
        "/vendor/bin/earlyEvs",
        "-r",
        NULL
    };

    evs_pid = fork();
    if(evs_pid == 0) {
        /* child process */
        if (execv(evs_args[0], (char**)evs_args) == -1) {
            mprintf("Lauch Evs failed\n");
        }

        /* If exec returns, it must have failed. */
        exit(0);
    } else if (evs_pid == -1) {
        mprintf("EVS fork failure\n");
        return -1;
    }

    return evs_pid;
}

int earlyapp_stop(pid_t early_pid)
{
    int error;
    int status;
    if(early_pid == -1 || early_pid == 0) {
        return 0;
    }
    kill(early_pid, SIGUSR1);
    if (waitpid(early_pid, &status, 0) != early_pid) {
        /* Wait for child to exit */
        mprintf("wait early app exit failure\n");
        return -1;
    }
    return 0;
}

void earlyapp_cleanup()
{
    int ret;
    ret = unlink("/dev/dri/card0");
    ret = unlink("/dev/dri/renderD128");
    ret = rmdir("/dev/dri");
}


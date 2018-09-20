/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>
#include <linux/fs.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "suspend_ioctls.h"
#include "cansend.h"
#include "candump.h"

float t_s(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (float)tv.tv_sec * 1000.0 + (float)tv.tv_usec / 1000.0;
};

float d_s(void) {
    static float t_last;
    float t_now = t_s();
    float d;

    printf("t now = %f\n", t_now);
    d = t_now - t_last;
    t_last = t_now;
    return d;
};

static int log = -1;
static char logbuf[1024];

#define mprintf(format, args...) \
do {\
    sprintf(logbuf,"S4USWSUP: ");\
    sprintf(logbuf+10, format, ##args);\
    printf("%s", logbuf);\
    write(log, logbuf, strlen(logbuf));\
} while(0)

#if 1
static volatile int stop = 0;
static pthread_t tk_thread;
static int cnt = 0;
static volatile unsigned long long tksize = 0;
static volatile unsigned long long tktotal = 0;

char bufpage[4096] __attribute__((aligned(4096)));
char bufxfer[4096] __attribute__((aligned(4096)));

static void* _thread(void* data) {
    while (!stop) {
        mprintf("tk thread: transfer size (%llu:%.2f%%)...\n", tksize, (float)tksize/(float)tktotal*100.0);
        sleep(60);
    };

    return NULL;
};

int start_tk_thread(void) {
    pthread_create(&tk_thread, NULL, _thread, NULL);
    return 0;
};

int mstop_tk_thread(void) {
    stop = 1;
    pthread_join(tk_thread, NULL);
    return 0;
};
#endif

int main(int argc, char **argv)
{

    int dev, img, ret, in_suspend;
    int c;
    unsigned long long image_size;

    mprintf("image saver %s\n", "v0.1");
    mprintf("dummy print, argc = %d, argv = %p\n", argc, argv);

    //TODO: to fix this
    errno = 0;

    log = open("/dev/ttyS2", O_WRONLY);
    mprintf("open serial device, fd = %d, errorno = %s, ts = %f\n", log, strerror(errno), d_s());

    dev = open("/dev/snapshot", O_RDONLY);
    mprintf("snapshot device: fd = %d, errorno = %s, ts = %f\n", dev, strerror(errno), d_s());

    ret = mlockall(MCL_CURRENT | MCL_FUTURE);
    mprintf("action: lock pages. ret = %d, errorno = %s, ts = %f\n", ret, strerror(errno), d_s());

//FIXME: we cannot use file system here, it will always stuck here and there ...
#if 0
        ret = mount("/dev/block/sda1", "/dev/img/", "ext4", 0, NULL);
        mprintf("action: mount image disk, ret = %d, errorno = %s\n", ret, strerror(errno));

        img = open("/dev/img/s4_image.dat", O_CREAT|O_WRONLY, S_IRWXU|S_IRWXG|S_IRWXO);
        mprintf("action: create image file, ret = %d, errorno = %s\n", img, strerror(errno));
#else
        img = open("/dev/block/sda1", O_WRONLY|O_DIRECT);
//        img = open("/data/s4.img", O_CREAT|O_WRONLY|O_DIRECT, S_IRWXU|S_IRWXG|S_IRWXO);
        mprintf("action: mount image disk, ret = %d, errorno = %s\n", img, strerror(errno));
#endif


    mprintf("action: prepare to freeze. ts = %f\n", d_s());
    ret = ioctl(dev, SNAPSHOT_FREEZE);
    mprintf("action: freeze done. ret = %d, errorno = %s, ts = %f\n", ret, strerror(errno), d_s());

    ret = ioctl(dev, SNAPSHOT_PREF_IMAGE_SIZE, 0);
    mprintf("action: set pref image size done. ret = %d, errorno = %s, ts = %f\n", ret, strerror(errno), d_s());

    ret = ioctl(dev, SNAPSHOT_CREATE_IMAGE, &in_suspend);    
    mprintf("action: create image done. in_suspend = %d, ret = %d, errorno = %s, ts = %f\n", in_suspend, ret, strerror(errno), d_s());

    if (in_suspend) {
 
        start_wc_thread();
        start_hb_thread();
        start_tk_thread();
        
        ret = ioctl(dev, SNAPSHOT_GET_IMAGE_SIZE, &image_size);
        mprintf("action: read image size = %llu, ret = %d\n", image_size, ret);
        tktotal = image_size;

#if 1
        sprintf(bufpage, "header = s4 uswsup image, size = %llu\n", image_size);
        ret = write(img, bufpage, 4096);
        mprintf("action: write image header, ret = %d, errorno = %s\n", ret, strerror(errno));
#endif

        int ofst, ofst1;
        ofst = 0;
        ofst1 = 0;
        do {
            //read
            ret = read(dev, &bufxfer[ofst], 4096-ofst);
            if (ret == -1) {
                mprintf("action: read error, errorno = %s\n", strerror(errno));
                break;
            } else if (ret + ofst < 4096) {
                ofst += ret;
                continue;
            };
            tksize += 4096;
            ofst = 0;
            //FIXME: we cannot output to uart to many messages, otherwise the system will hang
            //mprintf("action: read a page (%d), ret = %d, errorno = %s, ts = %f\n", tksize, ret, strerror(errno), d_s());
 
            //write
            ofst1 = 0;
            do {
                ret = write(img, &bufxfer[ofst1], 4096-ofst1);
                if (ret == -1 || ret == 0) {
                    mprintf("action: write error, ret = %d, errorno = %s\n", ret, strerror(errno));
                    break;
                    //TODO: we should break again
                }
                if (ret + ofst1 < 4096) {
                    ofst1 += ret;
                    continue;
                }
                //FIXME: we cannot output to uart to many messages, otherwise the system will hang
                //mprintf("action: write a page (%d), ret = %d, errorno = %s, ts = %f\n", tksize, ret, strerror(errno), d_s());
                break;
            } while (1);

            //quit
            if (tksize >= image_size) {
                mprintf("read done, tksize = %llu, image_size = %llu\n", tksize, image_size);
                break;
            }

        } while (1);

//FIXME: we cannot use filesystem here, it will always stuck here and there
#if 0
        ret = close(img);
        mprintf("action: close image file, errorno = %s, ts = %f\n", strerror(errno), d_s());
        do {
            ret = umount("/dev/img/");
            mprintf("action: umount image file, errorno = %s, ts = %f\n", strerror(errno), d_s());
            sleep(1);
        } while (ret);
#else
        sprintf(bufpage, "tail = s4 uswsup image, size = %llu\n", tksize);
        ret = write(img, bufpage, 4096);
        mprintf("action: write image tail, ret = %d, errorno = %s\n", ret, strerror(errno));
        ret = close(img);
        mprintf("action: close image block device, errorno = %s, ts = %f\n", strerror(errno), d_s());
#endif

        mprintf("CAUTION: now, please use adb pull to pull the file to host, then reboot the system. this app will enter waits, %s", ".");
        close(log);
        while (1) {
            sleep(1);
        }

    } else {

        close(dev);

        mprintf("action: close the snapshot device, errorno = %s, ts = %f\n", strerror(errno), d_s());
        close(log);
    }

    mprintf("CAUTION: now, please use adb pull to pull the file to host, then reboot the system. this app will enter waits, %s", ".");
    c = 0;
    while (1) {
        mprintf("sleep %d\n", c++);
        sleep(1);
    }

    return 0;
}

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

#include <errno.h>
#include <fcntl.h>
#include <linux/watchdog.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEV_NAME "/dev/watchdog"

#if 1
static volatile int stop = 0;
static pthread_t wc_thread;
static int cnt = 0;

static void* _thread(void* data) {
    while (!stop) {
        printf("feed wc (%d)...\n", cnt++);
        watchdogd_main();
    };

    return NULL;
};

int start_wc_thread(void) {
    pthread_create(&wc_thread, NULL, _thread, NULL);
    return 0;
};

int stop_wc_thread(void) {
    stop = 1;
    pthread_join(wc_thread, NULL);
    printf("wc thread stopped\n");
    return 0;
};
#endif

int watchdogd_main(void) {

    int interval = 10;
    int margin = 5;

    int fd = open(DEV_NAME, O_RDWR|O_CLOEXEC);
    if (fd == -1) {
        printf("Failed to open %s, %s\n", DEV_NAME, strerror(errno));
        return 1;
    }

    int timeout = interval + margin;
    int ret = ioctl(fd, WDIOC_SETTIMEOUT, &timeout);
    if (ret) {
        printf("Failed to set timeout to %d\n", timeout);
        ret = ioctl(fd, WDIOC_GETTIMEOUT, &timeout);
        if (ret) {
            printf("Failed to get timeout\n");
        } else {
            if (timeout > margin) {
                interval = timeout - margin;
            } else {
                interval = 1;
            }
            printf("Adjusted interval to timeout returned by driver: \n");
            printf("    timeout %d\n", timeout);
            printf("    interval %d\n", interval);
            printf("    margin %d\n", margin);
        }
    }

    while (1) {
        write(fd, "", 1);
        sleep(interval);
    }
}

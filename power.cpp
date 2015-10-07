/*
 * Copyright (C) 2014 Intel Corporation
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
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <pthread.h>

#include <hardware/power.h>
#include <fcntl.h>

#define LOG_TAG "PowerHAL"
#include <utils/Log.h>

#include <cutils/log.h>
#include <cutils/properties.h>
#include <hardware/hardware.h>
#include "CGroupCpusetController.h"
#include "DevicePowerMonitor.h"
#include <thd_binder_client.h>

#define ENABLE 1
#define TOUCHBOOST_PULSE_SYSFS    "/sys/devices/system/cpu/cpufreq/interactive/touchboostpulse"
#define CPUFREQ_BOOST	"/sys/devices/system/cpu/cpufreq/interactive/boost"

/*
 * This parameter is to identify continuous touch/scroll events.
 * Any two touch hints received between a 20 interval ms is
 * considered as a scroll event.
 */
#define SHORT_TOUCH_TIME 20

/*
 * This parameter is to identify first touch events.
 * Any two touch hints received after 100 ms is considered as
 * a first touch event.
 */
#define LONG_TOUCH_TIME 100

/*
 * This parameter defines the number of vsync boost to be
 * done after the finger release event.
 */
#define VSYNC_BOOST_COUNT 4

/*
 * This parameter defines the time between a touch and a vsync
 * hint. the time if is > 30 ms, we do a vsync boost.
 */
#define VSYNC_TOUCH_TIME 30

#if APP_LAUNCH_BOOST
#define SAFETY_TIMER_THRESHOLD 5000
static pthread_mutex_t mutex;
static pthread_cond_t thread_cond;
static pthread_t thread_id;
static int app_launch_boosted=0;
#endif

using namespace powerhal_api;

static CGroupCpusetController cgroupCpusetController;
static DevicePowerMonitor powerMonitor;
static android::sp<IThermalAPI> shw;
static bool serviceRegistered = false;
static bool interactiveActive = false;

struct intel_power_module{
    struct power_module container;
    int touchboost_disable;
    int timer_set;
    int vsync_boost;
};

static int sysfs_write(char *path, char *s)
{
    char buf[80];
    int len;
    int fd = open(path, O_WRONLY);

    if (fd < 0) {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("Error opening %s: %s\n", path, buf);
        return -1;
    }

    len = write(fd, s, strlen(s));
    if (len < 0) {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("Error writing to %s: %s\n", path, buf);
        return -1;
    }

    close(fd);
    return 0;
}

#if APP_LAUNCH_BOOST

static void * g_start_timer(void *)
{
	struct timeval currentTime;
	struct timespec ts;
	int ret;

	gettimeofday(&currentTime, NULL);
	ts.tv_nsec = (currentTime.tv_usec * 1000) + (SAFETY_TIMER_THRESHOLD % 1000) * 1000000;
	ts.tv_sec = currentTime.tv_sec + (SAFETY_TIMER_THRESHOLD / 1000) + (ts.tv_nsec / 1000000000);
	ts.tv_nsec %= 1000000000;

	pthread_mutex_lock(&mutex);
	ret = pthread_cond_timedwait(&thread_cond, &mutex, &ts);
	if (ret == 0) {
		ALOGI("APP LAUNCH boot reverted normally!");
	} else {
		ALOGI("APP LAUNCH SAFETY TIMER TRIGGERED!");
		sysfs_write(CPUFREQ_BOOST,"0");

	}
	pthread_mutex_unlock(&mutex);

	app_launch_boosted=0;
	pthread_exit(NULL);
}
void start_timer()
{
	int rc = 0;
	int prevType;

	rc = pthread_create(&thread_id, NULL, g_start_timer,NULL);
	if(rc) {
		ALOGE("pthread_create failed!");
		app_launch_boosted=0;
		sysfs_write(CPUFREQ_BOOST,"0");
		return;
	}
	if(!rc)
		ALOGI("pthread is created !! ");
	return;
}

static void app_launch_boost(void *hint_data)
{
	int rc;
	if ((long)hint_data == 1) {
		ALOGI("PowerHAL HAL:App Boost ON");
		sysfs_write(CPUFREQ_BOOST,"1");
		if(!app_launch_boosted)
		{
			app_launch_boosted=1;
			start_timer();
		}
	} else {
		ALOGI("PowerHAL HAL:App Boost OFF");
		sysfs_write(CPUFREQ_BOOST,"0");
		pthread_cond_signal(&thread_cond);
	}

}

static int app_launch_boost_init()
{
	pthread_cond_init(&thread_cond, NULL);
	pthread_mutex_init(&mutex, NULL);
	return 0;
}

#endif

static bool itux_enabled() {
    char value[PROPERTY_VALUE_MAX];
    int length = property_get("persist.thermal.mode", value, "thermald");
    std::string mode(value);

    if (mode == "itux" || mode == "ituxd")
        return true;
    return false;
}

static void power_init(__attribute__((unused))struct power_module *module)
{
    sp<IServiceManager> sm = defaultServiceManager();
    sp<IBinder> binder;
    int cnt = 0;

    /* Enable all devices by default */
    powerMonitor.setState(ENABLE);
    cgroupCpusetController.setState(ENABLE);

    if (!sysfs_write(TOUCHBOOST_PULSE_SYSFS, "1"))
        interactiveActive = true;

    if (itux_enabled()) //we do not need the connection
        return;

    do {
        binder = sm->getService(android::String16(SERVICE_NAME));
        if (binder != 0)
            break;
        ALOGE("thd_binder_service not published, waiting...");
        usleep(500000); // 0.5 s
        if (cnt++ > 10) //do not wait forever
            return;
    } while (true);

    serviceRegistered = true;
    shw = android::interface_cast<IThermalAPI>(binder);

#if APP_LAUNCH_BOOST
	app_launch_boost_init();
#endif
}

static void power_set_interactive(__attribute__((unused))struct power_module *module, int on)
{
    powerMonitor.setState(on);
    cgroupCpusetController.setState(on);
}

static void power_hint_worker(void *hint_data)
{
    int rc;
    struct PowerSaveMessage data = { 1 , 50 };
    status_t status;

    if (!serviceRegistered)
        return;

    if (NULL == hint_data)
        data.on = 0;

    shw->sendPowerSaveMsg(&data);

}

static void power_hint(struct power_module *module, power_hint_t hint,
                       void *data)
{
    struct intel_power_module *intel = (struct intel_power_module *) module;
    static struct timespec curr_time, prev_time = {0,0}, vsync_time;
    double diff;
    static int vsync_count;
    static int consecutive_touch_int;

    switch(hint) {
    case POWER_HINT_INTERACTION:
        if (!interactiveActive)
            return;
        clock_gettime(CLOCK_MONOTONIC, &curr_time);
        diff = (curr_time.tv_sec - prev_time.tv_sec) * 1000 +
               (double)(curr_time.tv_nsec - prev_time.tv_nsec) / 1e6;
        prev_time = curr_time;
        if (diff < SHORT_TOUCH_TIME) {
            consecutive_touch_int++;
        }
        else if (diff > LONG_TOUCH_TIME) {
            intel->vsync_boost = 0;
            intel->timer_set = 0;
            intel->touchboost_disable = 0;
            vsync_count = 0;
            consecutive_touch_int = 0;
        }
        /* Simple touch: timer rate need not be changed here */
        if ((diff < SHORT_TOUCH_TIME) && (intel->touchboost_disable == 0)
                        && (consecutive_touch_int > 4))
            intel->touchboost_disable = 1;
        /*
         * Scrolling: timer rate reduced to increase sensitivity. No more touch
         * boost after this
         */
        if ((intel->touchboost_disable == 1) && (consecutive_touch_int > 15)
                        && (intel->timer_set == 0)) {
           intel->timer_set = 1;
        }
        if (!intel->touchboost_disable) {
            sysfs_write(TOUCHBOOST_PULSE_SYSFS, "1");
        }
        break;
    case POWER_HINT_VSYNC:
        if (!interactiveActive)
            return;
        if (intel->touchboost_disable == 1) {
            clock_gettime(CLOCK_MONOTONIC, &vsync_time);
            diff = (vsync_time.tv_sec - curr_time.tv_sec) * 1000 +
            (double)(vsync_time.tv_nsec - curr_time.tv_nsec) / 1e6;
            if (diff > VSYNC_TOUCH_TIME) {
                intel->timer_set = 0;
                intel->vsync_boost = 1;
                intel->touchboost_disable = 0;
                vsync_count = VSYNC_BOOST_COUNT;
            }
        }
        if (intel->vsync_boost) {
            if (((unsigned long)data != 0) && (vsync_count > 0)) {
                sysfs_write(TOUCHBOOST_PULSE_SYSFS,"1");
                vsync_count-- ;
            if (vsync_count == 0)
               intel->vsync_boost = 0;
            }
        }
        break;
    case POWER_HINT_LOW_POWER:
        power_hint_worker(data);
        break;

#if APP_LAUNCH_BOOST
    case POWER_HINT_APP_LAUNCH:
		app_launch_boost(data);
#endif
    default:
        break;
    }
}

static struct hw_module_methods_t power_module_methods = {
    .open = NULL,
};

struct intel_power_module HAL_MODULE_INFO_SYM = {
    container:{
        .common = {
            .tag = HARDWARE_MODULE_TAG,
            .module_api_version = POWER_MODULE_API_VERSION_0_2,
            .hal_api_version = HARDWARE_HAL_API_VERSION,
            .id = POWER_HARDWARE_MODULE_ID,
            .name = "Intel PC Compatible Power HAL",
            .author = "Intel Open Source Technology Center",
            .methods = &power_module_methods,
            .dso = NULL,
            .reserved = {},
        },
    .init = power_init,
    .setInteractive = power_set_interactive,
    .powerHint = power_hint,
    },
    touchboost_disable: 0,
    timer_set: 0,
    vsync_boost: 0,
};

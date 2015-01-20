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

using namespace powerhal_api;

static CGroupCpusetController cgroupCpusetController;
static DevicePowerMonitor powerMonitor;
static android::sp<IThermalAPI> shw;
static bool serviceRegistered = false;

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

    do {
        binder = sm->getService(android::String16(SERVICE_NAME));
        if (binder != 0)
            break;
        ALOGE("OGI thd_binder_service not published, waiting...");
        usleep(500000); // 0.5 s
        if (cnt++ > 10) //do not wait forever
            return;
    } while (true);
    if (itux_enabled()) //we do not need the connection
        return;
    serviceRegistered = true;
    shw = android::interface_cast<IThermalAPI>(binder);
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

static void power_hint(__attribute__((unused))struct power_module *module, power_hint_t hint,
                       void *data)
{
    switch(hint) {
    case POWER_HINT_LOW_POWER:
        power_hint_worker(data);
    default:
        break;
    }
}

static struct hw_module_methods_t power_module_methods = {
    .open = NULL,
};

struct power_module HAL_MODULE_INFO_SYM = {
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
};

/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "include/hint.h"
#include <fcntl.h>

#define LOG_TAG "PowerHAL"
#include <utils/Log.h>

#include <cutils/log.h>
#include <hardware/hardware.h>
#include "DevicePowerMonitor.h"

#define SOCK_DEV "/dev/socket/power_hal"

static int sockfd;
static struct sockaddr_un client_addr;

static DevicePowerMonitor powerMonitor;

static int socket_init()
{
    if (sockfd < 0) {
        sockfd = socket(PF_UNIX, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            ALOGE("%s: failed to open: %s", __func__, strerror(errno));
            return 1;
        }
        memset(&client_addr, 0, sizeof(struct sockaddr_un));
        client_addr.sun_family = AF_UNIX;
        snprintf(client_addr.sun_path, UNIX_PATH_MAX, SOCK_DEV);
    }
    return 0;
}

static void power_init(__attribute__((unused))struct power_module *module)
{
    /* Enable all devices by default */
    powerMonitor.setState(1);
    sockfd = -1;
    socket_init();
}

static void power_set_interactive(__attribute__((unused))struct power_module *module, int on)
{
    powerMonitor.setState(on);
}

static void power_hint_worker(power_hint_t hint, void *hint_data)
{
    int rc;
    power_hint_data_t data;
    if (socket_init()) {
        ALOGE("socket init failed");
        return;
    }

    data.hint = hint;

    if (NULL == hint_data) {
        data.data = 0;
    } else {
        data.data = 1;
    }

    rc = sendto(sockfd, &data, sizeof(data), 0, (const struct sockaddr *)&client_addr, sizeof(struct sockaddr_un));
    if (rc < 0) {
        ALOGE("%s: failed to send: %s", __func__, strerror(errno));
        return;
    }
}

static void power_hint(__attribute__((unused))struct power_module *module, power_hint_t hint,
                       void *data) {
    switch(hint) {
    case POWER_HINT_LOW_POWER:
        power_hint_worker(POWER_HINT_LOW_POWER, data);
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

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

#define LOG_TAG "I2CDevicePowerMonitor"

#include "I2CDevicePowerMonitor.h"

#include <cutils/log.h>
#include <errno.h>


static const char* I2C_BUS_DIR = "/sys/bus/i2c/devices";
static const char* I2C_DEV_NAME = "name";
static const char* DEVICE_CONTROL_FILE = "enable";

void I2CDevicePowerMonitor::scanPaths()
{

    char deviceNamePath[PATH_MAX];
    DIR *dir;
    struct dirent *de;
    int cnt = 0;

    if(!mScanNeeded)
        return;

    mDevicePaths.erase(mDevicePaths.begin(), mDevicePaths.end());
    dir = opendir(I2C_BUS_DIR);
    if(dir == NULL){
        ALOGE("Could not open directory '%s': %s", I2C_BUS_DIR, strerror(errno));
        return;
    }
    while((de = readdir(dir))) {
        if(de->d_name[0] == '.')
            continue;

        snprintf(deviceNamePath, sizeof(deviceNamePath), "%s/%s/%s", I2C_BUS_DIR, de->d_name, I2C_DEV_NAME);
        int fd = ::open(deviceNamePath, O_RDONLY);
        if(fd < 0){
            ALOGE("Could not open file '%s': %s", deviceNamePath, strerror(errno));
            continue;
        }

        char deviceName[DEVICE_NAME_MAX];
        ssize_t numBytes = read(fd, &deviceName, DEVICE_NAME_MAX);
        if(numBytes < 0){
            ALOGE("Error while reading file '%s': %s", deviceNamePath, strerror(errno));
            close(fd);
            continue;
        }
        close(fd);
        deviceName[numBytes]='\0';

        bool devFound = false;
        unsigned int i;
        for(i = 0;  i < I2CDevicePowerMonitorInfo::numDev; i++){
            if(!strncmp(I2CDevicePowerMonitorInfo::deviceList[i], deviceName,
	       strlen(I2CDevicePowerMonitorInfo::deviceList[i]))){
                ALOGV("Found device: %s", deviceName);
                devFound = true;
                break;
            }
        }

        if(devFound){
            snprintf(deviceNamePath, sizeof(deviceNamePath), "%s/%s/%s", I2C_BUS_DIR, de->d_name, DEVICE_CONTROL_FILE);
            mDevicePaths.push_back(deviceNamePath);
        }
    }
    if(mDevicePaths.size() == I2CDevicePowerMonitorInfo::numDev){
        mScanNeeded = false;
    }

    closedir(dir);
}

void I2CDevicePowerMonitor::setState(int state)
{
    unsigned int quitLoop = 0;
    ssize_t ret = 0;
    scanPaths();
    std::vector<std::string>::iterator it=mDevicePaths.begin();
    while(it != mDevicePaths.end())
    {
        int fd = ::open((char*) it->c_str(), O_WRONLY);
        if(fd < 0){
            ALOGE("Could not open the file:%s", (char*) it->c_str());
            /*
                We might have issue that the kernel removed the node so we need to re-scan.
                However if we have permission problem we do not want to be stuck forever in this loop
            */
            mScanNeeded = true;
            scanPaths();
            it = mDevicePaths.begin();
            if(quitLoop++ > 0)
                break;
            continue;
        }

        if(!state){
            ret = write(fd, "0", 1);
            if (ret < 0)
                ALOGE("Error when trying to write to the file errno:%d", errno);
        }
        else{
            ret = write(fd, "1", 1);
            if (ret < 0)
                ALOGE("Error when trying to write to the file errno:%d", errno);
        }
        close(fd);
        it++;

    }
}

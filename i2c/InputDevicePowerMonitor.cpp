/*
 * Copyright (C) 2013 The Android Open Source Project
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

#include "InputDevicePowerMonitor.h"

#include <cutils/log.h>
#include <errno.h>

#define LOG_TAG "InputDevicePowerMonitor"

static const char* INPUT_DIR = "/sys/class/input";
static const char* INPUT_FILE = "device/name";
static const char* DEVICE_CONTROL_FILE = "device/device/enable";

void InputDevicePowerMonitor::scanPaths()
{

    char eventName[PATH_MAX];
    DIR *dir;
    struct dirent *de;
    int cnt = 0;

    if(!mScanNeeded)
        return;

    mUeventPaths.erase(mUeventPaths.begin(), mUeventPaths.end());
    dir = opendir(INPUT_DIR);
    if(dir == NULL){
        ALOGE("%s: Could not open INPUT_DIR:%s", __func__, INPUT_DIR);
        return;
    }
    while((de = readdir(dir))) {
        if(!strstr(de->d_name,"event"))
            continue;
        snprintf(eventName, PATH_MAX, "%s/%s/%s", INPUT_DIR, de->d_name, INPUT_FILE);
        int fd = ::open(eventName, O_RDONLY);
        if(fd < 0){
            ALOGE("%s: Could not open the file:%s", __func__, eventName);
            continue;
        }
        char deviceName[DEVICE_NAME_MAX];
        ssize_t numBytes = read(fd, &deviceName, DEVICE_NAME_MAX);
        if(numBytes < 0){
            ALOGE("%s: Error while reading the file:%s", __func__, eventName);
            close(fd);
            continue;
        }
        close(fd);
        deviceName[numBytes]='\0';
        bool devFound = false;
        unsigned int i;
        for(i = 0;  i < InputDevicePowerMonitorInfo::numDev; i++){
            if(!strncmp(InputDevicePowerMonitorInfo::deviceList[i], deviceName,
	       strlen(InputDevicePowerMonitorInfo::deviceList[i]))){
                ALOGV("%s: Found the device:%s", __func__, deviceName);
                devFound = true;
                break;
            }
        }
        if(devFound){
            snprintf(eventName, PATH_MAX, "%s/%s/%s", INPUT_DIR, de->d_name, DEVICE_CONTROL_FILE);
                     mUeventPaths.push_back(eventName);
        }
    }
    if(mUeventPaths.size() == InputDevicePowerMonitorInfo::numDev){
        mScanNeeded = false;
    }

    closedir(dir);
}

void InputDevicePowerMonitor::setState(int state)
{
    unsigned int quitLoop = 0;
    ssize_t ret = 0;
    scanPaths();
    std::vector<std::string>::iterator it=mUeventPaths.begin();
    while(it != mUeventPaths.end())
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
            it = mUeventPaths.begin();
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

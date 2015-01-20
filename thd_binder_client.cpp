/*
 * thd_binder_client.h: implementation of thermal daemon binder client
 *
 * Copyright (C) 2014 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 or later as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * Author Name <radivoje.jovanovic@intel.com>
 *
 */
#include <thd_binder_client.h>

using namespace powerhal_api;

IMPLEMENT_META_INTERFACE(ThermalAPI, META_INTERFACE_NAME);


BpThermalAPI::BpThermalAPI(const sp<IBinder>& impl)
    : BpInterface<IThermalAPI>(impl) {
}

status_t BpThermalAPI::sendPowerSaveMsg(struct PowerSaveMessage *tmsg) {
    Parcel data, reply;
    data.writeInterfaceToken(IThermalAPI::getInterfaceDescriptor());
    data.write((const void*)tmsg, sizeof(*tmsg));
    remote()->transact(SEND_POWER_SAVE_MSG, data, &reply);
    return reply.readInt32();
}

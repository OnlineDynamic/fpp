#pragma once
/*
 * This file is part of the Falcon Player (FPP) and is Copyright (C)
 * 2013-2022 by the Falcon Player Developers.
 *
 * The Falcon Player (FPP) is free software, and is covered under
 * multiple Open Source licenses.  Please see the included 'LICENSES'
 * file for descriptions of what files are covered by each license.
 *
 * This source file is covered under the LGPL v2.1 as described in the
 * included LICENSE.LGPL file.
 */

#include "ChannelOutputSetup.h"

/* Config routine exposed so we can re-load the config on demand */
void SendFPDConfig();

/* Create a pixelnetDMX config file */
void CreatePixelnetDMXfile(const char* file);

typedef struct fppChannelOutput {
    int (*maxChannels)(void* data);
    int (*open)(const char* device, void** privDataPtr);
    int (*close)(void* data);
    int (*isConfigured)(void);
    int (*isActive)(void* data);
    int (*send)(void* data, const char* channelData, int channelCount);
    int (*startThread)(void* data);
    int (*stopThread)(void* data);
} FPPChannelOutput;

/* Expose our interface */
extern FPPChannelOutput FPDOutput;

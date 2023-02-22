/*
 * This file is part of the Falcon Player (FPP) and is Copyright (C)
 * 2013-2023 by the Falcon Player Developers.
 *
 * The Falcon Player (FPP) is free software, and is covered under
 * multiple Open Source licenses.  Please see the included 'LICENSES'
 * file for descriptions of what files are covered by each license.
 *
 * This source file is covered under the LGPL v2.1 as described in the
 * included LICENSE.LGPL file.
 */

#include "fpp-pch.h"

#include <curl/curl.h>

#include "OLEDCommands.h"

OLEDMenuCommand::OLEDMenuCommand() :
    Command("Stuart Test OLED Navigation", "Sends actions to the OLED Menu") {
    args.push_back(CommandArg("action", "string", "Action").setContentListUrl("api/oled/action/options"));
}
std::unique_ptr<Command::Result> OLEDMenuCommand::run(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return std::make_unique<Command::ErrorResult>("Not found");
    }

    //  int v = std::atoi(args[0].c_str());
    // setVolume(v);
    // Need code in here to trigger action in the fppoled service

    LogInfo(VB_COMMAND, "Stuarts OLED Button Push: \n", args[0].c_str());
    return std::make_unique<Command::Result>("Nav Menu Action");
}

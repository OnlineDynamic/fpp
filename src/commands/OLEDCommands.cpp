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
    Command("OLED Navigation", "Sends actions to the OLED Menu") {
    args.push_back(CommandArg("action", "string", "Button Action").setContentListUrl("api/oled/action/options"));
    args.push_back(CommandArg("readMethod", "string", "GPIO Read Mode").setDefaultValue("Interrupt").setContentList({ "Poll", "Interrupt" }));
}
std::unique_ptr<Command::Result> OLEDMenuCommand::run(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return std::make_unique<Command::ErrorResult>("Not found");
    }

    LogInfo(VB_COMMAND, "Stuarts OLED Button Push: ", args[0].c_str());
    // Need code in here to trigger action in the fppoled service

    return std::make_unique<Command::Result>("Nav Menu Action");
}

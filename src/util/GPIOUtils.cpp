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

#include "fpp-pch.h"

#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <thread>

#include "GPIOUtils.h"
#include "../Warnings.h"
#include "commands/Commands.h"

// No platform information on how to control pins
static NoPinCapabilities NULL_PIN_INSTANCE("-none-", 0);

const NoPinCapabilities& NoPinCapabilitiesProvider::getPinByName(const std::string& name) { return NULL_PIN_INSTANCE; }
const NoPinCapabilities& NoPinCapabilitiesProvider::getPinByGPIO(int chip, int gpio) { return NULL_PIN_INSTANCE; }
const NoPinCapabilities& NoPinCapabilitiesProvider::getPinByUART(const std::string& n) { return NULL_PIN_INSTANCE; }

Json::Value PinCapabilities::toJSON() const {
    Json::Value ret;
    if (name != "" && name != "-non-") {
        ret["pin"] = name;
        ret["gpioChip"] = gpioIdx;
        ret["gpioLine"] = gpio;
        if (gpioIdx == 0) {
            //somewhat for compatibility with the old kernel GPIO numbers on the Pi's
            ret["gpio"] = gpio;
        }
        if (pwm != -1) {
            ret["pwm"] = pwm;
            ret["subPwm"] = subPwm;
        }
        if (i2cBus != -1) {
            ret["i2c"] = i2cBus;
        }
        if (uart != "") {
            ret["uart"] = uart;
        }
        ret["supportsPullUp"] = supportsPullUp();
        ret["supportsPullDown"] = supportsPullDown();
    }
    return ret;
}

void PinCapabilities::enableOledScreen(int i2cBus, bool enable) {
    // this pin is i2c, we may need to tell fppoled to turn off the display
    // before we shutdown this pin because once we re-configure, i2c will
    // be unavailable and the display won't update
    int smfd = shm_open("fppoled", O_CREAT | O_RDWR, 0);
    ftruncate(smfd, 1024);
    unsigned int* status = (unsigned int*)mmap(0, 1024, PROT_WRITE | PROT_READ, MAP_SHARED, smfd, 0);
    if (i2cBus == status[0]) {
        if (!enable) {
            // force the display off
            status[2] = 1;
            int count = 0;
            while (status[1] != 0 && count < 150) {
                count++;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } else {
            // allow the display to come back on
            status[2] = 0;
        }
    }
    close(smfd);
    munmap(status, 1024);
}

// the built in GPIO chips that are handled by the more optimized
// platform specific GPIO drivers
static const std::set<std::string> PLATFORM_IGNORES{
    "pinctrl-bcm2835", // raspberry pi's
    "pinctrl-bcm2711", // Pi4
    "raspberrypi-exp-gpio",
    "brcmvirt-gpio",
    "gpio-0-31", // beagles
    "gpio-32-63",
    "gpio-64-95",
    "gpio-96-127",
    "gpio-brcmstb@107d508500", // Pi5's internal GPIO chips
    "gpio-brcmstb@107d508520",
    "gpio-brcmstb@107d517c00",
    "gpio-brcmstb@107d517c20",
    "pinctrl-rp1",   // Pi5's external GPIO chip
    "tps65219-gpio", // AM62x
    "4201000.gpio",
    "600000.gpio",
    "601000.gpio"
};
// No platform information on how to control pins
static std::string PROCESS_NAME = "FPPD";

bool GPIODCapabilities::supportsPullUp() const {
    return true;
}
bool GPIODCapabilities::supportsPullDown() const {
    return true;
}

#ifdef HASGPIOD
constexpr int MAX_GPIOD_CHIPS = 32;
class GPIOChipHolder {
public:
    static GPIOChipHolder INSTANCE;

    std::array<gpiod::chip, MAX_GPIOD_CHIPS> chips;
};
GPIOChipHolder GPIOChipHolder::INSTANCE;
#endif
int GPIODCapabilities::configPin(const std::string& mode,
                                 bool directionOut,
                                 const std::string& desc) const {
#ifdef HASGPIOD
    // printf("Configuring %s %d %d %s\n", name.c_str(), gpioIdx, gpio, mode.c_str());
    if (chip == nullptr) {
        if (!GPIOChipHolder::INSTANCE.chips[gpioIdx]) {
            if (gpioName.empty()) {
                std::string n = std::to_string(gpioIdx);
                GPIOChipHolder::INSTANCE.chips[gpioIdx].open(n, gpiod::chip::OPEN_BY_NUMBER);
            } else {
                GPIOChipHolder::INSTANCE.chips[gpioIdx].open(gpioName, gpiod::chip::OPEN_BY_NAME);
            }
        }
        chip = &GPIOChipHolder::INSTANCE.chips[gpioIdx];
        line = chip->get_line(gpio);
    }
    gpiod::line_request req;
    req.consumer = PROCESS_NAME;
    if (!desc.empty()) {
        req.consumer += "-" + desc;
    }
    if (directionOut) {
        req.request_type = gpiod::line_request::DIRECTION_OUTPUT;
    } else {
        req.request_type = gpiod::line_request::DIRECTION_INPUT;
        if (mode == "gpio_pu") {
            req.flags |= gpiod::line_request::FLAG_BIAS_PULL_UP;
        } else if (mode == "gpio_pd") {
            req.flags |= gpiod::line_request::FLAG_BIAS_PULL_DOWN;
        } else {
            req.flags |= gpiod::line_request::FLAG_BIAS_DISABLE;
        }
    }
    if (req.request_type != lastRequestType) {
        if (line.is_requested()) {
            line.release();
        }
        try {
            line.request(req, 1);
        } catch (const std::exception& ex) {
            WarningHolder::AddWarning("Could not configure pin " + name + " as " + mode + " (" + ex.what() + ")");
        }
        lastRequestType = req.request_type;
    }
#endif
    return 0;
}
void GPIODCapabilities::releaseGPIOD() const {
#ifdef HASGPIOD
    if (line.is_requested()) {
        // printf("Releasing %s %d %d\n", name.c_str(), gpioIdx, gpio);
        line.release();
        lastRequestType = 0;
    }
#endif
}
int GPIODCapabilities::requestEventFile(bool risingEdge, bool fallingEdge) const {
    int fd = -1;
#ifdef HASGPIOD
    gpiod::line_request req;
    req.consumer = PROCESS_NAME;
    req.request_type = lastRequestType;
    if (risingEdge && fallingEdge) {
        req.request_type |= gpiod::line_request::EVENT_BOTH_EDGES;
    } else if (risingEdge) {
        req.request_type |= gpiod::line_request::EVENT_RISING_EDGE;
    } else if (fallingEdge) {
        req.request_type |= gpiod::line_request::EVENT_FALLING_EDGE;
    }
    if (line.is_requested()) {
        line.release();
    }
    try {
        line.request(req, 0);
    } catch (const std::exception& ex) {
        WarningHolder::AddWarning("Could not configure pin " + name + " for events (" + ex.what() + ") for edges Rising:" +
                                  std::to_string(risingEdge) + "   Falling:" + std::to_string(fallingEdge));
    }
    if (line.is_requested()) {
        fd = line.event_get_fd();
        if (fd < 0) {
            WarningHolder::AddWarning("Could not get event file descriptor for pin " + name);
        }
    } else {
        WarningHolder::AddWarning("Could not request event for pin " + name);
    }
#endif
    return fd;
}

bool GPIODCapabilities::getValue() const {
#ifdef HASGPIOD
    return line.get_value();
#else
    return 0;
#endif
}
void GPIODCapabilities::setValue(bool i) const {
#ifdef HASGPIOD
    line.set_value(i ? 1 : 0);
#endif
}
static std::vector<GPIODCapabilities> GPIOD_PINS;
static PinCapabilitiesProvider* PIN_PROVIDER = nullptr;

void PinCapabilities::InitGPIO(const std::string& process, PinCapabilitiesProvider* p) {
    PROCESS_NAME = process;
    if (p == nullptr) {
        p = new NoPinCapabilitiesProvider();
    }
    PIN_PROVIDER = p;
#ifdef HASGPIOD
    int chipCount = 0;
    int pinCount = 0;
    ::gpiod_chip* chip = gpiod_chip_open_by_number(0);
    if (chip != nullptr) {
        ::gpiod_chip_close(chip);
        if (GPIOD_PINS.empty()) {
            // has at least one chip
            std::set<std::string> found;
            for (auto& a : gpiod::make_chip_iter()) {
                std::string name = a.name();
                std::string label = a.label();

                if (PLATFORM_IGNORES.find(label) == PLATFORM_IGNORES.end()) {
                    char i = 'b';
                    while (found.find(label) != found.end()) {
                        label = a.label() + i;
                        i++;
                    }
                    found.insert(label);
                    for (int x = 0; x < a.num_lines(); x++) {
                        std::string n = label + "-" + std::to_string(x);
                        std::string plabel = a.get_line(x).name();
                        if (plabel.empty()) {
                            plabel = n;
                        }
                        GPIOD_PINS.push_back(GPIODCapabilities(plabel, pinCount + x, name).setGPIO(chipCount, x));
                    }
                }
                pinCount += a.num_lines();
                chipCount++;
            }
        }
    }
#endif
    PIN_PROVIDER->Init();
}
std::vector<std::string> PinCapabilities::getPinNames() {
    std::vector<std::string> pn = PIN_PROVIDER->getPinNames();
    for (auto& a : GPIOD_PINS) {
        pn.push_back(a.name);
    }
    return pn;
}

const PinCapabilities& PinCapabilities::getPinByName(const std::string& n) {
    for (auto& a : GPIOD_PINS) {
        if (n == a.name) {
            return a;
        }
    }
    return PIN_PROVIDER->getPinByName(n);
}
const PinCapabilities& PinCapabilities::getPinByGPIO(int chip, int gpio) {
    
    for (auto& a : GPIOD_PINS) {
        if (a.gpioIdx == chip && a.gpio == gpio) {
            return a;
        }
    }
    return PIN_PROVIDER->getPinByGPIO(chip, gpio);
}
const PinCapabilities& PinCapabilities::getPinByUART(const std::string& n) {
    return PIN_PROVIDER->getPinByUART(n);
}

void PinCapabilities::SetMultiPinValue(const std::list<const PinCapabilities*>& pins, int v) {
    if (pins.empty()) {
        return;
    }
    for (auto p : pins) {
        p->setValue(v);
    }
}

/*
 * This file is part of the Falcon Player (FPP) and is Copyright (C)
 * 2013-2022 by the Falcon Player Developers.
 *
 * The Falcon Player (FPP) is free software, and is covered under
 * multiple Open Source licenses.  Please see the included 'LICENSES'
 * file for descriptions of what files are covered by each license.
 *
 * This source file is covered under the GPL v2 as described in the
 * included LICENSE.GPL file.
 */

#include "fpp-pch.h"

#include "BBBMatrix.h"
#include <sys/mman.h>
#include <sys/wait.h>
#include <cmath>
#include <fstream>

#include "../Warnings.h"
#include "../common.h"
#include "../log.h"

#include "PanelInterleaveHandler.h"
#include "overlays/PixelOverlay.h"
#include "util/BBBUtils.h"

#include "Plugin.h"
class BBBMatrixPlugin : public FPPPlugins::Plugin, public FPPPlugins::ChannelOutputPlugin {
public:
    BBBMatrixPlugin() :
        FPPPlugins::Plugin("BBBMatrix") {
    }
    virtual ChannelOutput* createChannelOutput(unsigned int startChannel, unsigned int channelCount) override {
        return new BBBMatrix(startChannel, channelCount);
    }
};

extern "C" {
FPPPlugins::Plugin* createPlugin() {
    return new BBBMatrixPlugin();
}
}

// These are the number of clock cycles it takes to clock out a single "row" of bits (1 bit) for 32x16 1/8 P10 scan panels.  Other
// panel types and scan rates and stuff are proportional to these
static const uint32_t v1Timings[8][16] = {
    { 0xA65, 0x14EA, 0x1F6F, 0x29ED, 0x346E, 0x3EEE, 0x496C, 0x53EB, 0x5E6B, 0x68ED, 0x7374, 0x7DF0, 0x887B, 0x92F4, 0x9D75, 0xA7FB },
    { 0xA69, 0x14EA, 0x1F6F, 0x29ED, 0x346E, 0x3EEE, 0x496C, 0x53EB, 0x5E6B, 0x68ED, 0x7374, 0x7DF0, 0x887B, 0x92F4, 0x9D75, 0xA7FB },
    { 0xA6A, 0x14EA, 0x1F6F, 0x29ED, 0x346E, 0x3EEE, 0x496C, 0x53EB, 0x5E6B, 0x68ED, 0x7374, 0x7DF0, 0x887B, 0x92F4, 0x9D75, 0xA7FB },
    { 0xC88, 0x1906, 0x2588, 0x340D, 0x3E8D, 0x4B10, 0x5790, 0x6410, 0x7090, 0x7D10, 0x8990, 0x9610, 0xA291, 0xAF0D, 0xBB90, 0xC80D },
    { 0xC88, 0x1906, 0x2588, 0x340D, 0x3E8D, 0x4B10, 0x5790, 0x6410, 0x7090, 0x7D10, 0x8990, 0x9610, 0xA291, 0xAF0D, 0xBB90, 0xC80D },
    { 0xC88, 0x1906, 0x2588, 0x340D, 0x3E8D, 0x4B10, 0x5790, 0x6410, 0x7090, 0x7D10, 0x8990, 0x9610, 0xA291, 0xAF0D, 0xBB90, 0xC80D },
    { 0xC88, 0x1906, 0x2588, 0x340D, 0x3E8D, 0x4B10, 0x5790, 0x6410, 0x7090, 0x7D10, 0x8990, 0x9610, 0xA291, 0xAF0D, 0xBB90, 0xC80D },
    { 0xC90, 0x190D, 0x2590, 0x340D, 0x3E8D, 0x4B10, 0x5790, 0x6410, 0x7090, 0x7D10, 0x8990, 0x9610, 0xA291, 0xAF0D, 0xBB90, 0xC80D }
};
static const uint32_t v2Timings[8][16] = {
    { 0x347, 0x64F, 0x95F, 0xC70, 0xF80, 0x129E, 0x15B3, 0x18CB, 0x1BE4, 0x1EFB, 0x2211, 0x252C, 0x2786, 0x2A88, 0x2D85, 0x3088 },
    { 0x864, 0x10ED, 0x196C, 0x21EA, 0x2A6C, 0x32E9, 0x3B6A, 0x43EC, 0x4C6A, 0x54EA, 0x5D6B, 0x65EB, 0x6E6D, 0x76ED, 0x7F6E, 0x87EE },
    { 0x864, 0x10ED, 0x196C, 0x21EA, 0x2A6C, 0x32E9, 0x3B6A, 0x43EC, 0x4C6A, 0x54EA, 0x5D6B, 0x65EB, 0x6E6D, 0x76ED, 0x7F6E, 0x87EE },
    { 0xA69, 0x14EA, 0x1F6F, 0x29ED, 0x346E, 0x3EEE, 0x496C, 0x53EB, 0x5E6B, 0x68ED, 0x7374, 0x7DF0, 0x887B, 0x92F4, 0x9D75, 0xA7FB },
    { 0xA6A, 0x14EA, 0x1F6F, 0x29ED, 0x346E, 0x3EEE, 0x496C, 0x53EB, 0x5E6B, 0x68ED, 0x7374, 0x7DF0, 0x887B, 0x92F4, 0x9D75, 0xA7FB },
    { 0xC88, 0x1906, 0x2588, 0x340D, 0x3E8D, 0x4B10, 0x5790, 0x6410, 0x7090, 0x7D10, 0x8990, 0x9610, 0xA291, 0xAF0D, 0xBB90, 0xC80D },
    { 0xC88, 0x1906, 0x2588, 0x340D, 0x3E8D, 0x4B10, 0x5790, 0x6410, 0x7090, 0x7D10, 0x8990, 0x9610, 0xA291, 0xAF0D, 0xBB90, 0xC80D },
    { 0xC88, 0x1906, 0x2588, 0x340D, 0x3E8D, 0x4B10, 0x5790, 0x6410, 0x7090, 0x7D10, 0x8990, 0x9610, 0xA291, 0xAF0D, 0xBB90, 0xC80D },
};
static const uint32_t psTimings[8][16] = {
    { 0x440, 0x820, 0xC30, 0x1000, 0x1400, 0x17F1, 0x1C00, 0x2000, 0x2400, 0x2800, 0x2C00, 0x3000, 0x3400, 0x3800, 0x3C00, 0x4000 },
    { 0x940, 0x12C0, 0x1C80, 0x25C0, 0x2F40, 0x3900, 0x4240, 0x4C00, 0x5540, 0x5EC0, 0x6850, 0x71C0, 0x7B40, 0x84C0, 0x8E80, 0x97CE },
    { 0xB70, 0x1700, 0x2280, 0x2E00, 0x3A00, 0x4500, 0x5080, 0x5C00, 0x6780, 0x7320, 0x7E80, 0x8A00, 0x9580, 0xA100, 0xAC80, 0xB7F0 },
    { 0xB80, 0x1700, 0x2280, 0x2E00, 0x3A00, 0x4500, 0x5080, 0x5C00, 0x6780, 0x7320, 0x7E80, 0x8A00, 0x9580, 0xA100, 0xAC80, 0xB800 },
    { 0xB90, 0x1700, 0x2280, 0x2E00, 0x3A00, 0x4500, 0x5080, 0x5C00, 0x6780, 0x7320, 0x7E80, 0x8A00, 0x9580, 0xA100, 0xAC80, 0xB800 },
    { 0xB90, 0x1700, 0x2280, 0x2E00, 0x3A00, 0x4500, 0x5080, 0x5C00, 0x6780, 0x7320, 0x7E80, 0x8A00, 0x9580, 0xA100, 0xAC80, 0xB800 },
    { 0xC88, 0x1906, 0x2588, 0x340D, 0x3E8D, 0x4B10, 0x5790, 0x6410, 0x7090, 0x7D10, 0x8990, 0x9610, 0xA291, 0xAF0D, 0xBB90, 0xC80D },
    { 0xC88, 0x1906, 0x2588, 0x340D, 0x3E8D, 0x4B10, 0x5790, 0x6410, 0x7090, 0x7D10, 0x8990, 0x9610, 0xA291, 0xAF0D, 0xBB90, 0xC80D },
};

static std::map<int, std::vector<int>> BIT_ORDERS = {
    { 6, { 5, 2, 1, 4, 3, 0 } },
    { 7, { 6, 2, 1, 4, 5, 3, 0 } },
    { 8, { 7, 3, 5, 1, 2, 6, 4, 0 } },
    //{ 8, { 7, 6, 5, 4, 3, 2, 1, 0 } },
    { 9, { 8, 3, 5, 1, 7, 2, 6, 4, 0 } },
    { 10, { 9, 4, 1, 6, 3, 8, 2, 7, 5, 0 } },
    { 11, { 10, 4, 7, 2, 3, 1, 6, 9, 8, 5, 0 } },
    { 12, { 11, 5, 8, 2, 4, 1, 7, 10, 3, 9, 6, 0 } }
};
static void compilePRUMatrixCode(std::vector<std::string>& sargs) {
    pid_t compilePid = fork();
    if (compilePid == 0) {
        char* args[sargs.size() + 3];
        args[0] = (char*)"/bin/bash";
        args[1] = (char*)"/opt/fpp/src/pru/compileMatrix.sh";

        for (int x = 0; x < sargs.size(); x++) {
            args[x + 2] = (char*)sargs[x].c_str();
        }
        args[sargs.size() + 2] = NULL;

        execvp("/bin/bash", args);
    } else {
        wait(NULL);
    }
}

void BBBMatrix::calcBrightnessFlags(std::vector<std::string>& sargs) {
    LogDebug(VB_CHANNELOUT, "Calc Brightness:   maxPanel:  %d    maxOutput: %d     Brightness: %d    rpo: %d    ph:  %d    pw:  %d\n", m_longestChain, m_outputs, m_brightness, m_panelScan, m_panelHeight, m_panelWidth);

    uint32_t max = 0xB00;
    switch (m_timing) {
    case 2:
        max = psTimings[m_outputs - 1][m_longestChain - 1];
        break;
    case 1:
        max = v2Timings[m_outputs - 1][m_longestChain - 1];
        break;
    default:
        max = v1Timings[m_outputs - 1][m_longestChain - 1];
        break;
    }

    // timings are based on 32 pixel wide panels
    max *= m_panelWidth;
    max /= 32;

    // 1/4 scan we need to double the time since we have twice the number of pixels to clock out
    max *= m_panelHeight;
    max /= (m_panelScan * 2);

    uint32_t origMax = max;
    if (m_colorDepth >= 11 && max < 0x9000) {
        // for depth 10, we'll need a little more on time
        // or the last bit will be on far too short
        max = 0x9000;
    } else if (m_colorDepth >= 10 && max < 0x8000) {
        // for depth 10, we'll need a little more on time
        // or the last bit will be on far too short
        max = 0x8000;
    } else if (m_colorDepth == 9 && max < 0x6800) {
        // for depth 9, we'll need a little more on time
        max = 0x6800;
    } else if (max < 0x4500) {
        // if max is too low, the low bit time is too short and
        // extra ghosting occurs
        //  At this point, framerate will be supper high anyway >100fps
        max = 0x4500;
    }

    uint32_t origMax2 = max;

    max *= m_brightness;
    max /= 10;

    uint32_t delay = origMax2 - max;
    if ((origMax2 > origMax) && (origMax > max)) {
        delay = origMax - max;
    }

    int maxBit = 8;
    if (m_colorDepth > 8) {
        maxBit = m_colorDepth;
    }
    // printf("Delay : %d      Max:  %d       OrigMax:   %d      OrigMax2:  %d\n", delay, max, origMax, origMax2);
    for (int x = 0; x < maxBit; x++) {
        LogDebug(VB_CHANNELOUT, "Brightness %d:  %X\n", x, max);
        delayValues[x] = delay;
        brightnessValues[x] = max;
        max >>= 1;
        origMax >>= 1;
    }
    // low value cannot be less than 20 or ghosting
    if (brightnessValues[maxBit - 1] < 20) {
        brightnessValues[maxBit - 1] = 20;
    }
    if (brightnessValues[maxBit - 2] < brightnessValues[maxBit - 1]) {
        brightnessValues[maxBit - 2] = brightnessValues[maxBit - 2] + 5;
    }

    if (FileExists("/home/fpp/media/config/ledscape_dimming")) {
        FILE* file = fopen("/home/fpp/media/config/ledscape_dimming", "r");

        if (file != NULL) {
            char buf[100];
            char* line = buf;
            size_t len = 100;
            ssize_t read;
            int count = 0;

            while (((read = getline(&line, &len, file)) != -1) && (count < 10)) {
                if ((!line) || (!read) || (read == 1))
                    continue;

                LogDebug(VB_CHANNELOUT, "Line %d: %s\n", count, line);
                if (count == 0) {
                    m_printStats = atoi(line);
                    count++;
                } else {
                    uint32_t d1, d2;
                    sscanf(line, "%X %X", &d1, &d2);
                    brightnessValues[count - 1] = d1;
                    delayValues[count - 1] = d2;
                    count++;
                }
                line = buf;
                len = 100;
            }
            fclose(file);
        }
    }

    char buf[255];

    int x = m_colorDepth;
    m_bitOrder.clear();
    if (m_outputByRow) {
        // if outputing by row, we have to keep in decending order
        for (int x = m_colorDepth; x > 0; --x) {
            m_bitOrder.push_back(x - 1);
        }
    } else {
        m_bitOrder = BIT_ORDERS[m_colorDepth];
    }

    for (auto b : m_bitOrder) {
        int idx = m_colorDepth - b - 1;
        snprintf(buf, sizeof(buf), "-DBRIGHTNESS%d=%d", x, brightnessValues[idx]);

        sargs.push_back(buf);
        snprintf(buf, sizeof(buf), "-DDELAY%d=%d", x, delayValues[idx]);
        sargs.push_back(buf);
        x--;
    }
}

BBBMatrix::BBBMatrix(unsigned int startChannel, unsigned int channelCount) :
    ChannelOutput(startChannel, channelCount),
    m_pru(nullptr),
    m_pruCopy(nullptr),
    m_matrix(nullptr),
    m_panelMatrix(nullptr),
    m_outputs(0),
    m_gpioFrame(nullptr),
    m_longestChain(0),
    m_panelWidth(32),
    m_panelHeight(16),
    m_invertedData(0),
    m_brightness(10),
    m_colorDepth(8),
    m_panelInterleave(),
    m_panelScan(8),
    m_timing(0),
    m_printStats(false),
    m_handler(nullptr),
    m_outputByRow(false),
    m_outputBlankData(false),
    m_curFrame(0),
    m_numFrames(0) {
    LogDebug(VB_CHANNELOUT, "BBBMatrix::BBBMatrix(%u, %u)\n",
             startChannel, channelCount);
}

BBBMatrix::~BBBMatrix() {
    LogDebug(VB_CHANNELOUT, "BBBMatrix::~BBBMatrix()\n");
    if (m_gpioFrame)
        delete[] m_gpioFrame;
    if (m_pru)
        delete m_pru;
    if (m_pruCopy)
        delete m_pruCopy;
    if (m_handler)
        delete m_handler;
    if (m_matrix)
        delete m_matrix;
    if (m_panelMatrix)
        delete m_panelMatrix;
}

bool BBBMatrix::configureControlPin(const std::string& ctype, Json::Value& root, std::ofstream& outputFile, int pru, int& controlGPIO) {
    std::string type = root["controls"][ctype]["type"].asString();
    if (type != "none") {
        std::string pinName = root["controls"][ctype]["pin"].asString();
        const PinCapabilities& pin = PinCapabilities::getPinByName(pinName);
        if (ctype == "oe" && pin.pwm >= 99) {
            outputFile << "#define oe_pwm_address " << std::to_string(pin.getPWMRegisterAddress()) << "\n";
            outputFile << "#define oe_pwm_output " << std::to_string(pin.subPwm) << "\n";
            int max = 300 * 255;
            // FIXME - adjust max for brightness
            pin.setupPWM(max);
            pin.setPWMValue(0);
            return true;
        } else {
            if (type == "pruout") {
                pin.configPin("pru" + std::to_string(pru) + "out");
                const BBBPinCapabilities* bp = static_cast<const BBBPinCapabilities*>(&pin);
                outputFile << "#define pru_" << ctype << " " << std::to_string(bp->pruPin(pru)) << "\n";
            } else {
                pin.configPin(type);
                outputFile << "#define gpio_" << ctype << " " << std::to_string(pin.mappedGPIO()) << "\n";
                controlGPIO = pin.mappedGPIOIdx();
            }
        }
        m_usedPins.push_back(pinName);
    }
    return false;
}

void BBBMatrix::configurePanelPin(int x, const std::string& color, int row, Json::Value& root, std::ofstream& outputFile, int* minPort) {
    std::string pinName = root["outputs"][x]["pins"][color + std::to_string(row)].asString();
    const PinCapabilities& pin = PinCapabilities::getPinByName(pinName);
    pin.configPin();
    m_usedPins.push_back(pinName);
    int gpioIdx = pin.mappedGPIOIdx();
    minPort[gpioIdx] = std::min(minPort[gpioIdx], (int)pin.mappedGPIO());
    outputFile << "#define " << color << std::to_string(x + 1) << std::to_string(row) << "_gpio " << std::to_string(pin.mappedGPIOIdx()) << "\n";
    outputFile << "#define " << color << std::to_string(x + 1) << std::to_string(row) << "_pin  " << std::to_string(pin.mappedGPIO()) << "\n";

    if (color == "r") {
        m_pinInfo[x].row[row - 1].r_gpio = pin.mappedGPIOIdx();
        m_pinInfo[x].row[row - 1].r_pin = 1UL << pin.mappedGPIO();
    } else if (color == "g") {
        m_pinInfo[x].row[row - 1].g_gpio = pin.mappedGPIOIdx();
        m_pinInfo[x].row[row - 1].g_pin = 1UL << pin.mappedGPIO();
    } else {
        m_pinInfo[x].row[row - 1].b_gpio = pin.mappedGPIOIdx();
        m_pinInfo[x].row[row - 1].b_pin = 1UL << pin.mappedGPIO();
    }
}

void BBBMatrix::configurePanelPins(int x, Json::Value& root, std::ofstream& outputFile, int* minPort) {
    configurePanelPin(x, "r", 1, root, outputFile, minPort);
    configurePanelPin(x, "g", 1, root, outputFile, minPort);
    configurePanelPin(x, "b", 1, root, outputFile, minPort);
    outputFile << "\n";
    configurePanelPin(x, "r", 2, root, outputFile, minPort);
    configurePanelPin(x, "g", 2, root, outputFile, minPort);
    configurePanelPin(x, "b", 2, root, outputFile, minPort);
    outputFile << "\n";
}

int BBBMatrix::Init(Json::Value config) {
    LogDebug(VB_CHANNELOUT, "BBBMatrix::Init(JSON)\n");

    m_panelWidth = config["panelWidth"].asInt();
    m_panelHeight = config["panelHeight"].asInt();
    if (!m_panelWidth)
        m_panelWidth = 32;

    if (!m_panelHeight)
        m_panelHeight = 16;

    int addressingType = config["panelRowAddressType"].asInt();

    m_invertedData = config["invertedData"].asInt();
    m_colorOrder = ColorOrderFromString(config["colorOrder"].asString());

    m_panelMatrix = new PanelMatrix(m_panelWidth, m_panelHeight, m_invertedData);
    if (!m_panelMatrix) {
        LogErr(VB_CHANNELOUT, "BBBMatrix: Unable to create PanelMatrix\n");
        return 0;
    }
    bool usesOutput[16] = {
        false, false, false, false,
        false, false, false, false,
        false, false, false, false,
        false, false, false, false
    };
    for (int i = 0; i < config["panels"].size(); i++) {
        Json::Value p = config["panels"][i];
        char orientation = 'N';
        const char* o = p["orientation"].asString().c_str();

        if (o && *o)
            orientation = o[0];

        if (p["colorOrder"].asString() == "")
            p["colorOrder"] = ColorOrderToString(m_colorOrder);

        m_panelMatrix->AddPanel(p["outputNumber"].asInt(),
                                p["panelNumber"].asInt(), orientation,
                                p["xOffset"].asInt(), p["yOffset"].asInt(),
                                ColorOrderFromString(p["colorOrder"].asString()));

        if (p["outputNumber"].asInt() > m_outputs)
            m_outputs = p["outputNumber"].asInt();
        usesOutput[p["outputNumber"].asInt()] = true;

        if (p["panelNumber"].asInt() > m_longestChain)
            m_longestChain = p["panelNumber"].asInt();
    }
    // Both of these are 0-based, so bump them up by 1 for comparisons
    m_outputs++;
    m_longestChain++;

    // get the dimensions of the matrix
    m_panels = m_panelMatrix->PanelCount();
    m_rows = m_outputs * m_panelHeight;
    m_width = m_panelMatrix->Width();
    m_height = m_panelMatrix->Height();

    if (config.isMember("brightness")) {
        m_brightness = config["brightness"].asInt();
    }
    if (m_brightness < 1 || m_brightness > 10) {
        m_brightness = 10;
    }
    if (config.isMember("panelColorDepth")) {
        m_colorDepth = config["panelColorDepth"].asInt();
    }
    if (config.isMember("panelOutputOrder")) {
        m_outputByRow = config["panelOutputOrder"].asBool();
    }
    if (config.isMember("panelOutputBlankRow")) {
        m_outputBlankData = config["panelOutputBlankRow"].asBool();
    }
    if (m_colorDepth < 0) {
        m_colorDepth = -m_colorDepth;
        m_outputBlankData = true;
    }
    if (m_colorDepth > 12 || m_colorDepth < 6) {
        m_colorDepth = 8;
    }

    if (config.isMember("panelInterleave")) {
        m_panelInterleave = config["panelInterleave"].asString();
    }
    m_panelScan = config["panelScan"].asInt();
    // printf("Interleave: %d     Scan: %d    ZZI: %d    ZZCI:  %d    SI: %d\n", m_interleave, m_panelScan, zigZagInterleave, zigZagClusterInterleave, stripeInterleave);
    if (m_panelScan == 0) {
        //  default scan is 1/2 the height of the panel
        m_panelScan = m_panelHeight / 2;
    }
    m_handler = PanelInterleaveHandler::createHandler(m_panelInterleave, m_panelWidth, m_panelHeight, m_panelScan);
    if (!m_handler) {
        LogErr(VB_CHANNELOUT, "BBBMatrix: Unable to create PanelInterleaveHandler for %s\n", m_panelInterleave.c_str());
        WarningHolder::AddWarning("BBBMatrix: Unable to create PanelInterleaveHandler for " + m_panelInterleave);
        return 0;
    }
    m_channelCount = m_width * m_height * 3;

    m_matrix = new Matrix(m_startChannel, m_width, m_height);

    if (config.isMember("subMatrices")) {
        for (int i = 0; i < config["subMatrices"].size(); i++) {
            Json::Value sm = config["subMatrices"][i];

            m_matrix->AddSubMatrix(
                sm["enabled"].asInt(),
                sm["startChannel"].asInt() - 1,
                sm["width"].asInt(),
                sm["height"].asInt(),
                sm["xOffset"].asInt(),
                sm["yOffset"].asInt());
        }
    }

    m_rowSize = m_longestChain * m_panelWidth * 3;
    int maxBits = 8;
    if (m_colorDepth > 8) {
        maxBits = m_colorDepth;
    }
    int gpioFrameLen = m_longestChain * m_panelWidth * maxBits * (m_panelHeight / 2) * 4;
    m_gpioFrame = new uint32_t[gpioFrameLen];
    memset(m_gpioFrame, 0, gpioFrameLen * 4);

    std::vector<std::string> compileArgs;

    std::string dirname = "bbb";
    std::string name = "Octoscroller";
    if (getBeagleBoneType() == PocketBeagle) {
        dirname = "pb";
        name = "PocketScroller";
    }
    if (config["wiringPinout"] == "v2") {
        name += "-v2";
    }
    if (config["wiringPinout"] == "v3") {
        name += "-v3";
    }
    Json::Value root;
    char filename[256];
    int minPort[4] = { 99, 99, 99, 99 };
    int pru = 0;
    snprintf(filename, sizeof(filename), "/home/fpp/media/tmp/panels/%s.json", name.c_str());
    if (!FileExists(filename)) {
        snprintf(filename, sizeof(filename), "/home/fpp/media/tmp/panels/CapePanels.json", dirname.c_str());
        if (FileExists(filename)) {
            name = "CapePanels";
        }
    }
    if (!FileExists(filename)) {
        snprintf(filename, sizeof(filename), "/opt/fpp/capes/%s/panels/%s.json", dirname.c_str(), name.c_str());
    }
    bool isPWM = false;
    if (!FileExists(filename)) {
        LogErr(VB_CHANNELOUT, "No output pin configuration for %s - %s\n", name.c_str(), filename);
        WarningHolder::AddWarning("BBBMatrix: No output pin configuration for " + name);
        return 0;
    } else {
        LogDebug(VB_CHANNELOUT, "Using panel pinout from %s\n", filename);
        if (!LoadJsonFromFile(filename, root)) {
            LogErr(VB_CHANNELOUT, "Could not read pin configuration for %s - %s\n", name.c_str(), filename);
            WarningHolder::AddWarning("BBBMatrix: Could not read pin configuration for " + name);
            return 0;
        }
        std::string longName = root["longName"].asString();
        LogDebug(VB_CHANNELOUT, "Using pin configuration for %s from %s\n", longName.c_str(), filename);

        std::ofstream outputFile;
        outputFile.open("/tmp/PanelPinConfiguration.hp", std::ofstream::out | std::ofstream::trunc);

        // kind of a hack, ideally the timing info would go into the json as well
        m_timing = root["timing"].asInt();

        pru = root["pru"].asInt();

        if (root.isMember("singlePRU")) {
            m_singlePRU = root["singlePRU"].asBool();
        }
        // m_singlePRU = true;

        if (root.isMember("dataOffset")) {
            m_dataOffset = root["dataOffset"].asInt();
            m_dataOffset *= 1024; // dataOffset is in KB
        }

        int controlGpio = root["controls"]["gpio"].asInt();
        configureControlPin("latch", root, outputFile, pru, controlGpio);
        outputFile << "\n";
        isPWM = configureControlPin("oe", root, outputFile, pru, controlGpio);
        outputFile << "\n";
        configureControlPin("clock", root, outputFile, pru, controlGpio);
        outputFile << "\n";
        configureControlPin("sel0", root, outputFile, pru, controlGpio);
        configureControlPin("sel1", root, outputFile, pru, controlGpio);
        configureControlPin("sel2", root, outputFile, pru, controlGpio);
        configureControlPin("sel3", root, outputFile, pru, controlGpio);
        if (m_panelScan == 32) {
            // 1:32 scan panels need the "E" line
            configureControlPin("sel4", root, outputFile, pru, controlGpio);
            compileArgs.push_back("-DE_SCAN_LINE");
        }
        outputFile << "\n";
        outputFile << "#define CONTROLS_GPIO_BASE GPIO" << std::to_string(controlGpio) << "\n";
        outputFile << "#define gpio_controls_led_mask gpio" << std::to_string(controlGpio) << "_led_mask\n";
        outputFile << "\n";
        if (m_outputs > root["outputs"].size()) {
            m_outputs = root["outputs"].size();
        }
        for (int x = 0; x < 8; x++) {
            if (usesOutput[x] && x < m_outputs) {
                configurePanelPins(x, root, outputFile, minPort);
            } else {
                outputFile << "#define NO_OUTPUT_" << std::to_string(x + 1) << "\n";
                outputFile << "\n";
            }
        }
        outputFile << "\n";
        for (int x = 0; x < 4; x++) {
            if (minPort[x] != 99) {
                if (x == controlGpio) {
                    outputFile << "#define OUTPUT_GPIO_" << std::to_string(x) << "(a, b, c, d) OUTPUT_GPIO_FORCE_CLEAR a, b, c, d\n";
                } else {
                    outputFile << "#define OUTPUT_GPIO_" << std::to_string(x) << "(a, b, c, d) OUTPUT_GPIO a, b, c, d\n";
                }
            } else {
                outputFile << "#define OUTPUT_GPIO_" << std::to_string(x) << "(a, b, c, d)\n";
            }
        }
        outputFile << "\n";
        if (minPort[controlGpio] == 99) {
            // not outputting anything on the GPIO the controls are using
            // we need to make sure the controls are set/cleared indepentent of the panel data
            outputFile << "#define NO_CONTROLS_WITH_DATA\n";
        }
        outputFile << "\n";

        outputFile.close();
    }

    char buf[200];
    if (m_singlePRU) {
        compileArgs.push_back("-DSINGLEPRU");
    }
    snprintf(buf, sizeof(buf), "-DRUNNING_ON_PRU%d", pru);
    compileArgs.push_back(buf);
    snprintf(buf, sizeof(buf), "-DRUNNING_ON_PRU=%d", pru);
    compileArgs.push_back(buf);
    snprintf(buf, sizeof(buf), "-DOUTPUTS=%d", m_outputs);
    compileArgs.push_back(buf);
    snprintf(buf, sizeof(buf), "-DROWS=%d", m_panelScan);
    compileArgs.push_back(buf);
    snprintf(buf, sizeof(buf), "-DBITS=%d", m_colorDepth);
    compileArgs.push_back(buf);
    int tmp = m_longestChain * m_panelWidth;
    if (m_panelScan * 4 == m_panelHeight) {
        tmp *= 2;
    } else if (m_panelScan * 8 == m_panelHeight) {
        tmp *= 4;
    }
    snprintf(buf, sizeof(buf), "-DROW_LEN=%d", tmp);
    compileArgs.push_back(buf);
    if (isPWM) {
        compileArgs.push_back("-DUSING_PWM");
    }

    if (addressingType == 2) {
        // 1/2 scan panel that uses 2 bits, bit one for scan row 1 and bit two for row 2
        // Normal addressing would be 1 bit, 0 for row 1, 1 for row 2
        compileArgs.push_back("-DADDRESSING_AB=1");
    }

    calcBrightnessFlags(compileArgs);
    // m_printStats = true;
    if (m_printStats) {
        snprintf(buf, sizeof(buf), "-DENABLESTATS=1", m_outputs);
        compileArgs.push_back("-DENABLESTATS=1");
    }

    if (m_outputByRow) {
        compileArgs.push_back("-DOUTPUTBYROW");
        if (m_outputBlankData) {
            compileArgs.push_back("-DOUTPUTBLANKROW");
        }
    } else {
        compileArgs.push_back("-DOUTPUTBYDEPTH");
    }
    compilePRUMatrixCode(compileArgs);
    std::string pru_program = "/tmp/FalconMatrix.out";

    if (!m_singlePRU) {
        m_pruCopy = new BBBPru(pru ? 0 : 1);
        m_pruCopy->clearPRUMem(m_pruCopy->data_ram, 24);
    }

    m_pru = new BBBPru(pru);
    m_pruData = (BBBPruMatrixData*)m_pru->data_ram;
    m_pruCopy->clearPRUMem(m_pru->data_ram, sizeof(BBBPruMatrixData));
    m_pruData->address_dma = m_pru->ddr_addr + m_dataOffset;
    m_pruData->command = 0;
    m_pruData->response = 0;

    if (!m_singlePRU) {
        m_pruCopy->run("/tmp/FalconMatrixPRUCpy.out");
    }
    m_pru->run(pru_program);

    float gamma = 2.2;
    if (config.isMember("gamma")) {
        gamma = atof(config["gamma"].asString().c_str());
    }
    if (gamma < 0.01 || gamma > 50.0) {
        gamma = 2.2;
    }
    for (int x = 0; x < 256; x++) {
        int v = x;
        if (m_colorDepth == 6 && (v == 3 || v == 2)) {
            v = 4;
        } else if (m_colorDepth == 7 && v == 1) {
            v = 2;
        }
        float max = 255.0f;
        switch (m_colorDepth) {
        case 12:
            max = 4095.0f;
            break;
        case 11:
            max = 2047.0f;
            break;
        case 10:
            max = 1023.0f;
            break;
        case 9:
            max = 511.0f;
            break;
        }
        float f = v;
        f = max * pow(f / 255.0f, gamma);
        if (f > max) {
            f = max;
        }
        if (f < 0.0) {
            f = 0.0;
        }
        gammaCurve[x] = round(f);
        if (gammaCurve[x] == 0 && f > 0.25) {
            // don't drop as much of the low end to 0
            gammaCurve[x] = 1;
        }
        // printf("%d   %d  (%f)\n", x, gammaCurve[x], f);
    }

    if (isPWM) {
        // need to calculate the clock counts for the PWM subsystem
        int i = m_pruData->pwmBrightness[0];
        while (i == 0) {
            i = m_pruData->pwmBrightness[0];
        }
        printf("PERIOD: %X\n", i);

        int f = i;
        // f *= 300;
        // f /= 500;
        i = f;
        printf("New PERIOD: %X\n", i);

        i *= m_brightness;
        i /= 10;
        for (int x = 0; x < 8; x++) {
            printf("%d: %X\n", x, i);
            m_pruData->pwmBrightness[7 - x] = i;
            i /= 2;
        }
    }
    /*
    for (int x = 0; x < 8; x++) {
        printf("%d  R1:   %d   %8X\n",x,  m_pinInfo[x].row[0].r_gpio, m_pinInfo[x].row[0].r_pin);
        printf("    G1:   %d   %8X\n", m_pinInfo[x].row[0].g_gpio, m_pinInfo[x].row[0].g_pin);
        printf("    B1:   %d   %8X\n", m_pinInfo[x].row[0].b_gpio, m_pinInfo[x].row[0].b_pin);
        printf("    R2:   %d   %8X\n", m_pinInfo[x].row[1].r_gpio, m_pinInfo[x].row[1].r_pin);
        printf("    G2:   %d   %8X\n", m_pinInfo[x].row[1].g_gpio, m_pinInfo[x].row[1].g_pin);
        printf("    B2:   %d   %8X\n", m_pinInfo[x].row[1].b_gpio, m_pinInfo[x].row[1].b_pin);
    }
    */

    // make sure the PRU starts outputting a blank frame to remove any random noise
    // from the panels
    m_fullFrameLen = gpioFrameLen * 4;
    // round up to next page boundary
    int alignedLen = m_fullFrameLen + 8192;
    alignedLen -= (alignedLen & 0xFFF);

    m_frames[0] = (uint8_t*)m_pru->ddr + m_dataOffset;
    uint8_t* maxPtr = m_frames[0];
    maxPtr += (m_pru->ddr_size - m_dataOffset);
    m_numFrames = 0;
    for (int x = 1; x < 8; x++) {
        m_frames[x] = m_frames[x - 1] + alignedLen;
        if (m_frames[x] < maxPtr) {
            m_numFrames++;
        }
    }
    if ((m_frames[7] + alignedLen) < maxPtr) {
        m_numFrames++;
    }
    m_curFrame = 0;
    if (PixelOverlayManager::INSTANCE.isAutoCreatePixelOverlayModels()) {
        std::string dd = "LED Panels";
        if (config.isMember("LEDPanelMatrixName") && !config["LEDPanelMatrixName"].asString().empty()) {
            dd = config["LEDPanelMatrixName"].asString();
        }
        if (config.isMember("description")) {
            dd = config["description"].asString();
        }
        std::string desc = dd;
        int count = 0;
        while (PixelOverlayManager::INSTANCE.getModel(desc) != nullptr) {
            count++;
            desc = dd + "-" + std::to_string(count);
        }
        PixelOverlayManager::INSTANCE.addAutoOverlayModel(desc,
                                                          m_startChannel, m_channelCount, 3,
                                                          "H", m_invertedData ? "BL" : "TL",
                                                          m_height, 1);
    }
    // We need to send the data once to make sure the panels are cleared and "off"
    // However, this doesn't always work so we'll set everything slightly "on" first
    // real quick to make sure all the memory changes and is properly mapped into
    // the process, then we'll reset everything to off
    for (int x = 0; x < m_numFrames; x++) {
        memset(m_frames[x], 0x1, m_fullFrameLen);
    }
    msync(m_frames[0], m_pru->ddr_size, MS_SYNC | MS_INVALIDATE);
    SendData(nullptr);
    for (int x = 0; x < m_numFrames; x++) {
        memset(m_frames[x], 0x0, m_fullFrameLen);
    }
    msync(m_frames[0], m_pru->ddr_size, MS_SYNC | MS_INVALIDATE);
    SendData(nullptr);
    return ChannelOutput::Init(config);
}

int BBBMatrix::Close(void) {
    LogDebug(VB_CHANNELOUT, "BBBMatrix::Close()\n");
    // Send the stop command
    m_pruData->command = 0xFF;
    if (m_pru) {
        m_pru->stop();
        delete m_pru;
        m_pru = nullptr;
    }

    if (m_pruCopy) {
        m_pruCopy->stop();
        delete m_pruCopy;
        m_pruCopy = nullptr;
    }
    for (auto& pinName : m_usedPins) {
        const PinCapabilities& pin = PinCapabilities::getPinByName(pinName);
        pin.configPin("default", false);
    }
    return ChannelOutput::Close();
}

static int fcount = 0;

void BBBMatrix::printStats() {
    FILE* rfile;
    rfile = fopen("/tmp/framerates.txt", "w");
    for (int x = 0; x < m_colorDepth; ++x) {
        fprintf(rfile, "DV: %d    %8X   %8X\n", (m_colorDepth - x), brightnessValues[x], delayValues[x]);
    }
    int off = 0;
    uint32_t total = 0;
    int count = 0;
    for (int x = 0; x < m_panelScan; ++x) {
        for (int y = m_colorDepth; y > 0; --y) {
            fprintf(rfile, "r%2d  b%2d:   %8X   %8X   %8X\n", x, y, m_pruData->stats[off], m_pruData->stats[off + 1], m_pruData->stats[off + 2]);
            total += m_pruData->stats[off];
            count++;
            off += 3;
        }
    }
    fprintf(rfile, "Average Per Row/Bit:   %8X\n", (total / count));
    // printf("0x%X\n", (total / count));
    fclose(rfile);
}

void BBBMatrix::GetRequiredChannelRanges(const std::function<void(int, int)>& addRange) {
    addRange(m_startChannel, m_startChannel + m_channelCount - 1);
}

void BBBMatrix::OverlayTestData(unsigned char* channelData, int cycleNum, float percentOfCycle, int testType, const Json::Value& config) {
    for (int output = 0; output < m_outputs; output++) {
        int panelsOnOutput = m_panelMatrix->m_outputPanels[output].size();
        for (int i = 0; i < panelsOnOutput; i++) {
            int panel = m_panelMatrix->m_outputPanels[output][i];

            m_panelMatrix->m_panels[panel].drawTestPattern(channelData + m_startChannel, cycleNum, testType);
        }
    }
}

void BBBMatrix::PrepData(unsigned char* channelData) {
    m_matrix->OverlaySubMatrices(channelData);

    if (m_printStats) {
        fcount++;
        if (fcount == 20) {
            // every 20 frames or so, save stats
            fcount = 0;
            printStats();
        }
    }

    channelData += m_startChannel;

    // number of uint32_t per row for each bit
    size_t rowLen = m_panelWidth * m_longestChain * m_panelHeight / (m_panelScan * 2) * 4; // 4 GPIO's
    // number of uint32_t per full row (all bits)
    size_t fullRowLen = rowLen * m_colorDepth;

    uint32_t* gpioFrame = m_gpioFrame;
    /*
    if (m_numFrames >= 4) {
       gpioFrame = (uint32_t*)m_frames[m_curFrame];
    }
    */

    // long long startTime = GetTime();
    memset(gpioFrame, 0, m_fullFrameLen);
    // long long memsetTime = GetTime();

    for (int output = 0; output < m_outputs; output++) {
        int panelsOnOutput = m_panelMatrix->m_outputPanels[output].size();
        const GPIOPinInfo::Pins& pinInfo0 = m_pinInfo[output].row[0];
        const GPIOPinInfo::Pins& pinInfo1 = m_pinInfo[output].row[1];

        for (int i = 0; i < panelsOnOutput; i++) {
            int panel = m_panelMatrix->m_outputPanels[output][i];
            int chain = m_panelMatrix->m_panels[panel].chain;

            for (int y = 0; y < (m_panelHeight / 2); y++) {
                int yw1 = y * m_panelWidth * 3;
                int yw2 = (y + (m_panelHeight / 2)) * m_panelWidth * 3;

                int yOut = y;
                int xo2 = 0;
                m_handler->map(xo2, yOut);

                int offset = yOut * fullRowLen + (m_longestChain - chain - 1) * 4 * m_panelWidth * m_panelHeight / m_panelScan / 2;
                if (!m_outputByRow) {
                    offset = yOut * rowLen + (m_longestChain - chain - 1) * 4 * m_panelWidth * m_panelHeight / m_panelScan / 2;
                }

                for (int x = 0; x < m_panelWidth; ++x) {
                    uint16_t r1 = gammaCurve[channelData[m_panelMatrix->m_panels[panel].pixelMap[yw1 + x * 3]]];
                    uint16_t g1 = gammaCurve[channelData[m_panelMatrix->m_panels[panel].pixelMap[yw1 + x * 3 + 1]]];
                    uint16_t b1 = gammaCurve[channelData[m_panelMatrix->m_panels[panel].pixelMap[yw1 + x * 3 + 2]]];

                    uint16_t r2 = gammaCurve[channelData[m_panelMatrix->m_panels[panel].pixelMap[yw2 + x * 3]]];
                    uint16_t g2 = gammaCurve[channelData[m_panelMatrix->m_panels[panel].pixelMap[yw2 + x * 3 + 1]]];
                    uint16_t b2 = gammaCurve[channelData[m_panelMatrix->m_panels[panel].pixelMap[yw2 + x * 3 + 2]]];

                    int xOut = x;
                    int yo2 = y;
                    m_handler->map(xOut, yo2);

                    int xOff = xOut * 4;

                    for (auto bit : m_bitOrder) {
                        uint16_t mask = 1 << bit;
                        if (r1 & mask) {
                            gpioFrame[offset + xOff + pinInfo0.r_gpio] |= pinInfo0.r_pin;
                        }
                        if (g1 & mask) {
                            gpioFrame[offset + xOff + pinInfo0.g_gpio] |= pinInfo0.g_pin;
                        }
                        if (b1 & mask) {
                            gpioFrame[offset + xOff + pinInfo0.b_gpio] |= pinInfo0.b_pin;
                        }
                        if (r2 & mask) {
                            gpioFrame[offset + xOff + pinInfo1.r_gpio] |= pinInfo1.r_pin;
                        }
                        if (g2 & mask) {
                            gpioFrame[offset + xOff + pinInfo1.g_gpio] |= pinInfo1.g_pin;
                        }
                        if (b2 & mask) {
                            gpioFrame[offset + xOff + pinInfo1.b_gpio] |= pinInfo1.b_pin;
                        }
                        if (m_outputByRow) {
                            xOff += rowLen;
                        } else {
                            xOff += rowLen * m_panelScan;
                        }
                    }
                }
            }
        }
    }

    // long long dataTime = GetTime();
    if ((m_numFrames >= 3) && (m_frames[m_curFrame] != (uint8_t*)gpioFrame)) {
        memcpy(m_frames[m_curFrame], m_gpioFrame, m_fullFrameLen);
    }
    /*
    long long endTime = GetTime();
    if ((endTime - startTime) > 5000) {
        printf("%d:   Memset: %d     Data: %d    cpy: %d   Total: %d\n", m_curFrame, (int)(memsetTime- startTime), (int)(dataTime - memsetTime), (int)(endTime -dataTime), (int)(endTime -startTime));
    }
    */
}
int BBBMatrix::SendData(unsigned char* channelData) {
    LogExcess(VB_CHANNELOUT, "BBBMatrix::SendData(%p)\n", channelData);

    // long long startTime = GetTime();
    uint32_t addr = (m_pru->ddr_addr + m_dataOffset);
    addr += (m_frames[m_curFrame] - m_frames[0]);
    uint8_t* ptr = m_frames[m_curFrame];
    if (m_numFrames < 3) {
        // if we have less than 3 blocks, we cannot copy
        // in prep  or we'd get potential tearing/flickering
        memcpy(ptr, m_gpioFrame, m_fullFrameLen);
    }
    // long long cpyTime = GetTime();
    if (m_curFrame == 0) {
        m_curFrame = m_numFrames - 1;
    } else {
        m_curFrame--;
    }

    // make sure memory is flushed before command is set to 1
    msync(ptr, m_fullFrameLen, MS_SYNC | MS_INVALIDATE);
    __builtin___clear_cache(ptr, ptr + m_fullFrameLen);
    m_pruData->address_dma = addr;

    __asm__ __volatile__("" ::
                             : "memory");
    // long long flshTime = GetTime();
    m_pruData->command = 1;

    /*
    if (fcount == 0) {
        printf("%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x \n",
           m_outputFrame[0], m_outputFrame[1], m_outputFrame[2],
           m_outputFrame[3], m_outputFrame[4], m_outputFrame[5],
           m_outputFrame[6], m_outputFrame[7], m_outputFrame[8],
           m_outputFrame[9], m_outputFrame[10], m_outputFrame[11],
           m_outputFrame[12], m_outputFrame[13], m_outputFrame[15]
           );

        uint32_t *t = (uint32_t *)m_pruCopy->data_ram;
        printf("     %d   %X   %X    %X %X %X\n", t[0], t[1], t[2], t[3], t[4], t[5]);
    }
    */
    /*
    long long endTime = GetTime();
    if ((endTime - startTime) > 100) {
        printf("cpy: %d    flush: %d    cmd: %d    Total:  %d\n", (int)(cpyTime - startTime),  (int)(flshTime - cpyTime), (int)(endTime - flshTime), (int)(endTime - startTime));
    }
    */
    return m_channelCount;
}

void BBBMatrix::DumpConfig(void) {
    LogDebug(VB_CHANNELOUT, "BBBMatrix::DumpConfig()\n");
    LogDebug(VB_CHANNELOUT, "    Width          : %d\n", m_width);
    LogDebug(VB_CHANNELOUT, "    Height         : %d\n", m_height);
    LogDebug(VB_CHANNELOUT, "    Rows           : %d\n", m_rows);
    LogDebug(VB_CHANNELOUT, "    Row Size       : %d\n", m_rowSize);
    LogDebug(VB_CHANNELOUT, "    Color Depth    : %d\n", m_colorDepth);
    LogDebug(VB_CHANNELOUT, "    Outputs        : %d\n", m_outputs);
    LogDebug(VB_CHANNELOUT, "    Longest Chain  : %d\n", m_longestChain);
    LogDebug(VB_CHANNELOUT, "    Inverted Data  : %d\n", m_invertedData);

    ChannelOutput::DumpConfig();
}

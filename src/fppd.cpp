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

#ifdef PLATFORM_OSX
#include <sys/event.h>
#define USE_KQUEUE
#else
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/sysinfo.h>
#include <syscall.h>
#endif

#include <Magick++/Image.h>
#include <SDL2/SDL_events.h>
#include <curl/curl.h>
#include <magick/magick.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <getopt.h>
#include <iostream>
#include <map>
#include <set>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>

#include "common.h"
#include "log.h"
#include "mqtt.h"
#include "settings.h"

#include "CurlManager.h"
#include "Events.h"
#include "MultiSync.h"
#include "NetworkMonitor.h"
#include "OutputMonitor.h"
#include "Player.h"
#include "Plugins.h"
#include "Scheduler.h"
#include "Sequence.h"
#include "Timers.h"
#include "Warnings.h"
#include "command.h"
#include "e131bridge.h"
#include "effects.h"
#include "falcon.h"
#include "fppversion.h"
#include "gpio.h"
#include "httpAPI.h"
#include "channeloutput/ChannelOutputSetup.h"
#include "channeloutput/channeloutputthread.h"
#include "channeltester/ChannelTester.h"
#include "commands/Commands.h"
#include "mediaoutput/MediaOutputBase.h"
#include "mediaoutput/MediaOutputStatus.h"
#include "mediaoutput/mediaoutput.h"
#include "overlays/PixelOverlay.h"
#include "playlist/Playlist.h"
#include "sensors/Sensors.h"
#include "util/GPIOUtils.h"
#include "util/TmpFileGPIO.h"

#include "fppd.h"

#if defined(PLATFORM_BBB)
#include "util/BBBUtils.h"
#define PLAT_GPIO_CLASS BBBPinProvider
#elif defined(PLATFORM_PI)
#include "util/PiGPIOUtils.h"
#define PLAT_GPIO_CLASS PiGPIOPinProvider
#elif defined(GPIOD_CHIPS) && GPIOD_CHIPS > 0
#define PLAT_GPIO_CLASS NoPinCapabilitiesProvider
#else
#include "util/TmpFileGPIO.h"
#define PLAT_GPIO_CLASS TmpFilePinProvider
#endif

pid_t pid, sid;
volatile int runMainFPPDLoop = 1;
volatile bool restartFPPD = 0;

/* Prototypes for functions below */
void MainLoop(void);
void PublishStatsBackground(std::string reason);
void PublishStatsForce(std::string reason);

static int IsDebuggerPresent() {
    char buf[1024];
    int debugger_present = 0;

    int status_fd = open("/proc/self/status", O_RDONLY);
    if (status_fd == -1) {
        return 0;
    }
    ssize_t num_read = read(status_fd, buf, sizeof(buf) - 1);
    if (num_read > 0) {
        static const char TracerPid[] = "TracerPid:";
        char* tracer_pid;

        buf[num_read] = 0;
        tracer_pid = strstr(buf, TracerPid);
        if (tracer_pid) {
            debugger_present = !!atoi(tracer_pid + sizeof(TracerPid) - 1);
        }
    }
    return debugger_present;
}

// Try to attach gdb to print stack trace (Linux only).
// The sole purpose is to improve the very poor stack traces generated by backtrace() on ARM platforms
static bool dumpstack_gdb(void) {
#ifndef PLATFORM_OSX
    char pid_buf[30];
    snprintf(pid_buf, sizeof(pid_buf), "%d", getpid());
    char thread_buf[30];
    snprintf(thread_buf, sizeof(thread_buf), "(LWP %ld)", syscall(__NR_gettid));
    char name_buf[512];
    name_buf[readlink("/proc/self/exe", name_buf, 511)] = 0;

    if (IsDebuggerPresent()) {
        return false;
    }
    (void)remove("/tmp/fppd_crash.log");

    // Allow us to be traced
    // Note: Does not currently work in WSL: https://github.com/Microsoft/WSL/issues/3053
    int retval = prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);

    // Spawn helper process which will keep running when gdb is attached to main domoticz process
    pid_t intermediate_pid = fork();
    if (intermediate_pid == -1) {
        return false;
    }
    if (!intermediate_pid) {
        // Wathchdog 1: Used to kill sub processes to gdb which may hang
        pid_t timeout_pid1 = fork();
        if (timeout_pid1 == -1) {
            _Exit(1);
        }
        if (timeout_pid1 == 0) {
            int timeout = 10;
            sleep(timeout);
            _Exit(1);
        }

        // Wathchdog 2: Give up on gdb, if it still does not finish even after killing its sub processes
        pid_t timeout_pid2 = fork();
        if (timeout_pid2 == -1) {
            kill(timeout_pid1, SIGKILL);
            _Exit(1);
        }
        if (timeout_pid2 == 0) {
            int timeout = 20;
            sleep(timeout);
            _Exit(1);
        }

        // Worker: Spawns gdb
        pid_t worker_pid = fork();
        if (worker_pid == -1) {
            kill(timeout_pid1, SIGKILL);
            kill(timeout_pid2, SIGKILL);
            _Exit(1);
        }
        if (worker_pid == 0) {
            (void)remove("/tmp/fppd_crash.log");
            int fd = open("/tmp/fppd_crash.log", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
            if (fd == -1)
                _Exit(1);
            if (dup2(fd, STDOUT_FILENO) == -1)
                _Exit(1);
            if (dup2(fd, STDERR_FILENO) == -1)
                _Exit(1);
            execlp("gdb", "gdb", "--batch", "-n", "-ex", "thread apply all bt", "-ex", "info threads", "-ex", "bt", "-ex", "detach", name_buf, pid_buf, NULL);

            // If gdb failed to start, signal back
            close(fd);
            _Exit(1);
        }

        int result = 1;

        // Wait for all children to die
        while (worker_pid || timeout_pid1 || timeout_pid2) {
            int status = 0;
            // pid_t exited_pid = wait(&status);
            usleep(100000);
            // printf("pid worker_pid: %d, timeout_pid1: %d, timeout_pid2: %d\n", worker_pid, timeout_pid1, timeout_pid2);
            if (worker_pid && waitpid(worker_pid, &status, WNOHANG)) {
                worker_pid = 0;
                // printf("Status: %x, wifexited: %u, wexitstatus: %u\n", status, WIFEXITED(status), WEXITSTATUS(status));
                // printf("Sending SIGKILL to timeout_pid1\n");
                if (timeout_pid1)
                    kill(timeout_pid1, SIGKILL);
                if (timeout_pid2)
                    kill(timeout_pid2, SIGKILL);
            } else if (timeout_pid1 && waitpid(timeout_pid1, &status, WNOHANG)) {
                // Watchdog 1 timed out, attempt to recover by killing all gdb's child processes
                char tmp[128];
                timeout_pid1 = 0;
                // printf("Sending SIGKILL to worker_pid's children\n");
                if (worker_pid) {
                    snprintf(tmp, sizeof(tmp), "pkill -KILL -P %d", worker_pid);
                    int ret = system(tmp);
                }
            } else if (timeout_pid2 && waitpid(timeout_pid2, &status, WNOHANG)) {
                // Watchdog 2 timed out, give up
                timeout_pid2 = 0;
                // printf("Sending SIGKILL to worker_pid\n");
                if (worker_pid)
                    kill(worker_pid, SIGKILL);
                if (timeout_pid1)
                    kill(timeout_pid1, SIGKILL);
            }
        }
        _Exit(result); // Or some more informative status
    } else {
        int status = 0;
        pid_t res = waitpid(intermediate_pid, &status, 0);
        if (FileExists("/tmp/fppd_crash.log")) {
            std::string s = GetFileContents("/tmp/fppd_crash.log");
            if (s != "") {
                LogErr(VB_ALL, "Stack: \n%s\n", s.c_str());
                // printf("%s\n", s.c_str());
                return true;
            }
        }
    }
#endif
    return false;
}

static void handleCrash(int s) {
    static volatile bool inCrashHandler = false;
    if (inCrashHandler) {
        // need to ignore any crashes in the crash handler
        return;
    }
    inCrashHandler = true;
    int crashLog = getSettingInt("ShareCrashData", 3);
    LogErr(VB_ALL, "Crash handler called:  %d\n", s);

    if (!dumpstack_gdb()) {
        void* callstack[128];
        int i, frames = backtrace(callstack, 128);
        char** strs = backtrace_symbols(callstack, frames);
        int fd = open("/tmp/fppd_crash.log", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        for (i = 0; i < frames; i++) {
            LogErr(VB_ALL, "  %s\n", strs[i]);
        }
        for (i = 0; i < frames; i++) {
            printf("  %s\n", strs[i]);
            write(fd, strs[i], strlen(strs[i]));
            write(fd, "\n", 1);
        }
        write(fd, "\n", 1);
        close(fd);
        free(strs);
    }
    if (crashLog >= 1) {
        std::set<std::string> filenames;
        std::string mediaDir = getFPPMediaDir();
        std::string cdir = mediaDir + "/crashes";

        chdir(mediaDir.c_str());
        mkdir(cdir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        SetFilePerms(cdir, true);

        bool hasRecent = false;
        for (const auto& entry : std::filesystem::directory_iterator(cdir)) {
            filenames.insert(entry.path());
            auto ftime = entry.last_write_time();
#if defined(PLATFORM_OSX)
#if (__apple_build_version__ >= 14030022)
            auto stm = std::chrono::file_clock::to_sys(ftime);
#else
            std::time_t tt = decltype(ftime)::clock::to_time_t(ftime);
            auto stm = std::chrono::system_clock::from_time_t(tt);
#endif
#elif (__GNUC__ <= 8)
            std::time_t tt = decltype(ftime)::clock::to_time_t(ftime);
            auto stm = std::chrono::system_clock::from_time_t(tt);
#else
            auto stm = std::chrono::file_clock::to_sys(ftime);
#endif
            auto tdiff = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - stm);
            if (tdiff.count() < 60) {
                hasRecent = true;
            }
        }
        while (filenames.size() > 10) {
            std::string f = *filenames.begin();
            unlink(f.c_str());
            filenames.erase(filenames.begin());
        }

        if (!hasRecent) {
            char tbuffer[32];
            time_t rawtime;
            time(&rawtime);
            const auto timeinfo = localtime(&rawtime);
            strftime(tbuffer, sizeof(tbuffer), "%Y-%m-%d_%H-%M-%S", timeinfo);
#ifdef PLATFORM_ARMBIAN
            char sysType[] = "Armbian";
#elif defined(PLATFORM_BBB)
            char sysType[] = "BBB";
#elif defined(PLATFORM_PI)
            char sysType[] = "Pi";
#elif defined(PLATFORM_OSX)
            char sysType[] = "MacOS";
#elif defined(PLATFORM_DOCKER)
            char sysType[] = "Docker";
#elif defined(PLATFORM_DEBIAN)
            char sysType[] = "Debian";
#else
            char sysType[] = "Unknown";
#endif
            char zfName[128];
            snprintf(zfName, sizeof(zfName), "crashes/fpp-%s-%s-%s.zip", sysType, getFPPVersion(), tbuffer);

            char zName[1024];
            snprintf(zName, sizeof(zfName), "zip -r %s /tmp/fppd_crash.log", zfName);
            if (crashLog > 1) {
                strcat(zName, " settings");
                if (crashLog > 2) {
                    strcat(zName, " config tmp logs/fppd.log logs/apache2-error.log playlists");
                }
            }
            system(zName);
            SetFilePerms(zfName);
            snprintf(zName, sizeof(zfName), "curl https://dankulp.com/crashUpload/index.php -F userfile=@%s", zfName);
            system(zName);
        } else {
            LogErr(VB_ALL, "Very recent crash report found, not uploading\n");
        }
    }
    inCrashHandler = false;
    runMainFPPDLoop = 0;
    if (s != SIGQUIT && s != SIGUSR1) {
        WarningHolder::StopNotifyThread();
        WarningHolder::writeWarningsFile("['FPPD has crashed.  Check crash reports.']");
        exit(-1);
    }
}

bool setupExceptionHandlers() {
    // old sig handlers
    static bool s_savedHandlers = false;
    static struct sigaction s_handlerFPE,
        s_handlerILL,
        s_handlerBUS,
        s_handlerSEGV;

    bool ok = true;
    if (!s_savedHandlers) {
        // install the signal handler
        struct sigaction act;

        // some systems extend it with non std fields, so zero everything
        memset(&act, 0, sizeof(act));

        act.sa_handler = handleCrash;
        sigemptyset(&act.sa_mask);
        act.sa_flags = 0;

        ok &= sigaction(SIGFPE, &act, &s_handlerFPE) == 0;
        ok &= sigaction(SIGILL, &act, &s_handlerILL) == 0;
        ok &= sigaction(SIGBUS, &act, &s_handlerBUS) == 0;
        ok &= sigaction(SIGSEGV, &act, &s_handlerSEGV) == 0;
        ok &= sigaction(SIGQUIT, &act, nullptr) == 0;
        ok &= sigaction(SIGUSR1, &act, nullptr) == 0;

        struct sigaction sigchld_action;
        sigchld_action.sa_handler = SIG_DFL;
        sigchld_action.sa_flags = SA_NOCLDWAIT;
        sigemptyset(&sigchld_action.sa_mask);
        ok &= sigaction(SIGCHLD, &sigchld_action, NULL) == 0;

        if (!ok) {
            LogWarn(VB_ALL, "Failed to install our signal handler.\n");
        }
        s_savedHandlers = true;
    } else if (s_savedHandlers) {
        // uninstall the signal handler
        ok &= sigaction(SIGFPE, &s_handlerFPE, NULL) == 0;
        ok &= sigaction(SIGILL, &s_handlerILL, NULL) == 0;
        ok &= sigaction(SIGBUS, &s_handlerBUS, NULL) == 0;
        ok &= sigaction(SIGSEGV, &s_handlerSEGV, NULL) == 0;
        if (!ok) {
            LogWarn(VB_ALL, "Failed to install default signal handlers.\n");
        }
        s_savedHandlers = false;
    }
    return ok;
}

inline void WriteRuntimeInfoFile(Json::Value v) {
    std::string filename = FPP_DIR_MEDIA("/fpp-info.json");
    Json::Value systems = v["systems"];

    // If there is no network, then delete the file
    // to avoid confusion.
    if (systems.empty()) {
        const int ok = remove(filename.c_str());
        if (!ok) {
            LogWarn(VB_ALL, "Failed to remove file: %s\n", filename.c_str());
        }
        LogInfo(VB_ALL, "Not creating %s as no IP addresses found\n", filename.c_str());
        return;
    }

    std::string addresses = "";
    for (int x = 0; x < systems.size(); x++) {
        if (addresses != "") {
            addresses += ",";
        }
        addresses += systems[x]["address"].asString();
    }
    Json::Value local = systems[0];
    local.removeMember("address");
    local["addresses"] = addresses;

    SaveJsonToFile(local, filename);
}

static void initCapeFromFile(const std::string& f) {
    if (FileExists(f)) {
        Json::Value root;
        if (LoadJsonFromFile(f, root)) {
            // if there are sources of sensor data, get them loaded first
            if (root.isMember("sensorSources")) {
                Sensors::INSTANCE.addSensorSources(root["sensorSources"]);
            }
            Sensors::INSTANCE.addSensors(root["sensors"]);
        }
    }
}
static void initCape() {
    initCapeFromFile(FPP_DIR_MEDIA("/tmp/cape-sensors.json"));
}

void usage(char* appname) {
    printf("Usage: %s [OPTION...]\n"
           "\n"
           "fppd is the Falcon Player daemon.\n"
           "\n"
           "Options:\n"
           "  -f, --foreground              - Don't daemonize the application.  In the\n"
           "                                  foreground, all logging will be on the\n"
           "                                  console instead of the log file\n"
           "  -d, --daemonize               - Daemonize even if the config file says not to.\n"
           "  -v, --volume VOLUME           - Set a volume (over-written by config file)\n"
           "  -m, --mode MODE               - Set the mode: \"player\", \"bridge\",\n"
           "                                  \"master\", or \"remote\"\n"
           "  -l, --log-file FILENAME       - Set the log file\n"
           "  -H  --detect-hardware         - Detect Falcon hardware on SPI port\n"
           "  -C  --configure-hardware      - Configured detected Falcon hardware on SPI\n"
           "  -h, --help                    - This menu.\n"
           "      --log-level LEVEL         - Set the global log output level (all loggers):\n"
           "                                  \"info\", \"warn\", \"debug\", \"excess\")\n"
           "      --log-level LEVEL:logger  - Set the loger level for one or more loggers.\n"
           "                                  each level should be spereated by semicolon\n"
           "                                  with one or more loggers seperated by comma\n"
           "                                  example: debug:schedule,player;excess:mqtt \n"
           "                                  valid loggers are: \n"
           "                                    ChannelData - channel data itself\n"
           "                                    ChannelOut  - channel output code\n"
           "                                    Command     - command processing\n"
           "                                    Control     - Control socket debugging\n"
           "                                    E131Bridge  - E1.31 bridge\n"
           "                                    Effect      - Effects sequences\n"
           "                                    General     - general messages\n"
           "                                    GPIO        - GPIO Input handling\n"
           "                                    HTTP        - HTTP API requests\n"
           "                                    MediaOut    - Media file handling\n"
           "                                    Playlist    - Playlist handling\n"
           "                                    Plugin      - Plugin handling\n"
           "                                    Schedule    - Playlist scheduling\n"
           "                                    Sequence    - Sequence parsing\n"
           "                                    Setting     - Settings parsing\n"
           "                                    Sync        - Player/Remote Synchronization\n"
           "                                  The default logging is read from settings\n",
           appname);
}
extern SettingsConfig settings;

int parseArguments(int argc, char** argv) {
    char* s = NULL;
    int c;

    SetSetting("logFile", FPP_FILE_LOG);

    while (1) {
        int this_option_optind = optind ? optind : 1;
        int option_index = 0;
        static struct option long_options[] = {
            { "displayvers", no_argument, 0, 'V' },
            { "foreground", no_argument, 0, 'f' },
            { "daemonize", no_argument, 0, 'd' },
            { "restarted", no_argument, 0, 'r' },
            { "volume", required_argument, 0, 'v' },
            { "mode", required_argument, 0, 'm' },
            { "log-file", required_argument, 0, 'l' },
            { "playlist", required_argument, 0, 'p' },
            { "position", required_argument, 0, 'P' },
            { "detect-hardware", no_argument, 0, 'H' },
            { "detect-piface", no_argument, 0, 4 },
            { "configure-hardware", no_argument, 0, 'C' },
            { "help", no_argument, 0, 'h' },
            { "log-level", required_argument, 0, 2 },
            { 0, 0, 0, 0 }
        };

        c = getopt_long(argc, argv, "Vfdrv:m:l:p:P:HCh",
                        long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'V':
            printVersionInfo();
            exit(0);
        case 2: // log-level
            if (SetLogLevelComplex(optarg)) {
                std::cout << FPPLogger::INSTANCE.GetLogLevelString() << std::endl;
                LogInfo(VB_SETTING, "Log Level set to %d (%s)\n", FPPLogger::INSTANCE.MinimumLogLevel(), optarg);
            }
            break;
        case 'f': // foreground
            SetSetting("daemonize", 0);
            break;
        case 'd': // daemonize
            SetSetting("daemonize", 1);
            break;
        case 'r': // restarted
            SetSetting("restarted", 1);
            break;
        case 'p': // playlist
            SetSetting("resumePlaylist", optarg);
            break;
        case 'P': // position
            SetSetting("resumePosition", atoi(optarg));
            break;
        case 'v': // volume
            setVolume(atoi(optarg));
            break;
        case 'm': // mode
            if (strcmp(optarg, "player") == 0)
                settings.fppMode = PLAYER_MODE;
            else if (strcmp(optarg, "master") == 0) {
                settings.fppMode = PLAYER_MODE;
                SetSetting("MultiSyncEnabled", 1);
            } else if (strcmp(optarg, "remote") == 0)
                settings.fppMode = REMOTE_MODE;
            else {
                fprintf(stderr, "Error parsing mode\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 'l': // log-file
            SetSetting("logFile", optarg);
            break;
        case 'H': // Detect Falcon hardware
        case 'C': // Configure Falcon hardware
            PinCapabilities::InitGPIO("FPPD", new PLAT_GPIO_CLASS());
            SetLogFile("");
            FPPLogger::INSTANCE.Settings.level = LOG_DEBUG;
            if (DetectFalconHardware((c == 'C') ? 1 : 0))
                exit(1);
            else
                exit(0);
            break;
        case 'h': // help
            usage(argv[0]);
            exit(EXIT_SUCCESS);
            break;
        default:
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    SetLogFile(getSetting("logFile").c_str(), !getSettingInt("daemonize"));

    return 0;
}

int main(int argc, char* argv[]) {
    setupExceptionHandlers();
    FPPLogger::INSTANCE.Init();
    LoadSettings(argv[0]);

    curl_global_init(CURL_GLOBAL_ALL);

    Magick::InitializeMagick(NULL);

    // Parse our arguments first, override any defaults
    parseArguments(argc, argv);

    // Check to see if we were restarted and should skip sending blanking data at startup
    if (FileExists(FPP_DIR_MEDIA("/tmp/fppd_restarted"))) {
        unlink(FPP_DIR_MEDIA("/tmp/fppd_restarted").c_str());
        SetSetting("restarted", 1);
    }

    if (loggingToFile())
        logVersionInfo();

    // Start functioning
    if (getSettingInt("daemonize")) {
        CreateDaemon();
    }
    PinCapabilities::InitGPIO("FPPD", new PLAT_GPIO_CLASS());

    std::srand(std::time(nullptr));

    CommandManager::INSTANCE.Init();
    if (getSetting("MQTTHost") != "") {
        LogInfo(VB_GENERAL, "Creating MQTT\n");
        mqtt = new MosquittoClient(getSetting("MQTTHost").c_str(), getSettingInt("MQTTPort"), getSetting("MQTTPrefix").c_str());

        if (!mqtt || !mqtt->Init(getSetting("MQTTUsername").c_str(), getSetting("MQTTPassword").c_str(), getSetting("MQTTCaFile").c_str())) {
            LogWarn(VB_CONTROL, "MQTT Init failed. Starting without MQTT. -- Maybe MQTT host doesn't resolve\n");
        } else {
            Events::AddEventHandler(mqtt);
        }
    }

    std::function<void(const std::string&, const std::string&)>
        fppd_callback = [](const std::string& topic_in,
                           const std::string& payload) {
            LogDebug(VB_CONTROL, "System Callback for %s\n", topic_in.c_str());

            if (0 == topic_in.compare(topic_in.length() - 5, 5, "/stop")) {
                LogInfo(VB_CONTROL, "Stopping FPP for request:   %s\n", topic_in.c_str());
                ShutdownFPPD(false);
            } else if (0 == topic_in.compare(topic_in.length() - 8, 8, "/restart")) {
                LogInfo(VB_CONTROL, "Restarting FPP for request:   %s\n", topic_in.c_str());
                ShutdownFPPD(true);
            } else {
                LogWarn(VB_CONTROL, "Invalid request in fppd_callback  %s\n", topic_in.c_str());
            }
        };
    std::function<void(const std::string&, const std::string&)>
        system_callback = [](const std::string& topic_in,
                             const std::string& payload) {
            LogDebug(VB_CONTROL, "System Callback for %s\n", topic_in.c_str());
            std::string rc = "";

            if (0 == topic_in.compare(topic_in.length() - 9, 9, "/shutdown")) {
                LogInfo(VB_CONTROL, "Shutting down OS for request:   %s\n", topic_in.c_str());
                urlGet("http://localhost/api/system/shutdown", rc);
            } else if (0 == topic_in.compare(topic_in.length() - 8, 8, "/restart")) {
                LogInfo(VB_CONTROL, "Restarting FPP for request:   %s\n", topic_in.c_str());
                urlGet("http://localhost/api/system/reboot", rc);
            } else {
                LogWarn(VB_CONTROL, "Invalid  request in mqtt_system_callback  %s\n", topic_in.c_str());
            }
        };

    Events::AddCallback("/system/fppd/#", fppd_callback);
    Events::AddCallback("/system/shutdown", system_callback);
    Events::AddCallback("/system/restart", system_callback);

    WarningHolder::StartNotifyThread();

    LogInfo(VB_GENERAL, "Creating Scheduler, Playlist, and Sequence\n");
    scheduler = new Scheduler();
    sequence = new Sequence();
    LogInfo(VB_GENERAL, "Creation of Scheduler, Playlist, and Sequence Complete\n");

    if (!MultiSync::INSTANCE.Init()) {
        exit(EXIT_FAILURE);
    }

    Sensors::INSTANCE.DetectHWSensors();
    initCape();

    if (FileExists(FPP_DIR_CONFIG("/sensors.json"))) {
        Json::Value root;
        if (LoadJsonFromFile(FPP_DIR_CONFIG("/sensors.json"), root)) {
            Sensors::INSTANCE.addSensors(root["sensors"]);
        }
    }

    Player::INSTANCE.Init();
    PluginManager::INSTANCE.init();

    InitMediaOutput();
    PixelOverlayManager::INSTANCE.Initialize();
    InitializeChannelOutputs();
    PluginManager::INSTANCE.loadUserPlugins();

    if (!getSettingInt("restarted")) {
        sequence->SendBlankingData();
    }
    InitEffects();
    ChannelTester::INSTANCE.RegisterCommands();

    WriteRuntimeInfoFile(multiSync->GetSystems(true, false));

    CommandManager::INSTANCE.TriggerPreset("FPPD_STARTED");

    MainLoop();
    // DISABLED: Stats collected while fppd is shutting down
    // incomplete and cause problems with summary
    // PublishStatsForce("Shutdown"); // not background

    CommandManager::INSTANCE.TriggerPreset("FPPD_STOPPED");

    // turn off processing of events so we don't get
    // events while we are shutting down
    Events::PrepareForShutdown();

    CleanupMediaOutput();
    CloseEffects();
    CloseChannelOutputs();
    CommandManager::INSTANCE.Cleanup();
    MultiSync::INSTANCE.ShutdownSync();
    PluginManager::INSTANCE.Cleanup();
    GPIOManager::INSTANCE.Cleanup();

    delete scheduler;
    delete sequence;
    runMainFPPDLoop = -1;
    Sensors::INSTANCE.Close();

    WarningHolder::StopNotifyThread();

    if (mqtt) {
        Events::RemoveEventHandler(mqtt);
        delete mqtt;
    }

    MagickLib::DestroyMagick();
    curl_global_cleanup();
    std::string logLevelString = FPPLogger::INSTANCE.GetLogLevelString();

    CloseCommand();
    CloseOpenFiles();

    WarningHolder::clearWarningsFile();

    if (restartFPPD) {
        LogInfo(VB_GENERAL, "Performing Restart.\n");
        remove(FPP_DIR_MEDIA("/fpp-info.json").c_str());

        if ((Player::INSTANCE.GetStatus() == FPP_STATUS_PLAYLIST_PLAYING) &&
            (Player::INSTANCE.WasScheduled())) {
            std::string playlist = Player::INSTANCE.GetPlaylistName();
            std::string position = std::to_string(Player::INSTANCE.GetPosition() - 1);
            execlp(getFPPDDir("/src/fppd").c_str(), getFPPDDir("/src/fppd").c_str(), getSettingInt("daemonize") ? "-d" : "-f", "--log-level", logLevelString.c_str(), "-r", "-p", playlist.c_str(), "-P", position.c_str(), NULL);
        } else {
            execlp(getFPPDDir("/src/fppd").c_str(), getFPPDDir("/src/fppd").c_str(), getSettingInt("daemonize") ? "-d" : "-f", "--log-level", logLevelString.c_str(), "-r", NULL);
        }
    }

    return 0;
}

void ShutdownFPPDCallback(bool restart) {
    LogInfo(VB_GENERAL, "Shutting down main loop.\n");

    restartFPPD = restart;
    runMainFPPDLoop = 0;
}

void MainLoop(void) {
    RegisterShutdownHandler(ShutdownFPPDCallback);

    PlaylistStatus prevFPPstatus = FPP_STATUS_IDLE;
    int sleepms = 50;
    int publishCounter = 200; // about 40 seconds after boot
    std::string publishReason("Startup");
    std::map<int, std::function<bool(int)>> callbacks;

    LogDebug(VB_GENERAL, "MainLoop()\n");

    int sock = Command_Initialize();
    LogDebug(VB_GENERAL, "Command socket: %d\n", sock);
    if (sock >= 0) {
        callbacks[sock] = [](int i) {
            CommandProc();
            return false;
        };
    }

    sock = multiSync->GetControlSocket();
    LogDebug(VB_GENERAL, "Multisync socket: %d\n", sock);
    if (sock >= 0) {
        callbacks[sock] = [](int i) {
            multiSync->ProcessControlPacket();
            return false;
        };
    }
    if (getFPPmode() & PLAYER_MODE) {
        scheduler->CheckIfShouldBePlayingNow();
        if (getSettingInt("alwaysTransmit")) {
            StartChannelOutputThread();
        }
    }
    Bridge_Initialize(callbacks);

    APIServer apiServer;
    apiServer.Init();

    OutputMonitor::INSTANCE.Initialize(callbacks);
    GPIOManager::INSTANCE.Initialize(callbacks);
    PluginManager::INSTANCE.addControlCallbacks(callbacks);
    NetworkMonitor::INSTANCE.Init(callbacks);
    Sensors::INSTANCE.Init(callbacks);

    static const int MAX_EVENTS = 20;
#ifdef USE_KQUEUE
    int epollf = kqueue();
    std::vector<struct kevent> callbackKevents;
    size_t cur = 0;
    for (auto& a : callbacks) {
        struct kevent event;
        event.ident = a.first;
        event.filter = EVFILT_READ;
        event.flags = EV_ADD | EV_ENABLE | EV_CLEAR;
        event.fflags = 0;
        event.data = 0;
        event.udata = (void*)cur;
        cur++;
        callbackKevents.push_back(event);
    }
    kevent(epollf, &callbackKevents[0], callbackKevents.size(), nullptr, 0, nullptr);
    struct kevent events[MAX_EVENTS];
#else
    int epollf = epoll_create1(EPOLL_CLOEXEC);
    for (auto& a : callbacks) {
        epoll_event event;
        memset(&event, 0, sizeof(event));
        event.events = EPOLLIN;
        event.data.fd = a.first;
        int rc = epoll_ctl(epollf, EPOLL_CTL_ADD, a.first, &event);
        if (rc == -1) {
            LogWarn(VB_GENERAL, "epoll_ctl() failed for socket: %d  %s\n", a.first, strerror(errno));
        }
    }
    epoll_event events[MAX_EVENTS];
#endif
    multiSync->Discover();

    Events::Ready();

    LogInfo(VB_GENERAL, "Starting main processing loop\n");

    int lowestLogLevel = FPPLogger::INSTANCE.MinimumLogLevel();
    if (lowestLogLevel == LOG_EXCESSIVE)
        WarningHolder::AddWarning(EXCESSIVE_LOG_LEVEL_WARNING);
    else if (lowestLogLevel == LOG_DEBUG)
        WarningHolder::AddWarning(DEBUG_LOG_LEVEL_WARNING);

    memset(events, 0, sizeof(events));
    int idleCount = 0;
    bool alwaysTransmit = (bool)getSettingInt("alwaysTransmit");

    while (runMainFPPDLoop) {
#ifdef USE_KQUEUE
        struct timespec timeoutStruct = { 0, sleepms * 1000000 };
        int epollresult = kevent(epollf, nullptr, 0, events, MAX_EVENTS, &timeoutStruct);
#else
        int epollresult = epoll_wait(epollf, events, MAX_EVENTS, sleepms);
#endif
        if (epollresult < 0) {
            if (errno == EINTR) {
                // We get interrupted when media players finish
                continue;
            } else {
                LogErr(VB_GENERAL, "Main epoll() failed: %s\n", strerror(errno));
                runMainFPPDLoop = 0;
                continue;
            }
        }
        bool pushBridgeData = false;
        if (epollresult > 0) {
            for (int x = 0; x < epollresult; x++) {
#ifdef USE_KQUEUE
                pushBridgeData |= callbacks[events[x].ident](events[x].ident);
#else
                pushBridgeData |= callbacks[events[x].data.fd](events[x].data.fd);
#endif
            }
        }
        // Check to see if we need to start up the output thread.
        if ((!ChannelOutputThreadIsRunning()) &&
            ((PixelOverlayManager::INSTANCE.hasActiveOverlays()) ||
             (ChannelTester::INSTANCE.Testing()) ||
             (alwaysTransmit) || sequence->hasBridgeData() ||
             pushBridgeData)) {
            StartChannelOutputThread();
        }

        if (getFPPmode() & PLAYER_MODE) {
            if (Player::INSTANCE.IsPlaying()) {
                if (prevFPPstatus == FPP_STATUS_IDLE) {
                    sleepms = 10;
                }

                // Check again here in case PlayListPlayingInit
                // didn't find anything and put us back to IDLE
                if (Player::INSTANCE.IsPlaying()) {
                    Player::INSTANCE.Process();
                }
            }

            int reactivated = 0;
            if (Player::INSTANCE.GetStatus() == FPP_STATUS_IDLE) {
                if ((prevFPPstatus == FPP_STATUS_PLAYLIST_PLAYING) ||
                    (prevFPPstatus == FPP_STATUS_PLAYLIST_PAUSED) ||
                    (prevFPPstatus == FPP_STATUS_STOPPING_GRACEFULLY) ||
                    (prevFPPstatus == FPP_STATUS_STOPPING_GRACEFULLY_AFTER_LOOP)) {
                    Player::INSTANCE.Cleanup();

                    if (Player::INSTANCE.GetForceStopped())
                        scheduler->CheckIfShouldBePlayingNow(0, Player::INSTANCE.GetScheduleEntry());
                    else
                        scheduler->CheckIfShouldBePlayingNow();

                    if (Player::INSTANCE.GetStatus() != FPP_STATUS_IDLE)
                        reactivated = 1;
                    else
                        sleepms = 50;
                }
            }

            if (reactivated)
                prevFPPstatus = FPP_STATUS_IDLE;
            else
                prevFPPstatus = Player::INSTANCE.GetStatus();

            scheduler->ScheduleProc();
        } else if (getFPPmode() == REMOTE_MODE) {
            if (mediaOutputStatus.status == MEDIAOUTPUTSTATUS_PLAYING) {
                Player::INSTANCE.ProcessMedia();
            }
        }
        if (pushBridgeData) {
            ForceChannelOutputNow();
        }
        bool doPing = false;
        if (!epollresult) {
            idleCount++;
            if (idleCount >= 20) {
                doPing = true;
            }
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                // for now, discard event, but at least the queue doesn't grow
            }
        } else if (idleCount > 0) {
            doPing = true;
        } else {
            idleCount--;
            if (idleCount < -20) {
                doPing = true;
            }
        }
        if (doPing) {
            idleCount = 0;
            multiSync->PeriodicPing();
            if (--publishCounter < 0) {
                PublishStatsBackground(publishReason);
                // counting down is less CPU then the time check every cycle
                publishCounter = 60480;
                publishReason = "normal";
            }
        }
        Timers::INSTANCE.fireTimers();
        CurlManager::INSTANCE.processCurls();
        GPIOManager::INSTANCE.CheckGPIOInputs();
    }
    close(epollf);

    LogInfo(VB_GENERAL, "Stopping channel output thread.\n");
    StopChannelOutputThread();

    Bridge_Shutdown();
    LogInfo(VB_GENERAL, "Main Loop complete, shutting down.\n");
}

void CreateDaemon(void) {
    /* Fork and terminate parent so we can run in the background */
    /* Fork off the parent process */
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    /* If we got a good PID, then
        we can exit the parent process. */
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* Change the file mode mask */
    umask(0);

    /* Create a new SID for the child process */
    sid = setsid();
    if (sid < 0) {
        /* Log any failures here */
        exit(EXIT_FAILURE);
    }

    /* Fork a second time to get rid of session leader */
    /* Fork off the parent process */
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    /* If we got a good PID, then
      we can exit the parent process. */
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* Close out the standard file descriptors */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

void PublishStatsForce(std::string reason) {
    std::string result("");
    urlPost("http://localhost/api/statistics/usage?reason=" + reason, "", result);
    LogInfo(VB_GENERAL, "Publishing statistics because of \"%s\" = %s\n", reason.c_str(), result.c_str());
}

void PublishStatsBackground(std::string reason) {
    // No need to publish more than once every 10 days if fppd is up that long
    static auto lastPublish = std::chrono::system_clock::now() - std::chrono::hours(10 * 24);
    auto now_ts = std::chrono::system_clock::now();
    auto hours = std::chrono::duration_cast<std::chrono::hours>(now_ts - lastPublish);

    // Check 10 days
    if (hours.count() >= (10 * 24)) {
        lastPublish = now_ts;
        if (getSetting("statsPublish") == "Enabled") {
            std::thread t(PublishStatsForce, reason);
            t.detach();
        } else {
            LogInfo(VB_GENERAL, "Not Publishing statistics as mode is '%s'\n", getSetting("statsPublish").c_str());
        }
    } else {
        LogDebug(VB_GENERAL, "PublishStats called, but not been 10 days yet.\n");
    }
}

#include "audio_alert.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr const char *kFatigueMp3Path = "/home/elf/workspace/proj/integrated-inspection/MP3/fatigue.mp3";
constexpr const char *kDefaultAlsaDevice = "plughw:1,0";

bool fileExists(const std::string &path)
{
    return !path.empty() && access(path.c_str(), R_OK) == 0;
}

std::string resolveFatigueMp3Path()
{
    const char *envPath = std::getenv("FATIGUE_ALERT_MP3");
    const std::vector<std::string> candidates = {
        envPath ? std::string(envPath) : std::string(),
        kFatigueMp3Path,
        "/userdata/sdcard/workspace/proj/integrated-inspection/MP3/fatigue.mp3",
        "MP3/fatigue.mp3",
        "../MP3/fatigue.mp3",
    };

    for (const std::string &path : candidates) {
        if (fileExists(path)) {
            return path;
        }
    }
    return kFatigueMp3Path;
}

int runPlayer(const std::vector<std::string> &args)
{
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (const std::string &arg : args) {
        argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], argv.data());
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

bool playOnce(const std::string &path)
{
    const char *alsaEnv = std::getenv("FATIGUE_ALERT_ALSA_DEVICE");
    const std::string alsaDevice = alsaEnv && alsaEnv[0] != '\0'
        ? std::string(alsaEnv)
        : std::string(kDefaultAlsaDevice);
    const char *volumeEnv = std::getenv("FATIGUE_ALERT_VOLUME_DB");
    const std::string volumeFilter = std::string("volume=") +
        (volumeEnv && volumeEnv[0] != '\0' ? std::string(volumeEnv) : std::string("18")) + "dB";
    const std::string wavPath = "/tmp/integrated_inspection_fatigue_alert_" +
        std::to_string(static_cast<long long>(getpid())) + ".wav";

    if (runPlayer({"ffmpeg", "-y", "-loglevel", "quiet", "-i", path,
                   "-af", volumeFilter, "-ar", "48000", "-ac", "1",
                   "-sample_fmt", "s16", "-map_metadata", "-1", "-bitexact",
                   "-f", "wav", wavPath}) == 0) {
        const bool ok = runPlayer({"speaker-test", "-D", alsaDevice, "-c", "2",
                                   "-t", "wav", "-w", wavPath, "-l", "1"}) == 0;
        if (!ok) {
            runPlayer({"aplay", "-D", alsaDevice, "--buffer-size=131072",
                       "--period-size=32768", wavPath});
        }
        unlink(wavPath.c_str());
        if (ok) {
            return true;
        }
    } else {
        unlink(wavPath.c_str());
    }

    const std::vector<std::vector<std::string>> players = {
        {"mpv", "--no-video", "--really-quiet", "--audio-device=alsa/" + alsaDevice, path},
        {"ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet", path},
        {"mpg123", "-q", path},
        {"mpv", "--no-video", "--really-quiet", path},
        {"gst-play-1.0", "-q", path},
        {"cvlc", "--play-and-exit", "--quiet", path},
    };

    for (const auto &player : players) {
        const int code = runPlayer(player);
        if (code == 0) {
            return true;
        }
    }
    return false;
}

std::atomic<bool> g_fatigueAlertPlaying{false};

} // namespace

namespace AudioAlert {

void PlayFatigueWarningAsync()
{
    if (g_fatigueAlertPlaying.exchange(true)) {
        return;
    }

    const std::string path = resolveFatigueMp3Path();
    std::thread([path]() {
        if (!fileExists(path)) {
            std::fprintf(stderr, "[WARN] fatigue audio file not found: %s\n", path.c_str());
            g_fatigueAlertPlaying.store(false);
            return;
        }

        for (int i = 0; i < 2; ++i) {
            if (!playOnce(path)) {
                std::fprintf(stderr, "[WARN] failed to play fatigue audio: %s\n", path.c_str());
                break;
            }
            if (i == 0) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        g_fatigueAlertPlaying.store(false);
    }).detach();
}

} // namespace AudioAlert

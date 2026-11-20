/*
 *  FreeSynd - a remake of the classic Bullfrog game "Syndicate".
 *
 *   This program is free software: you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, either version 3 of the
 *  License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "fs-engine/system/recorder.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>

#include "fs-utils/log/log.h"
#include "fs-engine/sound/audio.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#define NULL_DEVICE "NUL"
#else
#define NULL_DEVICE "/dev/null"
#endif

namespace fs_eng {

//! Frames waiting to be sent to ffmpeg : when there are more, frames are skipped (and the next ones repeated)
const size_t kMaxQueuedFrames = 20;
//! A frame is repeated at most this number of times (after a long pause of the game)
const int kMaxRepeatedFrames = Recorder::kFramesPerSecond * 10;
//! The video is not wider than this
const int kMaxVideoWidth = 1920;

/*!
 * Commands are run by the shell : on Windows, the whole command must be in quotes
 * when the program's arguments have quotes.
 */
static std::string shellCommand(const std::string &command) {
#ifdef _WIN32
    return "\"" + command + "\"";
#else
    return command;
#endif
}

Recorder::~Recorder() {
    stop();
    waitForVideos();
}

bool Recorder::start(const std::string &folder, Audio *pAudio) {
    if (recording_) {
        return true;
    }
    if (std::system(shellCommand("ffmpeg -version >" NULL_DEVICE " 2>&1").c_str()) != 0) {
        FSERR(Log::k_FLG_GFX, "Recorder", "start", ("Cannot record : ffmpeg was not found in the PATH"))
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(folder, error);
    std::time_t now = std::time(nullptr);
    char name[64];
    std::strftime(name, sizeof(name), "freesynd_%Y%m%d_%H%M%S", std::localtime(&now));
    std::filesystem::path base = std::filesystem::path(folder) / name;
    outputPath_ = base.string() + ".mp4";
    videoPath_ = base.string() + ".video.mp4";
    soundPath_ = base.string() + ".sound.raw";

    pAudio_ = pAudio;
    pSoundFile_ = nullptr;
    if (pAudio_ != nullptr && pAudio_->isInitialized()) {
        pSoundFile_ = std::fopen(soundPath_.c_str(), "wb");
    }
    width_ = 0;
    height_ = 0;
    nbFrames_ = 0;
    startTicks_ = 0;
    stopWriting_ = false;
    frames_.clear();
    sound_.clear();
    recording_ = true;
    // ffmpeg is started with the first frame, when the size of the screen is known
    FSINFO(Log::k_FLG_GFX, "Recorder", "start", ("Recording in %s", outputPath_.c_str()))
    return true;
}

bool Recorder::needsFrame(uint32_t ticks) {
    if (!recording_) {
        return false;
    }
    if (pVideoPipe_ == nullptr) {
        return true;
    }
    int64_t expected = static_cast<int64_t>(ticks - startTicks_) * kFramesPerSecond / 1000 + 1;
    return expected > nbFrames_;
}

void Recorder::addFrame(const uint8_t *pPixels, int width, int height, uint32_t ticks) {
    if (!recording_) {
        return;
    }
    if (pVideoPipe_ == nullptr) {
        // The first frame : starts ffmpeg, then the sound
        width_ = width;
        height_ = height;
        std::string command = std::format(
            "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgba -s {}x{} -framerate {} -i - "
            "-vf \"scale='trunc(min({},iw)/2)*2':-2\" -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p \"{}\"",
            width, height, kFramesPerSecond, kMaxVideoWidth, videoPath_);
        pVideoPipe_ = popen(shellCommand(command).c_str(), "wb");
        if (pVideoPipe_ == nullptr) {
            FSERR(Log::k_FLG_GFX, "Recorder", "addFrame", ("Cannot start ffmpeg"))
            recording_ = false;
            if (pSoundFile_) {
                std::fclose(pSoundFile_);
                pSoundFile_ = nullptr;
                std::filesystem::remove(soundPath_);
            }
            return;
        }
        startTicks_ = ticks;
        if (pSoundFile_ && !pAudio_->startCapture([this](const uint8_t *pSamples, int length) { onSamples(pSamples, length); },
                                                  &soundFrequency_, &soundChannels_, &soundFormat_)) {
            std::fclose(pSoundFile_);
            pSoundFile_ = nullptr;
            std::filesystem::remove(soundPath_);
        }
        writer_ = std::thread(&Recorder::writeLoop, this);
    } else if (width != width_ || height != height_) {
        // The video has the size of the screen when it started
        FSINFO(Log::k_FLG_GFX, "Recorder", "addFrame", ("The size of the screen has changed : the recording stops"))
        stop();
        return;
    }

    int64_t expected = static_cast<int64_t>(ticks - startTicks_) * kFramesPerSecond / 1000 + 1;
    int count = static_cast<int>(std::min<int64_t>(expected - nbFrames_, kMaxRepeatedFrames));
    if (count <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (frames_.size() >= kMaxQueuedFrames) {
        // ffmpeg is late : this frame is skipped, the next one will be repeated
        return;
    }
    frames_.emplace_back(std::vector<uint8_t>(pPixels, pPixels + static_cast<size_t>(width) * height * 4), count);
    nbFrames_ += count;
    wakeUp_.notify_one();
}

void Recorder::onSamples(const uint8_t *pSamples, int length) {
    std::lock_guard<std::mutex> lock(mutex_);
    sound_.insert(sound_.end(), pSamples, pSamples + length);
    // The writer is woken up by the frames : no need to do it for each bit of sound
}

void Recorder::writeLoop() {
    std::deque<std::pair<std::vector<uint8_t>, int>> frames;
    std::vector<uint8_t> sound;
    bool failed = false;
    while (true) {
        bool stopping;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wakeUp_.wait_for(lock, std::chrono::milliseconds(100), [this] { return stopWriting_ || !frames_.empty(); });
            frames.swap(frames_);
            sound.swap(sound_);
            stopping = stopWriting_;
        }
        if (pSoundFile_ && !sound.empty()) {
            std::fwrite(sound.data(), 1, sound.size(), pSoundFile_);
        }
        sound.clear();
        for (const auto &[pixels, count] : frames) {
            for (int i = 0; i < count && !failed; i++) {
                if (std::fwrite(pixels.data(), 1, pixels.size(), pVideoPipe_) != pixels.size()) {
                    FSERR(Log::k_FLG_GFX, "Recorder", "writeLoop", ("ffmpeg stopped : the video can't be written"))
                    failed = true;
                }
            }
        }
        frames.clear();
        if (stopping) {
            return;
        }
    }
}

void Recorder::stop() {
    if (!recording_) {
        return;
    }
    recording_ = false;
    if (pVideoPipe_ == nullptr) {
        // Nothing was recorded
        if (pSoundFile_) {
            std::fclose(pSoundFile_);
            pSoundFile_ = nullptr;
            std::filesystem::remove(soundPath_);
        }
        return;
    }

    bool withSound = pSoundFile_ != nullptr;
    if (withSound) {
        pAudio_->stopCapture();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopWriting_ = true;
        wakeUp_.notify_one();
    }
    writer_.join();

    // ffmpeg finishes the video, then the sound is added, while the game goes on
    FILE *pVideoPipe = pVideoPipe_;
    FILE *pSoundFile = pSoundFile_;
    pVideoPipe_ = nullptr;
    pSoundFile_ = nullptr;
    std::string mux = std::format(
        "ffmpeg -y -loglevel error -i \"{}\" -f {} -ar {} -ac {} -i \"{}\" -c:v copy -c:a aac -b:a 192k -shortest \"{}\"",
        videoPath_, soundFormat_, soundFrequency_, soundChannels_, soundPath_, outputPath_);
    std::string videoPath = videoPath_, soundPath = soundPath_, outputPath = outputPath_;
    finishers_.emplace_back([=]() {
        int videoResult = pclose(pVideoPipe);
        std::error_code error;
        if (pSoundFile) {
            std::fclose(pSoundFile);
        }
        if (videoResult != 0) {
            FSERR(Log::k_FLG_GFX, "Recorder", "stop", ("ffmpeg could not encode the video %s", videoPath.c_str()))
            return;
        }
        if (!withSound) {
            std::filesystem::rename(videoPath, outputPath, error);
        } else if (std::system(shellCommand(mux).c_str()) == 0) {
            std::filesystem::remove(videoPath, error);
            std::filesystem::remove(soundPath, error);
        } else {
            FSERR(Log::k_FLG_GFX, "Recorder", "stop", ("ffmpeg could not add the sound to %s", videoPath.c_str()))
            return;
        }
        FSINFO(Log::k_FLG_GFX, "Recorder", "stop", ("Video saved in %s", outputPath.c_str()))
    });
}

void Recorder::waitForVideos() {
    for (std::thread &finisher : finishers_) {
        if (finisher.joinable()) {
            finisher.join();
        }
    }
    finishers_.clear();
}

}

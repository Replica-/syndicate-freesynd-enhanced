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

#ifndef SYSTEM_RECORDER_H_
#define SYSTEM_RECORDER_H_

#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "fs-utils/misc/singleton.h"

namespace fs_eng {

class Audio;

/*!
 * Records the game in a MP4 video with its sound, using ffmpeg (it must be in the PATH).
 *
 * The screen is captured at kFramesPerSecond and sent to ffmpeg, which encodes it while
 * the game goes on. The final mix of the audio is written in a file at the same time.
 * When the recording stops, ffmpeg puts both in the video, in the background.
 * Frames are repeated when the game draws slower than the video so the video keeps
 * in time with the sound.
 */
class Recorder : public Singleton<Recorder> {
public:
    //! Frames per second of the video
    static const int kFramesPerSecond = 30;

    Recorder() {}
    ~Recorder();

    /*!
     * Starts recording in a new file of the given folder, with the sound from pAudio
     * (can be null). Returns false if the recording can't start.
     */
    bool start(const std::string &folder, Audio *pAudio);
    //! Stops the recording : the video is finished in the background (see waitForVideos())
    void stop();
    bool isRecording() { return recording_; }
    //! Waits until the videos being finished are ready (when quitting)
    void waitForVideos();
    //! The last file recorded or being recorded
    const std::string &lastFile() { return outputPath_; }

    //! Returns true if a frame of the screen must be captured now
    bool needsFrame(uint32_t ticks);
    /*!
     * Adds a frame of the screen, in RGBA pixels. If the size of the screen changes,
     * the recording stops.
     */
    void addFrame(const uint8_t *pPixels, int width, int height, uint32_t ticks);

private:
    //! Sends the frames and the sound to their files (in a thread)
    void writeLoop();
    //! Called in the audio thread with the final mix
    void onSamples(const uint8_t *pSamples, int length);

private:
    bool recording_ = false;
    Audio *pAudio_ = nullptr;
    std::string outputPath_;
    std::string videoPath_;
    std::string soundPath_;
    //! Format of the sound for ffmpeg
    std::string soundFormat_;
    int soundFrequency_ = 0;
    int soundChannels_ = 0;
    int width_ = 0;
    int height_ = 0;
    uint32_t startTicks_ = 0;
    //! Number of frames sent to the video, repeated ones included
    int64_t nbFrames_ = 0;

    //! ffmpeg encoding the video
    FILE *pVideoPipe_ = nullptr;
    FILE *pSoundFile_ = nullptr;
    std::thread writer_;
    std::mutex mutex_;
    std::condition_variable wakeUp_;
    bool stopWriting_ = false;
    //! Frames waiting to be sent, with how many times each one is in the video
    std::deque<std::pair<std::vector<uint8_t>, int>> frames_;
    //! Sound waiting to be written
    std::vector<uint8_t> sound_;
    //! The videos being finished : the sound is added to them
    std::vector<std::thread> finishers_;
};

}

#define g_Recorder fs_eng::Recorder::singleton()

#endif  // SYSTEM_RECORDER_H_

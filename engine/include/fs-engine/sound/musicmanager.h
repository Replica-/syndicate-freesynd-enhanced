/*
 *  FreeSynd - a remake of the classic Bullfrog game "Syndicate".
 *
 *   Copyright (C) 2005  Stuart Binge  <skbinge@gmail.com>
 *   Copyright (C) 2005  Joost Peters  <joostp@users.sourceforge.net>
 *   Copyright (C) 2006  Trent Waddington <qg@biodome.org>
 *   Copyright (C) 2006  Tarjei Knapstad <tarjei.knapstad@gmail.com>
 *   Copyright (C) 2010, 2024-2025  Benoit Blancard <benblan@users.sourceforge.net>
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

#ifndef MUSICMANAGER_H
#define MUSICMANAGER_H

#include <vector>
#include <memory>
#include <string>

#include "fs-utils/common.h"
#include "fs-utils/misc/singleton.h"
#include "fs-engine/sound/audio.h"

namespace fs_eng {

/*!
* Music manager class.
*/
class MusicManager : public Singleton < MusicManager > {
public:

    /*!
     * The different files used for music
     */
    enum MusicFile {
        kMusicFileIntro,
        kMusicFileGame,
        kMusicFileNoFile
    };

    /*!
     * This is the list of tracks throughout the game.
     */
    enum MusicSong {
        kMusicSongIntro,
        kMusicSongAssassinate,
        kMusicSongDanger,
        kMusicSongGameCompleted,
        kMusicSongMissionFailed,
        kMusicSongMissionCompleted,
        kMusicSongNoSong = -1
    };

    MusicManager();
    ~MusicManager();

    void initialize(bool disabled, Audio* audio);
    /*!
     * Looks for music files replacing the songs of the game in the given folder. Each
     * file is named after its song (assassinate, danger, intro, game_completed,
     * mission_failed, mission_completed) with the extension mp3, ogg, flac or wav.
     */
    void findMusicFiles(const std::string &folder);
    //! Returns true if the song is played from a music file
    bool hasMusicFile(MusicSong song) { return !musicFiles_[song].empty(); }

    /*!
     * Plays the given song with possibility to loop. With resume, a song played from a
     * music file starts where it was when another song replaced it.
     */
    void playSong(MusicSong song, bool loopForEver = false, bool resume = false);
    //! Returns the song being played
    MusicSong currentSong() { return currentSong_; }
    //! Stop playing the current song
    void stopCurrentSong();
    //! Pause the current song
    void pause();
    //! Resume the song
    void resume();

    //! Sets the music volume to the given level
    void setVolume(int volume);
    //! Returns the current volume
    int getVolume();
    //! Increase volume with the given percentage (0-100)
    int increaseVolume(int percentage = 10);
    //! Decrease volume with the given percentage (0-100)
    int decreaseVolume(int percentage = 10);
    //! Mute / unmute the music
    void toggleMusic();

protected:
    bool isAudioInitialized();

protected:
    //! store the current loaded file
    MusicFile currentFile_;
    /*!
    * Saves the volume level before a mute so
    * we can restore it after a unmute.
    */
    int volumeBeforeMute_;
    bool disabled_;
    Audio* audio_;
    //! Number of songs
    static const int kNbSongs = 6;
    //! For each song, the music file that replaces it or empty
    std::string musicFiles_[kNbSongs];
    //! For each song played from a music file, where it was stopped (in seconds)
    double songPositions_[kNbSongs] = {};
    //! The song being played
    MusicSong currentSong_ = kMusicSongNoSong;
};

};

#define g_MusicMgr   fs_eng::MusicManager::singleton()

#endif

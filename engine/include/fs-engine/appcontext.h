/*
 *  FreeSynd - a remake of the classic Bullfrog game "Syndicate".
 *
 *   Copyright (C) 2005  Stuart Binge  <skbinge@gmail.com>
 *   Copyright (C) 2005  Joost Peters  <joostp@users.sourceforge.net>
 *   Copyright (C) 2006  Trent Waddington <qg@biodome.org>
 *   Copyright (C) 2006  Tarjei Knapstad <tarjei.knapstad@gmail.com>
 *   Copyright (C) 2010  Bohdan Stelmakh <chamel@users.sourceforge.net>
 *   Copyright (C) 2013, 2024-2025  Benoit Blancard <benblan@users.sourceforge.net>
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

#ifndef CORE_APPCONTEXT_H_
#define CORE_APPCONTEXT_H_

#include <filesystem>

#include "fs-utils/common.h"
#include "fs-utils/misc/singleton.h"
#include "fs-utils/io/configfile.h"
#include "fs-engine/enginecommon.h"

namespace fs_eng {
/*!
 * This class stores application level parameters.
 */
class AppContext : public Singleton < AppContext > {
public:
    AppContext();
    ~AppContext();

    //! Reads the game config file from the given path
    void readConfiguration(const std::string& iniPath, const std::string& userConfFolder);

    bool isFullScreen() { return fullscreen_; }
    //! Development : file where to export the sprites of a mission (empty for none)
    const std::string &getSpriteExportPath() { return spriteExportPath_; }
    void setSpriteExportPath(const std::string &path) { spriteExportPath_ = path; }
    //! Sets the fullscreen parameter and saves it in the config file
    void updateFullScreen(bool fullscreen);
    //! True when shadows and lights are drawn (hardware mode)
    bool isLightingEnabled() { return lighting_; }
    //! Sets the lighting parameter and saves it in the config file
    void updateLighting(bool lighting);

    bool isPlayIntro() { return playIntro_; }

    //! True for hardware rendering, false for the original software rendering
    bool isHardwareRendering() { return hardwareRendering_; }
    void setHardwareRendering(bool hardware) { hardwareRendering_ = hardware; }

    //! The folder with music files replacing the songs of the game (music_dir in freesynd.ini), empty if none
    const std::string &musicDir() { return musicDir_; }

    bool isTestFiles() { return test_files_; }
    //! Return the sound volume parameter
    int getSoundVolume() { return soundVolume_; }
    //! Return the music volume parameter, -1 if it was never set
    int getMusicVolume() { return musicVolume_; }

    //! Sets the intro flag to false in the config file
    void updateIntroFlag();
    //! Sets the test files flag to false in the config file
    void deactivateTestFlag();
    //! Sets the sound volume parameter to the given value in the config file
    void updateSoundVolume(int volume);
    //! Saves the music volume in the user configuration
    void updateMusicVolume(int volume);

    /*!
     * Returns the palette to use for missions instead of their own one (it sets the
     * time of day) : from 1 to 5 for the files hpal01.dat to hpal05.dat, 0 to use
     * the palette of the mission.
     */
    int getMissionPalette() { return missionPalette_; }
    //! Sets the palette for missions and saves it in the config file
    void updateMissionPalette(int paletteId);

    /*!
     * Hardware mode : time of day applied to missions as a color grade, without
     * changing the palette. 0 is the mission's own look, then day, dusk, night, dawn.
     */
    int getTimeOfDay() { return timeOfDay_; }
    //! Sets the time of day and saves it in the config file
    void updateTimeOfDay(int timeOfDay);

    uint32_t getTimeForClick() { return time_for_click_; }

    fs_eng::FS_Lang currLanguage(void) {return curr_language_; }
    std::string getMessage(const std::string & id);
    void getMessage(const std::string & id, std::string & msg);

private:
    void readLanguage(const int languageId);
    void readFreesyndIni(const std::string& iniFolder);
    void readOrCreateUserConf(const std::string& userConfFolder);
    bool updateUserConf(const ConfigFile& userConf, const std::filesystem::path userConfPath);

private:
    /*! True means the game will run in fullscreen. */
    bool fullscreen_;
    /*! True means the intro will be played.*/
    bool playIntro_;
    /*!
     * True (render_mode = hardware) uses the GPU renderer and enhancements
     * like extra company colours and faster menu transitions.
     * False (render_mode = software) keeps the original behaviour for comparison.
     */
    bool hardwareRendering_;
    //! See musicDir()
    std::string musicDir_;
    //! Palette to use for missions (time of day in software mode) : 0 is the palette of the mission
    int missionPalette_;
    //! Time of day in hardware mode : 0 is the mission's own look
    int timeOfDay_;
    //! Shadows and lights in hardware mode
    bool lighting_;
    //! Development : file where to export the sprites of a mission
    std::string spriteExportPath_;
    /*! Time range between mouse up and down that is treated as click,
     * if it will be longer it will be treated as dragging
    */
    uint32_t time_for_click_;
    /*! True means data files will be verified.*/
    bool test_files_;
    /*! Language file. */
    ConfigFile  *language_;
    fs_eng::FS_Lang curr_language_;
    //! Volume of sound : from 0 to 128
    int soundVolume_;
    //! Music volume (music_volume in user.conf), -1 to keep the default one
    int musicVolume_ = -1;
};

}

#define g_Ctx   fs_eng::AppContext::singleton()

#endif // CORE_APPCONTEXT_H_

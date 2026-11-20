/*
 *  FreeSynd - a remake of the classic Bullfrog game "Syndicate".
 *
 *   Copyright (C) 2023-2025  Benoit Blancard <benblan@users.sourceforge.net>
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

#include "fs-engine/base_app.h"

#include <format>

#include "fs-utils/log/log.h"
#include "fs-utils/io/file.h"

#include "fs-engine/events/event.h"
#include "fs-engine/events/default_events.h"
#include "fs-engine/gfx/animationloader.h"

#ifdef HAVE_SDL_MIXER

#include "mixer/sdlmixeraudio.h"
#endif // HAVE_SDL_MIXER

namespace fs_eng {

CliParam::CliParam() {
    disableSound_ = false;
    softwareRendering_ = false;
}

int CliParam::parseCommandLine(int argc, char *argv[]) {
    CLI::App app;

    app.add_flag("-n,--nosound",disableSound_, "Disable sound");
    app.add_flag("--software", softwareRendering_, "Use the original software rendering (for comparison with hardware mode).");
    app.add_option("--screenshot", screenshotPath_, "Save the screen in the given PNG file, then quit (development).")->option_text("<file>");
    app.add_option("--screenshot-delay", screenshotDelay_, "Time in ms before taking the screenshot (default 3000).")->option_text("<ms>");
    app.add_option("--export-sprites", spriteExportPath_, "With -m and --play, save the sprites of the mission (death animations first) in the given PNG file, then quit (development).")->option_text("<file>");
    app.add_option("-i,--ini", iniPath_, "Specify the directory where to find the freesynd.ini config file.")->option_text("<path>");

    std::string desc = "Specify the directory where to find the user.conf file.";
    #ifdef _WIN32
    desc.append(" (default: freesynd.ini in the same folder as freesynd.exe)");
#elif defined(__APPLE__)
    desc.append(" (default: $HOME/Library/Application Support/FreeSynd/freesynd.ini)");
#else
    desc.append(" (default: $HOME/.freesynd/freesynd.ini)");
#endif
    app.add_option("-u,--user", userConfPath_, desc)->option_text("<path>");

#ifdef _DEBUG
    app.add_option("-l,--log", logMask_, "Apply the specified log flags separated by colon.")->option_text("<flag>");
#endif
    // Add options from subclasses
    addOptions(app);

    try
	{
		app.parse(argc, argv);
	}
	catch (const CLI::ParseError& e)
	{
        app.exit(e);
        if(e.get_name() == "CallForHelp") {
			return -1;
        }
		return 1;
	}

    return 0;
}

BaseApp::BaseApp(MenuFactory *pMenuFactory)
    : context_(std::make_unique<AppContext>()),
      system_(System::createSystem()),
      animationManager_(),
      soundManager_(),
      music_(),
      menus_(pMenuFactory) {
    running_ = false;
}

BaseApp::~BaseApp() {}

int BaseApp::run(int argc, char *argv[]) {
    //fs_eng::CliParam param;

    int res = getCliParam().parseCommandLine(argc, argv);
    if ( res == -1) { // help
        return 0;
    } else if (res == 1) {
        return 1;
    }

    // Initialize log
    Log::initialize(getCliParam().getLogMask(), "game.log");

    LOG(Log::k_FLG_INFO, "Main", "main", ("----- Initializing application..."))
    if (initialize()) {
        LOG(Log::k_FLG_INFO, "Main", "main", ("----- Initializing application completed"))
        LOG(Log::k_FLG_INFO, "Main", "main", ("----- Starting game loop"))
        res = run();
    } else {
        LOG(Log::k_FLG_INFO, "Main", "main", ("----- Initializing application failed"))
    }

    return res;
}

bool BaseApp::initialize() {
    try {
        LOG(Log::k_FLG_INFO, "BaseApp", "initialize", ("App initialization started..."))
        const CliParam& param = getCliParam();
        context_->readConfiguration(param.getIniPath(), param.getUserConfDir());
        if (param.isSoftwareRendering()) {
            context_->setHardwareRendering(false);
        }
        context_->setSpriteExportPath(param.getSpriteExportPath());

        if (context_->isTestFiles()) {
            if (!fs_utl::File::testOriginalData()) {
                throw InitializationFailedException("Original files are not correct. Check logs for details");
            }
            // do not tests files from now
            context_->deactivateTestFlag();
        }

        system_->initialize(context_->isFullScreen(), context_->isHardwareRendering());

        menus_.initialize(isLoadIntroResources());

        // TODO : add a parameter somewhere to load custom files
        OriginalFilesAnimationLoader loader;
        loader.loadAnimations(animationManager_);
        animationManager_.checkLoadIsOk();

        soundManager_.initialize(system_->getAudio(), param.isSoundDisabled(), isLoadIntroResources());

        music_.initialize(param.isSoundDisabled(), system_->getAudio());
        music_.findMusicFiles(context_->musicDir());
        if (context_->getMusicVolume() >= 0) {
            music_.setVolume(context_->getMusicVolume());
        }

        bool resInit = doInitialize();
        if (resInit) {
            LOG(Log::k_FLG_INFO, "BaseApp", "initialize", ("App initialized with success"))
        }

        EventManager::listen<QuitEvent>(this, &BaseApp::onQuitHandler);

        return resInit;
    } catch (InitializationFailedException &e) {
        g_System.showError(e.what());
        return false;
    }
}

/*!
 * By default we load intro resources 
 * @return 
 */
bool BaseApp::isLoadIntroResources() {
    return context_->isPlayIntro();
}

void BaseApp::destroy() {
    menus_.destroy();

    doDestroy();

    // Close log
    Log::close();
}

void BaseApp::doDestroy() {}

/*!
 * This method defines the application loop.
 * \param start_mission Mission id used to start the application in debug mode
 * In standard mode start_mission is always -1.
 */
int BaseApp::run() {

#if 0
    system_->updateScreen();
    int nx = 0, ny = 0, my = 0;
    for (int i = 0; i < tabSize / 6; i++) {
        Sprite *s = menu_sprites_.sprite(i);
        if (nx + s->width() >= Screen::kScreenWidth) {
            nx = 0;
            ny += my;
            my = 0;
        }
        if (ny + s->height() > Screen::kScreenHeight)
            break;
        s->draw(nx, ny, 0);
        system_->updateScreen();
        nx += s->width();
        if (s->height() > my)
            my = s->height();
    }

    waitForKeyPress();
    exit(1);
#endif

    // Let the concrete app decide what menu to start with
    menus_.gotoMenu(getStartMenuId());

    running_ = true;
    uint32_t lasttick = system_->getTicks();
    const std::string screenshotPath = getCliParam().getScreenshotPath();
    const uint32_t screenshotTick = lasttick + getCliParam().getScreenshotDelay();
    while (running_) {
        uint32_t curtick = system_->getTicks();
        uint32_t diff_ticks = curtick - lasttick;
        menus_.updtSinceMouseDown(diff_ticks);

        FS_Event fsEvt;
        while(system_->pumpEvents(fsEvt)) {
            menus_.handleEvent(fsEvt);
        }
        // Animated menus ask for a shorter interval so they are paced by the display
        uint32_t frameInterval = menus_.frameInterval();
        if (diff_ticks < frameInterval) {
            system_->delay(frameInterval - diff_ticks);
            continue;
        }
        system_->clearScreen();
        menus_.handleTick(diff_ticks);
        menus_.renderMenu();
        lasttick = curtick;
        if (!screenshotPath.empty() && curtick >= screenshotTick) {
            system_->saveScreenshot(screenshotPath);
            running_ = false;
        }
        system_->updateScreen();
    }

    return 0;
}

void BaseApp::waitForKeyPress() {

    while (isRunning()) {
        // small pause while waiting for key, also mouse event
        system_->delay(20);
        FS_Event fsEvt;
        while(system_->pumpEvents(fsEvt)) {
            menus_.handleEvent(fsEvt);
        }
    }
}

void BaseApp::onQuitHandler([[maybe_unused]] QuitEvent *evt) {
    LOG(Log::k_FLG_INFO, "BaseApp", "onQuitHandler", ("Received Quit Evt : quitting"))
    // A video being recorded is finished before quitting
    recorder_.stop();
    recorder_.waitForVideos();
    // Without sound (option -n), the volume is unknown : the saved one is kept
    if (!getCliParam().isSoundDisabled()) {
        context_->updateSoundVolume(soundManager_.getVolume());
    }
    running_ = false;
}

}

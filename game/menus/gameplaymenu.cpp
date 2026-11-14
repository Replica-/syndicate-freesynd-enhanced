/*
 *  FreeSynd - a remake of the classic Bullfrog game "Syndicate".
 *
 *   Copyright (C) 2005  Stuart Binge  <skbinge@gmail.com>
 *   Copyright (C) 2005  Joost Peters  <joostp@users.sourceforge.net>
 *   Copyright (C) 2006  Trent Waddington <qg@biodome.org>
 *   Copyright (C) 2006  Tarjei Knapstad <tarjei.knapstad@gmail.com>
 *   Copyright (C) 2007  Davor Ocelic <docelic@mail.inet.hr>
 *   Copyright (C) 2010, 2024-2025  Benoit Blancard <benblan@users.sourceforge.net>
 *   Copyright (C) 2010  Bohdan Stelmakh <chamel@users.sourceforge.net>
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

#include "gameplaymenu.h"

#include <algorithm>
#include <cassert>

#include "fs-engine/appcontext.h"
#include "fs-engine/events/default_events.h"
#include "fs-engine/gfx/animationmanager.h"
#include "fs-engine/gfx/logomanager.h"
#include "fs-engine/gfx/tilemanager.h"
#include "fs-engine/sound/musicmanager.h"
#include "fs-engine/sound/soundmanager.h"
#include "menus/gamemenuid.h"
#include "fs-engine/menus/fliplayer.h"
#include "fs-kernel/model/vehicle.h"
#include "fs-kernel/mgr/missionmanager.h"
#include "fs-kernel/model/shot.h"
#include "core/gamecontroller.h"
#include "core/gamesession.h"

#ifdef _WIN32
#include <windows.h>
#endif

// The number of pixel of a scroll
const int SCROLL_STEP = 16;

const Point2D GameplayMenu::kMiniMapScreenPos = {0, 46 + 44 + 10 + 46 + 44 + 15 + 2 * 32 + 2};
const double GameplayMenu::kMaxZoom = 3.0;
//! Each notch of the mouse wheel multiplies or divides the zoom by this value
const double kZoomStep = 1.25;
//! Maximum time in ms between two presses of the same agent key to center the view on the agent
const uint32_t kDoublePressDelay = 400;
//! How long the marker of a move order stays, in ms
const uint32_t kMoveMarkerDuration = 600;
//! Colour of the marker of a move order
const fs_eng::FSColor kMoveMarkerColour = {90, 255, 90, 255};

//! Width of the map area on screen : it's wider on a wide screen
static int mapAreaWidth() {
    return g_System.screenWidth() - MapRenderer::kGameplayPanelWidth;
}

//#define ANIM_PLUS_FRAME_VIEW

GameplayMenu::GameplayMenu(fs_eng::MenuManager *m) :
Menu(m, fs_game_menus::kMenuIdGameplay, fs_game_menus::kMenuIdDebrief),
tick_count_(0), last_animate_tick_(0),
last_motion_x_(320), last_motion_y_(240), drawHintYellowRect_(false),
hintTimer_(1200, false), hintColorTimer_(200, false), hintSpaceTimer_(500, false),
mission_(nullptr), selection_(), target_(nullptr),
mm_renderer_(kMiniMapScreenPos), zoom_(1.0), escMenuShown_(false), escMenuHover_(-1), pausedBeforeEscMenu_(false),
gradeMultiply_({255, 255, 255, 255}), gradeAdd_({0, 0, 0, 255}), hasGradeAdd_(false),
lastAgentKey_(-1), lastAgentKeyTicks_(0),
zoomOriginX_(MapRenderer::kGameplayPanelWidth), zoomOriginY_(0), warningTimer_(20000)
{
    cursorOnShow_ = kGameplayCursor;
    displayOriginPt_.x = 0;
    displayOriginPt_.y = 0;
    scroll_x_ = 0;
    scroll_y_ = 0;
    ipa_chng_.ipa_chng = -1;
    canPlayPoliceWarnSound_ = true;
}

/*!
 * Scroll the map horizontally.
 * Each map has a min and max value for the world origin coords and this
 * method moves that point between those limits. If scrolling hits the
 * map border, the scrolling is made along that border.
 * \return True is a scroll is made
 */
bool GameplayMenu::scrollOnX() {
    bool change = false;

    int newOriginX = displayOriginPt_.x + scroll_x_;

    fs_knl::TilePoint mpt = mission_->get_map()->screenToTilePoint(newOriginX, displayOriginPt_.y);

    // Scroll to the right
    if (scroll_x_ > 0) {
        if (mpt.ty < scrollMinY()) {
            // we hit the upper right border of the map
            // so we scroll down until the far right corner
            int newWorldY = displayOriginPt_.y + SCROLL_STEP;
            newOriginX += SCROLL_STEP;
            mpt = mission_->get_map()->screenToTilePoint(newOriginX, newWorldY);

            if (mpt.ty < scrollMinY() || mpt.tx > scrollMaxX()) {
                // We hit the corner so don't scroll
                return false;
            } else {
                displayOriginPt_.x = newOriginX;
                displayOriginPt_.y = newWorldY;
                change = true;
            }
        } else if (mpt.tx > scrollMaxX()) {
            // we hit the lower right border of the map
            // so we scroll up until the far right corner
            int newWorldY = displayOriginPt_.y - SCROLL_STEP;
            newOriginX += SCROLL_STEP;
            mpt = mission_->get_map()->screenToTilePoint(newOriginX, newWorldY);

            if (mpt.ty < scrollMinY() || mpt.tx > scrollMaxX()) {
                return false;
            } else {
                displayOriginPt_.x = newOriginX;
                displayOriginPt_.y = newWorldY;
                change = true;
            }
        } else {
            // This is a regular right scroll
            displayOriginPt_.x = newOriginX;
            change = true;
        }

    } else { // Scroll to the left
        if (mpt.tx < scrollMinX()) {
            // we hit the upper left border of the map
            // so we scroll down until the far left corner
            int newWorldY = displayOriginPt_.y + SCROLL_STEP;
            newOriginX -= SCROLL_STEP;
            mpt = mission_->get_map()->screenToTilePoint(newOriginX, newWorldY);

            if (mpt.tx < scrollMinX() || mpt.ty > scrollMaxY()) {
                return false;
            } else {
                displayOriginPt_.x = newOriginX;
                displayOriginPt_.y = newWorldY;
                change = true;
            }
        } else if (mpt.ty > scrollMaxY()) {
            // we hit the lower left border of the map
            // so we scroll up until the far left corner
            int newWorldY = displayOriginPt_.y - SCROLL_STEP;
            newOriginX -= SCROLL_STEP;
            mpt = mission_->get_map()->screenToTilePoint(newOriginX, newWorldY);

            if (mpt.tx < scrollMinX() || mpt.ty > scrollMaxY()) {
                return false;
            } else {
                displayOriginPt_.x = newOriginX;
                displayOriginPt_.y = newWorldY;
                change = true;
            }
        } else {
            displayOriginPt_.x = newOriginX;
            change = true;
        }
    }

    return change;
}

/*!
 * Scroll the map vertically.
 * Each map has a min and max value for the world origin coords and this
 * method moves that point between those limits. If scrolling hits the
 * map border, the scrolling is made along that border.
 * \return True is a scroll is made
 */
bool GameplayMenu::scrollOnY() {
    bool change = false;

    int newWorldY = displayOriginPt_.y + scroll_y_;

    fs_knl::TilePoint mpt = mission_->get_map()->screenToTilePoint(displayOriginPt_.x, newWorldY);

    // Scroll down
    if (scroll_y_ > 0) {
        if (mpt.tx > scrollMaxX()) {
            // we hit the lower right border of the map
            // so we scroll down until the lower corner
            int newOriginX = displayOriginPt_.x - 2*SCROLL_STEP;
            mpt = mission_->get_map()->screenToTilePoint(newOriginX, newWorldY);

            if (mpt.ty > scrollMaxY() || mpt.tx > scrollMaxX()) {
                return false;
            } else {
                displayOriginPt_.x = newOriginX;
                displayOriginPt_.y = newWorldY;
                change = true;
            }
        } else if (mpt.ty > scrollMaxY()) {
            // we hit the lower left border of the map
            // so we scroll down until the lower corner
            int newOriginX = displayOriginPt_.x + 2*SCROLL_STEP;
            mpt = mission_->get_map()->screenToTilePoint(newOriginX, newWorldY);

            if (mpt.ty > scrollMaxY() || mpt.tx > scrollMaxX()) {
                return false;
            } else {
                displayOriginPt_.x = newOriginX;
                displayOriginPt_.y = newWorldY;
                change = true;
            }
        } else {
            displayOriginPt_.y = newWorldY;
            change = true;
        }

    } else { // Scroll up
        if (mpt.tx < scrollMinX()) {
            // we hit the upper right border of the map
            // so we scroll up until the upper corner
            int newOriginX = displayOriginPt_.x + 2*SCROLL_STEP;
            mpt = mission_->get_map()->screenToTilePoint(newOriginX, newWorldY);

            if (mpt.ty < scrollMinY() || mpt.tx < scrollMinX()) {
                return false;
            } else {
                displayOriginPt_.x = newOriginX;
                displayOriginPt_.y = newWorldY;
                change = true;
            }
        } else if (mpt.ty < scrollMinY()) {
            // we hit the upper left border of the map
            // so we scroll up until the upper corner
            int newOriginX = displayOriginPt_.x - 2*SCROLL_STEP;
            mpt = mission_->get_map()->screenToTilePoint(newOriginX, newWorldY);

            if (mpt.ty < scrollMinY() || mpt.tx < scrollMinX()) {
                return false;
            } else {
                displayOriginPt_.x = newOriginX;
                displayOriginPt_.y = newWorldY;
                change = true;
            }
        } else {
            displayOriginPt_.y = newWorldY;
            change = true;
        }
    }

    return change;
}

/*!
 * Initialize the screen position centered on the squad leader.
 */
/*!
 * Limits of the view, in tiles. In hardware mode, the view can go over the whole
 * map, as peds can walk outside the mission area. In software mode, the view stays
 * in the mission area as in the original game.
 */
int GameplayMenu::scrollMinX() {
    return pMapTexture_ ? 0 : mission_->minX();
}

int GameplayMenu::scrollMaxX() {
    return pMapTexture_ ? mission_->get_map()->maxX() - 1 : mission_->maxX();
}

int GameplayMenu::scrollMinY() {
    return pMapTexture_ ? 0 : mission_->minY();
}

int GameplayMenu::scrollMaxY() {
    return pMapTexture_ ? mission_->get_map()->maxY() - 1 : mission_->maxY();
}

void GameplayMenu::initWorldCoords()
{
    centerViewOnPed(selection_.leader());
}

/*!
 * Moves the view so the given ped is in the middle of the map area.
 * When the map is zoomed, the zoomed part is also moved to the middle.
 */
void GameplayMenu::centerViewOnPed(fs_knl::PedInstance *p_leader)
{
    zoomOriginX_ = MapRenderer::kGameplayPanelWidth + (mapAreaWidth() - mapAreaWidth() / zoom_) / 2;
    zoomOriginY_ = (fs_eng::kScreenHeight - fs_eng::kScreenHeight / zoom_) / 2;

    // get the ped position on the map
    fs_knl::TilePoint leaderPos(p_leader->tileX(),
                                p_leader->tileY(),
                                mission_->mmax_z_ + 1,
                                0, 0);
    Point2D start;
    mission_->get_map()->tileToScreenPoint(leaderPos, &start);
    start.x -= mapAreaWidth() / 2;
    start.y -= fs_eng::kScreenHeight / 2;

    if (start.x < 0)
        start.x = 0;

    if (start.y < 0)
        start.y = 0;

    // Check if the position is within map borders
    fs_knl::TilePoint mpt = mission_->get_map()->screenToTilePoint(start.x, start.y);

    if (mpt.tx < scrollMinX())
        mpt.tx = scrollMinX();

    if (mpt.ty < scrollMinY())
        mpt.ty = scrollMinY();

    if (mpt.tx > scrollMaxX())
        mpt.tx = scrollMaxX();

    if (mpt.ty > scrollMaxY())
        mpt.ty = scrollMaxY();

    // recalculating new screen coords
    fs_knl::TilePoint newPoint(mpt.tx,
                                mpt.ty,
                                mission_->mmax_z_ + 1, 
                                0, 0);
    Point2D msp;
    mission_->get_map()->tileToScreenPoint(newPoint, &msp);
    displayOriginPt_.x = msp.x;
    displayOriginPt_.y = msp.y;
}

/*!
 * Called before the menu is first shown.
 * Place to do some initializing.
 */
bool GameplayMenu::handleBeforeShow() {
    mission_ = g_missionCtrl.mission();
    mission_->start(g_gameCtrl.weaponManager());
    // init selection to the first selectable agent
    selection_.setSquad(mission_->getSquad());

    // init menu internal state
    isButtonSelectAllPressed_ = false;
    initWorldCoords();

    // set graphic palette
    missionPalette_ = mission_->get_map()->getTileManager()->getPalette();
    g_SpriteMgr.setPalette(missionPalette_);

    // In hardware mode, the map is drawn in a texture so it can be zoomed
    zoom_ = 1.0;
    zoomOriginX_ = MapRenderer::kGameplayPanelWidth;
    zoomOriginY_ = 0;
    if (g_Ctx.isHardwareRendering() && !pMapTexture_) {
        pMapTexture_ = g_System.createTexture();
        // The texture is big enough for the widest screen
        pMapTexture_->createRenderTargetTexture(fs_eng::kMaxScreenWidth, fs_eng::kScreenHeight);
    }

    // In hardware mode, agents wear the color of their company on their hat
    if (g_Ctx.isHardwareRendering() &&
            g_SpriteMgr.createTintMask(fs_knl::PedInstance::kAgentHeadSprites, fs_knl::PedInstance::kHatColourIndex)) {
        fs_knl::PedInstance::setAgentHatColours(g_LogoMgr.getColour(g_Session.getLogoColour()),
            g_Session.getEnemyColour(g_Session.getSelectedBlock()));
    } else {
        fs_knl::PedInstance::clearAgentHatColours();
    }

    highlightLeaderMarker();
    updateMarkersPosition();

    // Init renderers
    map_renderer_.init(mission_, &selection_, g_Ctx.isHardwareRendering() && g_Ctx.isLightingEnabled(),
        g_Ctx.isHardwareRendering());
    mm_renderer_.init(mission_, mission_->getSquad()->hasScanner(), missionPalette_);
    centerMinimapOnLeader();
    isPlayerShooting_ = false;
    if (pMapTexture_) {
        applyTimeOfDay();
    }

    // Development option --export-sprites
    if (!g_Ctx.getSpriteExportPath().empty()) {
        exportSpriteSheet(g_Ctx.getSpriteExportPath());
        EventManager::fire<QuitEvent>(0);
    }

    // Register event handlers
    handleAgentDied_ = EventManager::listen<fs_knl::AgentDiedEvent>(this, &GameplayMenu::onAgentDiedEvent);
    handleWeaponSelected_ = EventManager::listen<fs_knl::ShootingWeaponSelectedEvent>(this, &GameplayMenu::onShootingWeaponSelectedEvent);
    handleAgentWarned_ = EventManager::listen<fs_knl::PoliceWarningEmittedEvent>(this, &GameplayMenu::onPoliceWarningEmittedEvent);
    handleMissionEnded_ = EventManager::listen<fs_knl::MissionEndedEvent>(this, &GameplayMenu::onMissionEndedEvent);

    // play game track
    g_MusicMgr.playSong(fs_eng::MusicManager::kMusicSongAssassinate, true);
    
    menu_manager_->resetSinceMouseDown();

    return true;
}

#ifdef ANIM_PLUS_FRAME_VIEW
int qanim = 1959, qframe = 0;
#endif

bool GameplayMenu::handleTick(uint32_t elapsed)
{
    if (paused_) {
        // In the light editor, the view can still be moved
        if (lightEditor_) {
            scrollView();
        }
        return true;
    }
    tick_count_ += elapsed;

    if (mission_->isRunning()) {
        // Update stats
        mission_->stats()->incrMissionDuration(elapsed);
        mission_->checkObjectives();
    }

    if (!canPlayPoliceWarnSound_ && warningTimer_.update(elapsed)) {
        // wait an amount of time before allowing another warning
        canPlayPoliceWarnSound_ = true;
    }

    bool change = scrollView();

    if (tick_count_ - last_animate_tick_ > 33) {
        uint32_t diff = tick_count_ - last_animate_tick_;
        last_animate_tick_ = tick_count_;

        mission_->handleTick(elapsed, diff);

        updateMarkersPosition();
    }

    mm_renderer_.handleTick(elapsed);

    updateIPALevelMeters(elapsed);

    if (change) {
        // force target to update
        handleMouseMotion({last_motion_x_, last_motion_y_}, 0);
    }

    updateMissionHint(elapsed);

    return true;
}

bool GameplayMenu::scrollView() {
    bool change = false;
    // Arrows and WASD held down scroll the map, in several directions at the
    // same time. Not with Ctrl as Ctrl+D is the self-destruction
    if (!g_System.isKeyModStatePressed(fs_eng::KMD_CTRL)) {
        int keyX = (g_System.isKeyDown(fs_eng::kKeyCode_Right) || g_System.isKeyDown(fs_eng::kKeyCode_D))
                    - (g_System.isKeyDown(fs_eng::kKeyCode_Left) || g_System.isKeyDown(fs_eng::kKeyCode_A));
        int keyY = (g_System.isKeyDown(fs_eng::kKeyCode_Down) || g_System.isKeyDown(fs_eng::kKeyCode_S))
                    - (g_System.isKeyDown(fs_eng::kKeyCode_Up) || g_System.isKeyDown(fs_eng::kKeyCode_W));
        if (keyX != 0) {
            scroll_x_ = keyX * SCROLL_STEP;
        }
        if (keyY != 0) {
            scroll_y_ = keyY * SCROLL_STEP;
        }
    }

    // Scroll the map
    if (scroll_x_ != 0) {
        change = scrollOnX();
        scroll_x_ = 0;
    }

    if (scroll_y_ != 0) {
        change = scrollOnY() || change;
        scroll_y_ = 0;
    }
    return change;
}

/*!
 * Draws the sprites of the mission (with its palette) in a PNG file :
 * - first the blood pool,
 * - then the death animations of agents, one row per animation, twice as big,
 * - then the whole sprite sheet as it's stored in memory.
 * The sprites are read from the original game files when the game starts.
 */
void GameplayMenu::exportSpriteSheet(const std::string &path) {
    struct AnimRow {
        const char *name;
        uint16_t firstAnim;
        int nbDirections;
    };
    // Animation ids of agents (see PedInstance::setAnimations())
    const AnimRow rows[] = {
        {"DYING", 206, 1}, {"DEAD AGENT", 205, 1}, {"DEAD", 207, 1},
        {"HIT", 194, 4}, {"VAPORIZED", 198, 4},
        {"BURNING", 209, 1}, {"DYING BURNING", 210, 1}, {"SMOKING", 211, 1}, {"BURNT", 828, 1}
    };
    const int kBloodPoolSprite = 709;
    const int kCellWidth = 40;
    const int kCellHeight = 44;
    const int kLabelWidth = 104;
    const int kSectionWidth = 512;
    const int kBloodHeight = 40;
    const int kSheetWidth = 1024;
    const int kSheetHeight = 640;
    const fs_eng::FSColor kBackground = {60, 60, 70, 255};

    int nbRows = 0;
    for (const AnimRow &row : rows) {
        nbRows += row.nbDirections;
    }
    const int sectionHeight = kBloodHeight + nbRows * kCellHeight;

    // The death animations are drawn at their size, then twice as big in the final image
    std::unique_ptr<fs_eng::FSTexture> pSection = g_System.createTexture();
    pSection->createRenderTargetTexture(kSectionWidth, sectionHeight);
    pSection->setAsRenderTarget();
    g_System.drawFillRect({0, 0}, kSectionWidth, sectionHeight, kBackground);
    fs_eng::MenuFont *pFont = getMenuFont(fs_eng::FontManager::SIZE_1);

    pFont->drawText(4, 14, "BLOOD POOL", false, false);
    g_SpriteMgr.drawSpriteX2(kBloodPoolSprite, {kLabelWidth, 4});

    int y = kBloodHeight;
    for (const AnimRow &row : rows) {
        for (int dir = 0; dir < row.nbDirections; dir++) {
            uint16_t anim = row.firstAnim + dir;
            std::string label = row.name;
            if (row.nbDirections > 1) {
                label += " " + std::to_string(dir + 1);
            }
            pFont->drawText(4, y + kCellHeight / 2 - 4, label, false, false);
            int nbFrames = g_SpriteMgr.lastFrame(anim) + 1;
            for (int frame = 0; frame < nbFrames && kLabelWidth + (frame + 1) * kCellWidth <= kSectionWidth; frame++) {
                // Frames are drawn from the feet of the ped
                g_SpriteMgr.drawFrame(anim, frame, {kLabelWidth + frame * kCellWidth + kCellWidth / 2, y + kCellHeight - 6});
            }
            y += kCellHeight;
        }
    }

    std::unique_ptr<fs_eng::FSTexture> pSheet = g_System.createTexture();
    pSheet->createRenderTargetTexture(kSheetWidth, sectionHeight * 2 + kSheetHeight);
    pSheet->setAsRenderTarget();
    g_System.drawFillRect({0, 0}, kSheetWidth, sectionHeight * 2 + kSheetHeight, kBackground);
    pSection->renderScaled({0, 0}, kSectionWidth, sectionHeight, {0, 0}, kSectionWidth * 2, sectionHeight * 2);
    g_SpriteMgr.drawSpriteSheet({0, sectionHeight * 2}, kSheetWidth, kSheetHeight);

    g_System.saveScreenshot(path);

    // A second file with all the tiles and their number : <name>_tiles.png
    const int kTilesPerRow = 16;
    const int kTileCellHeight = fs_eng::Tile::kTileHeight + 12;
    const int kNbTiles = fs_eng::TileManager::kNumOfTiles;
    const int tilesHeight = (kNbTiles + kTilesPerRow - 1) / kTilesPerRow * kTileCellHeight;
    std::unique_ptr<fs_eng::FSTexture> pTiles = g_System.createTexture();
    pTiles->createRenderTargetTexture(kTilesPerRow * fs_eng::Tile::kTileWidth, tilesHeight);
    pTiles->setAsRenderTarget();
    g_System.drawFillRect({0, 0}, kTilesPerRow * fs_eng::Tile::kTileWidth, tilesHeight, kBackground);
    fs_eng::TileManager *pTileManager = mission_->get_map()->getTileManager();
    pTileManager->setColorModulation({255, 255, 255, 255});
    for (int id = 0; id < kNbTiles; id++) {
        int x = (id % kTilesPerRow) * fs_eng::Tile::kTileWidth;
        int y = (id / kTilesPerRow) * kTileCellHeight;
        pFont->drawText(x + 2, y + 1, std::to_string(id), false, false);
        pTileManager->drawTile(pTileManager->getTile(static_cast<uint8_t>(id)), x, y + 12);
    }
    std::string tilesPath = path;
    size_t dot = tilesPath.rfind('.');
    tilesPath.insert(dot == std::string::npos ? tilesPath.size() : dot, "_tiles");
    g_System.saveScreenshot(tilesPath);

    // A third file with the map as it's seen at the start of the mission, twice as big,
    // with the number of each tile written on it : <name>_map.png
    std::unique_ptr<fs_eng::FSTexture> pView = g_System.createTexture();
    pView->createRenderTargetTexture(fs_eng::kMaxScreenWidth, fs_eng::kScreenHeight);
    pView->setAsRenderTarget();
    g_System.clearScreen();
    map_renderer_.render(displayOriginPt_);
    std::unique_ptr<fs_eng::FSTexture> pMap = g_System.createTexture();
    const int kMapWidth = mapAreaWidth() * 2;
    const int kMapHeight = fs_eng::kScreenHeight * 2;
    pMap->createRenderTargetTexture(kMapWidth, kMapHeight);
    pMap->setAsRenderTarget();
    pView->renderScaled({MapRenderer::kGameplayPanelWidth, 0}, mapAreaWidth(), fs_eng::kScreenHeight,
        {0, 0}, kMapWidth, kMapHeight);
    // The number is written in the middle of the top of the tile, last drawn tile on top
    for (const auto &[pTile, pos] : map_renderer_.drawnTiles()) {
        std::string number = std::to_string(pTile->id());
        Point2D textPos = {(pos.x - MapRenderer::kGameplayPanelWidth + fs_eng::Tile::kTileWidth / 2) * 2 - pFont->textWidth(number) / 2,
                           (pos.y + fs_eng::Tile::kTileHeight / 3) * 2};
        g_System.drawFillRect(textPos.add(-1, -1), pFont->textWidth(number) + 2, 9, {0, 0, 0, 255});
        pFont->drawText(textPos.x, textPos.y, number, true, false);
    }
    std::string mapPath = path;
    mapPath.insert(dot == std::string::npos ? mapPath.size() : dot, "_map");
    g_System.saveScreenshot(mapPath);

    g_System.resetRenderTarget();
}

void GameplayMenu::showMoveMarker(const fs_knl::TilePoint &pos) {
    moveMarkerPos_ = pos;
    moveMarkerTicks_ = g_System.getTicks();
    if (moveMarkerTicks_ == 0) {
        moveMarkerTicks_ = 1;
    }
}

/*!
 * The marker is a diamond lying on the floor, like a tile, that shrinks towards the
 * destination and fades out.
 */
void GameplayMenu::drawMoveMarker() {
    if (moveMarkerTicks_ == 0) {
        return;
    }
    uint32_t age = g_System.getTicks() - moveMarkerTicks_;
    if (age >= kMoveMarkerDuration) {
        moveMarkerTicks_ = 0;
        return;
    }
    double progress = age / static_cast<double>(kMoveMarkerDuration);
    fs_eng::FSColor colour = kMoveMarkerColour;
    colour.a = static_cast<uint8_t>(255 * (1.0 - progress));

    Point2D centre = map_renderer_.floorPointOnScreen(moveMarkerPos_, displayOriginPt_);
    int halfWidth = static_cast<int>(6 + 22 * (1.0 - progress));
    int halfHeight = halfWidth / 2;
    Point2D corners[4] = {
        centre.add(0, -halfHeight), centre.add(halfWidth, 0), centre.add(0, halfHeight), centre.add(-halfWidth, 0)
    };
    // Each side is a thin band so it can fade out
    for (int i = 0; i < 4; i++) {
        Point2D side[4] = {corners[i], corners[(i + 1) % 4], corners[(i + 1) % 4].add(0, 2), corners[i].add(0, 2)};
        g_System.drawFillPolygon(side, 4, colour);
    }
    Point2D dot[4] = {centre.add(0, -2), centre.add(4, 0), centre.add(0, 2), centre.add(-4, 0)};
    g_System.drawFillPolygon(dot, 4, colour);
}

int GameplayMenu::seeThroughLevel() {
    fs_knl::PedInstance *pLeader = selection_.leader();
    if (escMenuShown_ || pLeader == nullptr || !g_System.isKeyDown(fs_eng::kKeyCode_B)) {
        return -1;
    }
    return pLeader->tileZ();
}

void GameplayMenu::renderMap() {
    map_renderer_.setSeeThroughLevel(seeThroughLevel());
    if (!pMapTexture_) {
        map_renderer_.render(displayOriginPt_);
        map_renderer_.drawWalkableOutline(displayOriginPt_);
        drawMoveMarker();
        return;
    }

    pMapTexture_->setAsRenderTarget();
    g_System.clearScreen();
    map_renderer_.render(displayOriginPt_);
    // The colour grade for the time of day is the ambient light : lights brighten
    // the map around them. It only changes the map, not the panel
    bool withLights = map_renderer_.isLightingEnabled();
    if (withLights) {
        map_renderer_.applyLights(pMapTexture_.get(), gradeMultiply_);
    }
    // The outline is drawn after the lights so it can be seen at night
    map_renderer_.drawWalkableOutline(displayOriginPt_);
    drawMoveMarker();
    if (lightEditor_) {
        drawLightEditorTiles();
    }
    g_System.resetRenderTarget();

    Point2D src = {static_cast<int>(zoomOriginX_), static_cast<int>(zoomOriginY_)};
    int srcWidth = static_cast<int>(mapAreaWidth() / zoom_);
    int srcHeight = static_cast<int>(fs_eng::kScreenHeight / zoom_);
    pMapTexture_->setColorModulation(withLights ? fs_eng::FSColor {255, 255, 255, 255} : gradeMultiply_);
    pMapTexture_->renderScaled(src, srcWidth, srcHeight, {MapRenderer::kGameplayPanelWidth, 0}, mapAreaWidth(), fs_eng::kScreenHeight);
    if (hasGradeAdd_) {
        pMapTexture_->setBlendMode(fs_eng::FSTexture::BlendMode::kAdd);
        pMapTexture_->setColorModulation(gradeAdd_);
        pMapTexture_->renderScaled(src, srcWidth, srcHeight, {MapRenderer::kGameplayPanelWidth, 0}, mapAreaWidth(), fs_eng::kScreenHeight);
        pMapTexture_->setBlendMode(fs_eng::FSTexture::BlendMode::kNone);
    }
}

/*!
 * Converts a point on the screen into the coordinates the map would have
 * without zoom. Points on the panel are not changed.
 */
Point2D GameplayMenu::unzoomPoint(Point2D point) {
    if (point.x < MapRenderer::kGameplayPanelWidth || zoom_ == 1.0) {
        return point;
    }
    return {static_cast<int>(zoomOriginX_ + (point.x - MapRenderer::kGameplayPanelWidth) / zoom_),
            static_cast<int>(zoomOriginY_ + point.y / zoom_)};
}

void GameplayMenu::handleMouseWheel(Point2D point, int delta) {
    if (!pMapTexture_ || escMenuShown_ || point.x < MapRenderer::kGameplayPanelWidth || delta == 0) {
        return;
    }

    // The point of the map under the mouse stays under the mouse
    Point2D mapPoint = unzoomPoint(point);
    double newZoom = delta > 0 ? zoom_ * kZoomStep : zoom_ / kZoomStep;
    zoom_ = std::clamp(newZoom, 1.0, kMaxZoom);

    double viewWidth = mapAreaWidth() / zoom_;
    double viewHeight = fs_eng::kScreenHeight / zoom_;
    zoomOriginX_ = std::clamp(mapPoint.x - (point.x - MapRenderer::kGameplayPanelWidth) / zoom_,
        (double) MapRenderer::kGameplayPanelWidth, g_System.screenWidth() - viewWidth);
    zoomOriginY_ = std::clamp(mapPoint.y - point.y / zoom_, 0.0, fs_eng::kScreenHeight - viewHeight);

    // Update the target under the mouse
    handleMouseMotion(point, 0);
}

void GameplayMenu::handleRender() {
    renderMap();
    g_System.drawRect({0,0}, 129, fs_eng::kScreenHeight, menu_manager_->kMenuColorBlack);
    agt_sel_renderer_.render(selection_, mission_->getSquad(), missionPalette_);
    drawSelectAllButton();
    drawMissionHint();
    drawWeaponSelectors();
    mm_renderer_.render(missionPalette_);

    if (escMenuShown_) {
        drawEscMenu();
    } else if (lightEditor_) {
        drawLightEditorPanel();
    } else if (paused_) {
        drawPausePanel();
    }

#ifdef _DEBUG
    // drawing of different sprites
//    g_SpriteMgr.sprite(9 * 40 + 1)->draw(0, 0, 0, false, true);
#if 0
    // 1601 == start of weapons icons for sidebar
    // 1621 == start of selected weapons icons for sidebar
    // 1748 == start of agent selectors for sidebar
    // 1772 == start of selected agent selectors for sidebar
    g_Screen.clear(1);
    int x = 0, y = 0, my = 0;
    for (int i = 1756; i < g_SpriteMgr.spriteCount(); i++) {
        Sprite *s = g_SpriteMgr.sprite(i);

        if (y + s->height() > GAME_SCREEN_HEIGHT) {
            printf("last sprite %i\n", i - 1);
            break;
        }

        if (x + s->width() > GAME_SCREEN_WIDTH) {
            x = 0;
            y += my;
            my = 0;
        }

        s->draw(x, y, 0);
        x += s->width();
        if (s->height() > my)
            my = s->height();
    }
#endif
    // this is used in combination with keys
#ifdef ANIM_PLUS_FRAME_VIEW
    g_SpriteMgr.drawFrame(qanim, qframe, 320, 200);
#endif
#endif

#ifdef FS_TRACK_FPS
    std::stringstream fpsText;
    static uint32_t current_time = 0;
    static uint32_t last_time = 0;
    static float fps = 0;
    static int frame = 0;

    current_time = g_System.getTicks();

    uint32_t elapsed = current_time - last_time;
    frame++;

    if (elapsed > 1000) {
        fps = (float) frame / ((float) elapsed / 1000.0f);
        frame = 0;
        last_time = current_time;
    }

    fpsText << "FPS: " << fps; 
    gameFont()->drawText(10, fs_eng::kScreenHeight - 15, fpsText.str().c_str(), menu_manager_->kMenuColorYellow);
#endif
}

void GameplayMenu::handleLeave()
{
    g_MusicMgr.stopCurrentSong();

    // Remove handlers to prevent events coming after the end of mission
    EventManager::remove_listener(handleAgentDied_);
    EventManager::remove_listener(handleWeaponSelected_);
    EventManager::remove_listener(handleAgentWarned_);
    EventManager::remove_listener(handleMissionEnded_);

    selection_.clear();

    tick_count_ = 0;
    last_animate_tick_ = 0;
    last_motion_x_ = 320;
    last_motion_y_ = 240;
    displayOriginPt_.x = 0;
    displayOriginPt_.y = 0;
    target_ = NULL;
    mission_ = NULL;
    scroll_x_ = 0;
    scroll_y_ = 0;
    paused_ = false;
    escMenuShown_ = false;
    lightEditor_ = false;
    ipa_chng_.ipa_chng = -1;
}

void GameplayMenu::handleMouseMotion(Point2D point, [[maybe_unused]] uint32_t state) {
    last_motion_x_ = point.x;
    last_motion_y_ = point.y;
    if (escMenuShown_) {
        escMenuHover_ = escMenuItemAt(point);
        return;
    }
    if (lightEditor_) {
        lightEditorHover_ = lightEditorItemAt(point);
        return;
    }
    // locking mouse motion on ipa change until mouseup is recieved
    if (ipa_chng_.ipa_chng != -1 && menu_manager_->isMouseDragged()) {
        fs_knl::PedInstance *p = mission_->ped(ipa_chng_.agent_used);
        if (p->isAlive()) {
            uint8_t percent = agt_sel_renderer_.getPercentageAnyX(
                ipa_chng_.agent_used, point.x);

            // if agent is in selected group we will update all groups IPA
            if (selection_.isAgentSelected(ipa_chng_.agent_used)) {
                for (uint8_t i = 0; i < fs_knl::Squad::kMaxSlot; ++i) {
                    if (selection_.isAgentSelected(i)) {
                        setIPAForAgent(i, (IPAStim::IPAType)ipa_chng_.ipa_chng,
                            percent);
                    }
                }
            } else {
                setIPAForAgent(ipa_chng_.agent_used,
                    (IPAStim::IPAType)ipa_chng_.ipa_chng, percent);
            }
            return;
        } else
            ipa_chng_.ipa_chng = -1;
    }

    if (last_motion_x_ < 5) {
        scroll_x_ = - SCROLL_STEP;
    } else if (last_motion_x_ > g_System.screenWidth() - 5) {
        scroll_x_ = SCROLL_STEP;
    }

    if (last_motion_y_ < 5) {
        scroll_y_ = - SCROLL_STEP;
    } else if (last_motion_y_ > fs_eng::kScreenHeight - 5) {
        scroll_y_ = SCROLL_STEP;
    }

    // Scrolling uses the edges of the screen, the rest uses the map coordinates
    point = unzoomPoint(point);

    bool inrange = false;
    target_ = NULL;

    if (point.x > 128) {
        for (size_t i = mission_->getSquad()->size(); mission_ && i < mission_->numPeds(); ++i) {
            fs_knl::PedInstance *p = mission_->ped(i);
            if (p->isAlive() && p->isDrawable()) {
                Point2D scPt;
                mission_->get_map()->tileToScreenPoint(p->position(), &scPt);
                int px = scPt.x - 10;
                int py = scPt.y - (1 + p->tileZ()) * fs_eng::Tile::kTileHeight/3
                    - (p->offZ() * fs_eng::Tile::kTileHeight/3) / 128;

                if (point.x - 129 + displayOriginPt_.x >= px && point.y + displayOriginPt_.y >= py &&
                    point.x - 129 + displayOriginPt_.x < px + 21 && point.y + displayOriginPt_.y < py + 34)
                {
                    // mouse pointer is on the object, so it's the new target
                    target_ = p;
                    inrange = selection_.isTargetInRange(mission_, target_);
                    break;
                }
            }
        }

        for (size_t i = 0; mission_ && i < mission_->numVehicles(); ++i) {
            fs_knl::Vehicle *v = mission_->vehicle(i);
            // TrainHead cannot be selected to prevent player from putting agents in it
            if (v->isAlive() && v->getType() != fs_knl::Vehicle::kVehicleTypeTrainHead) {
                Point2D scPt;
                mission_->get_map()->tileToScreenPoint(v->position(), &scPt);
                int px = scPt.x - 20;
                int py = scPt.y - 10 - v->tileZ() * fs_eng::Tile::kTileHeight/3;

                if (point.x - 129 + displayOriginPt_.x >= px && point.y + displayOriginPt_.y >= py &&
                    point.x - 129 + displayOriginPt_.x < px + 40 && point.y + displayOriginPt_.y < py + 32)
                {
                    target_ = v;
                    inrange = selection_.isTargetInRange(mission_, target_);
                    break;
                }
            }
        }

        for (size_t i = 0; mission_ && i < mission_->numWeaponsOnGround(); ++i) {
            fs_knl::WeaponInstance *w = mission_->weaponOnGround(i);

            if (w->isDrawable()) {
                Point2D scPt;
                mission_->get_map()->tileToScreenPoint(w->position(), &scPt);
                int px = scPt.x - 10;
                int py = scPt.y + 4 - w->tileZ() * fs_eng::Tile::kTileHeight/3
                    - (w->offZ() * fs_eng::Tile::kTileHeight/3) / 128;

                if (point.x - 129 + displayOriginPt_.x >= px && point.y + displayOriginPt_.y >= py &&
                    point.x - 129 + displayOriginPt_.x < px + 20 && point.y + displayOriginPt_.y < py + 15)
                {
                    target_ = w;
                    break;
                }
            }
        }
#if 0
#ifdef _DEBUG
        for (int i = 0; mission_ && i < mission_->numStatics(); ++i) {
            Static *s = mission_->statics(i);

            if (s->map() != -1) {
                Point2D scPt;
                mission_->get_map()->tileToScreenPoint(w->position(), &scPt);
                int px = scPt.x - 10;
                int py = scPt.y + 4 - s->tileZ() * Tile::kTileHeight/3
                    - (s->offZ() * Tile::kTileHeight/3) / 128;

                if (x - 129 + displayOriginPt_.x >= px && y + displayOriginPt_.y >= py &&
                    x - 129 + displayOriginPt_.x < px + 20 && y + displayOriginPt_.y < py + 15)
                {
                    target_ = s;
                    break;
                }
            }
        }
#endif
#endif
    }

    if (target_) {
        if (target_->is(fs_knl::MapObject::kNaturePed) ||
            target_->is(fs_knl::MapObject::kNatureVehicle)) {
            if (inrange)
                g_System.useTargetRedCursor();
            else
                g_System.useTargetCursor();
        } else if (target_->is(fs_knl::MapObject::kNatureWeapon)) {
            g_System.usePickupCursor();
        }
    } else if (point.x > 128) {
            g_System.usePointerCursor();
    } else {
            g_System.usePointerYellowCursor();
    }

    if (point.x < 129 && isPlayerShooting_) {
        stopShootingEvent();
    }

    if (isPlayerShooting_) {
        // update direction for each shooting player
        fs_knl::WorldPoint aimedAtLocW;
        if (getAimedAt(point.x, point.y, &aimedAtLocW)) {
            for (SquadSelection::Iterator it = selection_.begin(); it != selection_.end(); ++it) {
                fs_knl::PedInstance *pAgent = *it;
                if (pAgent->isUsingWeapon()) {
                    // If ped is currently shooting
                    // then update the action with new shooting target
                    pAgent->updateShootingTarget(aimedAtLocW);
                }
            }
        }
    }
}

bool GameplayMenu::handleMouseDown(Point2D point, int button)
{
    if (escMenuShown_) {
        handleEscMenuClick(escMenuItemAt(point), button);
        return true;
    }
    if (lightEditor_) {
        handleLightEditorClick(point, button);
        return true;
    }

    if (paused_)
        return true;

    if (point.x < 129) {
        // Is control button pressed
        bool ctrl = g_System.isKeyModStatePressed(fs_eng::KMD_CTRL);

        // First check if player has clicked on agent selectors
        SelectorEvent selEvt;
        if (agt_sel_renderer_.hasClickedOnAgentSelector(point.x, point.y, selEvt)) {
            switch (selEvt.eventType) {
            case SelectorEvent::kSelectAgent:
                // Handle agent selection. Click on an agent changes selection
                // to it. If control key is pressed, add or removes agent from
                // current selection.
                selectAgent(selEvt.agentSlot, ctrl);
                break;
            case SelectorEvent::kSelectIpa:
                ipa_chng_.ipa_chng = selEvt.IpaType;
                ipa_chng_.agent_used = selEvt.agentSlot;
                setIPAForAgent(selEvt.agentSlot, selEvt.IpaType, selEvt.percentage);
                break;
            case SelectorEvent::kNone:
                break;
            }
        } else if (point.y >= 42 + 48 && point.y < 42 + 48 + 10) {
            // User clicked on the select all button
            selectAllAgents();
        }
        else if (point.y >= 2 + 46 + 44 + 10 + 46 + 44 + 15
                 && point.y < 2 + 46 + 44 + 10 + 46 + 44 + 15 + 64)
        {
            // user clicked on the weapon selector
            handleClickOnWeaponSelector(point, button);
        } else if ( point.y > kMiniMapScreenPos.y && button == kMouseLeftButton) {
            handleClickOnMinimap(point);
        }
    } else {
        // User clicked on the map
        handleClickOnMap(unzoomPoint(point), button);
    }

    return true;
}

/*!
 * The user has clicked on the weapon selector.
 * \param point Mouse coord
 * \param button Mouse button that was clicked
 */
void GameplayMenu::handleClickOnWeaponSelector(Point2D point, int button) {
    uint8_t w_num = static_cast<uint8_t>(((point.y - (2 + 46 + 44 + 10 + 46 + 44 + 15)) / 32) * 4
            + point.x / 32);
    fs_knl::PedInstance *pLeader = selection_.leader();
    if (pLeader->isAlive()) {
        bool is_ctrl = g_System.isKeyModStatePressed(fs_eng::KMD_CTRL);
        if (w_num < pLeader->numWeapons()) {
            if (button == kMouseLeftButton) {
                // Button 1 : selection/deselection of weapon for all selection
                handleWeaponSelection(w_num, is_ctrl);
            } else {
                // Button 3 : drop weapon from selected agent inventory
                pLeader->addActionPutdown(w_num, is_ctrl);
            }
        }
    }
}

void GameplayMenu::setIPAForAgent(size_t slot, IPAStim::IPAType ipa_type, uint8_t percentage) {
    fs_knl::PedInstance *ped = mission_->ped(slot);
    if (ped->isDead())
        return;

    ped->setIPAAmount(ipa_type, percentage);
}

void GameplayMenu::updateIPALevelMeters(uint32_t elapsed) {
    for (auto it = mission_->getSquad()->alive_begin(); it != mission_->getSquad()->alive_end(); ++it) {
        (*it)->updateAllIPA(elapsed);
    }
}

/*!
 * React to a click on the map
 * @param point Where the player clicked in screen coordinates
 * @param button The mouse button he clicked
 */
void GameplayMenu::handleClickOnMap(Point2D point, int button) {
    fs_knl::TilePoint mapPt = mission_->get_map()->screenToTilePoint(displayOriginPt_.x + point.x - 129,
                    displayOriginPt_.y + point.y);
#ifdef _DEBUG
    if (g_System.isKeyModStatePressed(fs_eng::KMD_ALT)) {
        printf("Tile x:%d, y:%d, z:%d, ox:%d, oy:%d\n",
            mapPt.tx, mapPt.ty, mapPt.tz, mapPt.ox, mapPt.oy);

        if (target_) {
            printf("   > target(%i) : %s\n",
                target_->id(), target_->natureName());
        }

        int tileid = mission_->get_map()->tileAt(mapPt.tx, mapPt.ty, mapPt.tz);
        printf("Tile id %d\n", tileid);
        return;
    }
#endif //_DEBUG

    bool ctrl = g_System.isKeyModStatePressed(fs_eng::KMD_CTRL);
    if (button == kMouseLeftButton) {
        if (target_) {
            switch (target_->nature()) {
            case fs_knl::MapObject::kNatureWeapon:
                selection_.pickupWeapon(dynamic_cast<fs_knl::WeaponInstance *>(target_), ctrl);
                break;
            case fs_knl::MapObject::kNaturePed:
                selection_.followPed(dynamic_cast<fs_knl::PedInstance *>(target_));
                break;
            case fs_knl::MapObject::kNatureVehicle:
                selection_.enterOrLeaveVehicle(dynamic_cast<fs_knl::Vehicle *>(target_), ctrl);
                break;
            default:
                break;
            }
        } else if (mission_->getWalkable(mapPt, seeThroughLevel())) {
            // When buildings are see-through, the click is on the level of the leader
            selection_.moveTo(mapPt, ctrl);
            showMoveMarker(mapPt);
        }
    } else if (button == kMouseRightButton) {
        fs_knl::WorldPoint aimedAtLocW;
        if (getAimedAt(point.x, point.y, &aimedAtLocW)) {
            isPlayerShooting_ = true;
            selection_.shootAt(aimedAtLocW);
        }
    }
}

/*!
 * User has clicked on the minimap. All selected agent go to destination.
 * Clicking on the minimap does not allow to shoot or to use objects.
 * \param point minimap coordinate
 */
void GameplayMenu::handleClickOnMinimap(Point2D point) {
    // convert minimap coordinate in map coordinate
    fs_knl::TilePoint pt = mm_renderer_.minimapToMapPoint(point.add(-kMiniMapScreenPos.x, -kMiniMapScreenPos.y));
    // As minimap is flat, we can't see the height. So take the Z coordinate
    // of the leader as a reference
    pt.tz = selection_.leader()->tileZ();
    if (mission_->getWalkableClosestByZ(pt))
    {
        // Destination is walkable so go
        selection_.moveTo(pt, false);
        showMoveMarker(pt);
     }
}

/*!
 * Set the point on the map the player is aiming at.
 * It depends on whether the player has clicked on a shootable target
 * or a point on the ground.
 * \param x mouse X coord on screen
 * \param y mouse Y coord on screen
 * \param pLocWToSet Finale location
 * \return True if location has been set.
 */
bool GameplayMenu::getAimedAt(int x, int y, fs_knl::WorldPoint *pLocWToSet) {
    bool locationSet = false;

    if (target_) {
        //  Player has aimed an object
        pLocWToSet->convertFromTilePoint(target_->position());
        // z is set to half the size of the object
        pLocWToSet->z += target_->sizeZ() >> 1;
        locationSet = true;
    } else {
        // Player is shooting on the ground
        fs_knl::TilePoint mapLocT = mission_->get_map()->screenToTilePoint(displayOriginPt_.x + x - 129,
                    displayOriginPt_.y + y);
        mapLocT.tz = 0;
        if (mission_->getShootableTile(&mapLocT)) {
            locationSet = true;
            pLocWToSet->convertFromTilePoint(mapLocT);
        }
    }

    return locationSet;
}

/*!
 *
 */
void GameplayMenu::stopShootingEvent()
{
    isPlayerShooting_ = false;
    for (SquadSelection::Iterator it = selection_.begin(); it != selection_.end(); ++it) {
        fs_knl::PedInstance *pAgent = *it;

        pAgent->stopShooting();
    }
}


void GameplayMenu::handleMouseUp([[maybe_unused]] Point2D point, int button)
{
    ipa_chng_.ipa_chng = -1;

    if (button == kMouseRightButton && isPlayerShooting_) {
        stopShootingEvent();
    }

}

bool GameplayMenu::handleUnMappedKey(const fs_eng::FS_Key key) {
    bool consumed = true;

    if (lightEditor_) {
        // Escape goes back to the game
        if (key.keyCode == fs_eng::kKeyCode_Escape) {
            closeLightEditor();
        }
        return true;
    }
    // Escape opens or closes the menu while the mission is running
    if (key.keyCode == fs_eng::kKeyCode_Escape && mission_->isRunning()) {
        showEscMenu(!escMenuShown_);
        return true;
    }
    if (escMenuShown_) {
        return true;
    }

    // Pause/unpause game
    if (key.keyCode == fs_eng::kKeyCode_P) {
        paused_ = !paused_;
        if (paused_) {
            stopShootingEvent();
            g_MusicMgr.pause();
        } else {
            g_MusicMgr.resume();
        }
        return true;
    }

    if (paused_)
        return true;

    bool ctrl = g_System.isKeyModStatePressed(fs_eng::KMD_CTRL);

    // SPACE is pressed when the mission failed or succeeded to return
    // to debrief menu
    if (key.keyCode == fs_eng::kKeyCode_Space) {
        if (mission_->completed() || mission_->failed()) {
            if (mission_->completed()) {
                // Display success animation
                menu_manager_->gotoMenu(fs_game_menus::kMenuIdFliSuccess);
            }
            else if (mission_->failed()) {
                menu_manager_->gotoMenu(fs_game_menus::kMenuIdFliFailedMission);
            }

            return true;
        }
    } else if (key.keyCode == fs_eng::kKeyCode_Escape) {
        if (mission_->isRunning()) {
            // Abort mission
            mission_->endWithStatus(fs_knl::Mission::kMissionStatusAborted);
        }
        // Display only the menu up animation
        menu_manager_->gotoMenu(fs_game_menus::kMenuIdFliMissionAborted);
        return true;
    }

    // Handle agent selection by numeric keys. Key 0 cycles between one agent
    // selection and all 4 agents.
    // Individual keys select the specified agent unless ctrl is pressed -
    // then they add/remove agent from current selection.
    if (key.keyCode == fs_eng::kKeyCode_0) {
        // This code is exactly the same as for clicking on "group-button"
        // as you can see above.
        selectAllAgents();
    }
    else if (key.keyCode == fs_eng::kKeyCode_1) {
        handleAgentKey(0, ctrl);
    }
    else if (key.keyCode == fs_eng::kKeyCode_2) {
        handleAgentKey(1, ctrl);
    }
    else if (key.keyCode == fs_eng::kKeyCode_3) {
        handleAgentKey(2, ctrl);
    }
    else if (key.keyCode == fs_eng::kKeyCode_4) {
        handleAgentKey(3, ctrl);
    } else if (key.keyCode == fs_eng::kKeyCode_Left) { // Scroll the map to the left
        scroll_x_ = -SCROLL_STEP;
    } else if (key.keyCode == fs_eng::kKeyCode_Right) { // Scroll the map to the right
        scroll_x_ = SCROLL_STEP;
    } else if (key.keyCode == fs_eng::kKeyCode_Up) { // Scroll the map to the top
        scroll_y_ = -SCROLL_STEP;
    } else if (key.keyCode == fs_eng::kKeyCode_Down) { // Scroll the map to the bottom
        scroll_y_ = SCROLL_STEP;
    }

#ifdef _DEBUG
    else if (key.keyCode == fs_eng::kKeyCode_F11) {
        mission_->endWithStatus(fs_knl::Mission::kMissionStatusCompleted);
        return true;
    } else if (key.keyCode == fs_eng::kKeyCode_F12) {
        mission_->endWithStatus(fs_knl::Mission::kMissionStatusFailed);
        return true;
    }
#endif
    else if (key.keyCode >= fs_eng::kKeyCode_F1 && key.keyCode <= fs_eng::kKeyCode_F8) {
        // Those keys are direct access to inventory
        uint8_t weapon_idx = (uint8_t) key.keyCode - (uint8_t) fs_eng::kKeyCode_F5;
        handleWeaponSelection(weapon_idx, ctrl);
        return true;
    } else if ((key.keyCode == fs_eng::kKeyCode_D) && ctrl && mission_->isRunning()) { // selected agents are killed with 'd'
        // save current selection as it will be modified when agents die
        std::vector<fs_knl::PedInstance *> agents_suicide;
        for (SquadSelection::Iterator it = selection_.begin();
                        it != selection_.end(); ++it) {
                agents_suicide.push_back(*it);
        }

        for (size_t i=0; i < agents_suicide.size(); i++) {
            agents_suicide[i]->commitSuicide();
        }
    } else {
        consumed = false;
    }

#ifdef _DEBUG
#if 0
    static int sound_num = 20;
    if (key.unicode == 'i') {
     g_SoundMgr.play((snd::InGameSample)sound_num);
     printf("sn %i\n", sound_num);
     sound_num++;
     if (sound_num == 33)
         sound_num = 20;
    }
#endif

#if 0
    if (key == KEY_i)
        mission_->ped(0)->setTileY(mission_->ped(0)->tileY() - 1);

    if (key == KEY_k)
        mission_->ped(0)->setTileY(mission_->ped(0)->tileY() + 1);

    if (key == KEY_j)
        mission_->ped(0)->setTileX(mission_->ped(0)->tileX() - 1);

    if (key == KEY_l)
        mission_->ped(0)->setTileX(mission_->ped(0)->tileX() + 1);

    if (key == KEY_PAGEUP) {
        if (mission_->ped(0)->tileZ() < mission_->mmax_z_)
            mission_->ped(0)->setTileZ(mission_->ped(0)->tileZ() + 1);
    }

    if (key == KEY_PAGEDOWN) {
        if (mission_->ped(0)->tileZ() > 0)
            mission_->ped(0)->setTileZ(mission_->ped(0)->tileZ() - 1);
    }

    if (key == KEY_o) {
        mission_->ped(0)->setOffZ(mission_->ped(0)->offZ() + 8);
        if (mission_->ped(0)->tileZ() >= mission_->mmax_z_) {
            mission_->ped(0)->setTileZ(mission_->mmax_z_ - 1);
            mission_->ped(0)->setOffZ(127);
        }
    }

    if (key == KEY_p) {
        mission_->ped(0)->setOffZ(mission_->ped(0)->offZ() - 8);
        if (mission_->ped(0)->tileZ() < 0) {
            mission_->ped(0)->setTileZ(0);
            mission_->ped(0)->setOffZ(0);
        }
    }
    if (mission_->ped(0)->offZ() != 0)
        mission_->ped(0)->setVisZ(mission_->ped(0)->tileZ() - 1);
    else
        mission_->ped(0)->setVisZ(mission_->ped(0)->tileZ());

    printf("%i %i %i\n", mission_->ped(0)->tileX(), mission_->ped(0)->tileY(),
        mission_->ped(0)->tileZ());
#endif

#ifdef ANIM_PLUS_FRAME_VIEW
    // used to see animations by number + frame
    if (key.unicode == 'a') {
        qanim--;
        if (qanim < 0)
            qanim = 0;
    }

    if (key.unicode == 's') {
        qanim++;
        if (qanim > 1969)
            qanim = 1969;
    }

    if (key.unicode == 'q') {
        qanim -= 8;
        if (qanim < 0)
            qanim = 0;
    }

    if (key.unicode == 'w') {
        qanim += 8;
        if (qanim > 1969)
            qanim = 1969;
    }

    if (key.unicode == 'x') {
        qframe++;
        if (qframe > 30)
            qframe = 0;
    }

    if (key.unicode == 'z') {
        qframe--;
        if (qframe < 0)
            qframe = 0;
    }

    if (key.unicode == ' ')
        printf("qanim %i qframe %i\n", qanim, qframe);
#endif

#if 0
    // when directional pathfinding will be implemented this will be used
    // for tests
    if (key == KEY_t) {
        mission_->vehicle(0)->setDirection(
                mission_->vehicle(0)->direction() + 1);
    }

    if (key == KEY_y) {
        mission_->vehicle(0)->setDirection(
                mission_->vehicle(0)->direction() - 1);
    }
#endif

#endif //_DEBUG

    return consumed;
}


void GameplayMenu::drawSelectAllButton() {
    // 64x10
    if(isButtonSelectAllPressed_) {
        g_SpriteMgr.drawSprite(1792, {0, 46 + 44});
        g_SpriteMgr.drawSprite(1793, {64, 46 + 44});
    } else {
        g_SpriteMgr.drawSprite(1796, {0, 46 + 44});
        g_SpriteMgr.drawSprite(1797, {64, 46 + 44});
    }
}

/*!
 * Manage the animation of the hint display.
 * We either display the current action of the squad leader or
 * the mission's objective if mission is ongoing or a status
 * if mission has failed.
 * For objective/status, the font is blinking and there is 
 * a yellow box to accentuate the display
 * @param elapsed 
 */
void GameplayMenu::updateMissionHint(uint32_t elapsed) {
    drawHintYellowRect_ = false;
    hintTimer_.update(elapsed);

    if (hintTimer_.state()) {
        fs_knl::PedInstance *pLeader = selection_.leader();
        if (pLeader->hasDestination()) {
            hint_ = getMessage("HINT_GOING");
        } else {
            hint_ = getMessage("HINT_OBSERVING");
        }
        if (pLeader->wePickupWeapon()) {
            hint_ = getMessage("HINT_PICKUP_WEAPON");
        }
        if (pLeader->isState(fs_knl::PedInstance::pa_smHit)) {
            hint_ = getMessage("HINT_HIT_BY_BULLET");
        }
        hintColor_ = 14;
    } else {
        hintColorTimer_.update(elapsed);
        hintColor_ = hintColorTimer_.state() ? 0 : 11;

        if (mission_->isRunning()) {
            if (target_ && 
                    target_->nature() == fs_knl::MapObject::kNatureWeapon && 
                    target_->isDrawable()) {
                // The player is pointing to a weapon on ground -> display the name of weapon
                hint_ = static_cast<fs_knl::WeaponInstance *>(target_)->name();
                hintColor_ = hintColorTimer_.state() ? 14 : 11;
            } else {
                mission_->objectiveMsg(hint_);
                drawHintYellowRect_ = hintColorTimer_.state();
            }
        } else {
            hintSpaceTimer_.update(elapsed);
            if (hintSpaceTimer_.state()) { // When true, we display the press space message
                hint_ = getMessage("HINT_PRESS_SPACE");
            } else {
                if (mission_->failed()) {
                    hint_ = getMessage("HINT_MISSION_FAILED");
                } else {
                    hint_ = getMessage("HINT_MISSION_COMPLETE");
                }
            }
        }
    }
}

/*!
 * @brief 
 */
void GameplayMenu::drawMissionHint() {
    g_SpriteMgr.drawSprite(1798, {0, 46 + 44 + 10 + 46 + 44 - 1});
    g_SpriteMgr.drawSprite(1799, {64, 46 + 44 + 10 + 46 + 44 - 1});

    if (drawHintYellowRect_) {
        g_System.drawFillRect({0, 46 + 44 + 10 + 46 + 44}, 128, 12, menu_manager_->kMenuColorYellow);
    }

    int width = gameFont()->textWidth(hint_, false);
    int x = 64 - width / 2;
    fs_eng::FSColor aColor;
    menu_manager_->getColorFromMenuPalette(hintColor_, aColor);
    gameFont()->drawText(x, 46 + 44 + 10 + 46 + 44 + 2 - 1, hint_, aColor);
}

//! Size of the Escape menu : it's in the middle of the map area (see escMenuPos())
const int kEscMenuWidth = 300;
const int kEscMenuY = 110;

static Point2D escMenuPos() {
    return {MapRenderer::kGameplayPanelWidth + (mapAreaWidth() - kEscMenuWidth) / 2, kEscMenuY};
}
const int kEscMenuItemsY = 40;
const int kEscMenuItemHeight = 26;
//! Software mode : names for the palettes that can be chosen as time of day (0 = the palette of the mission)
const char *kPaletteNames[] = {"MISSION", "DAY AMBER", "NIGHT", "DAY GREEN", "DUSK RED", "DUSK SAND"};
const int kNbPalettes = 6;

/*!
 * Hardware mode : the time of day is a color grade of the map. The map is multiplied
 * by the color, then the map times "add" is added to make it brighter.
 * The original mission palettes all have about the same brightness once drawn, so
 * the same grade gives the same result whatever the palette of the mission.
 */
struct TimeOfDayGrade {
    const char *name;
    fs_eng::FSColor multiply;
    double add;
    //! Brightness of the scene compared to the mission's own look : sets the strength of the lighting
    double brightness;
};
const TimeOfDayGrade kTimesOfDay[] = {
    {"MISSION", {255, 255, 255, 255}, 0.0,  1.0},
    {"DAY",     {255, 252, 240, 255}, 0.25, 1.2},
    {"DUSK",    {235, 160, 115, 255}, 0.0,  0.7},
    {"NIGHT",   { 70,  82, 140, 255}, 0.0,  0.5},
    {"DAWN",    {250, 195, 200, 255}, 0.0,  0.85}
};
const int kNbTimesOfDay = 5;

void GameplayMenu::showEscMenu(bool show) {
    if (show == escMenuShown_) {
        return;
    }
    escMenuShown_ = show;
    escMenuHover_ = -1;
    if (show) {
        pausedBeforeEscMenu_ = paused_;
        if (!paused_) {
            paused_ = true;
            stopShootingEvent();
            g_MusicMgr.pause();
        }
        g_System.usePointerCursor();
    } else {
        paused_ = pausedBeforeEscMenu_;
        if (!paused_) {
            g_MusicMgr.resume();
        }
        // Restore the cursor for what is under the mouse
        handleMouseMotion({last_motion_x_, last_motion_y_}, 0);
    }
}

void GameplayMenu::drawEscMenu() {
    const int kEscMenuHeight = kEscMenuItemsY + kEscMenuNbItems * kEscMenuItemHeight + 8;
    g_System.drawFillRect(escMenuPos(), kEscMenuWidth, kEscMenuHeight, menu_manager_->kMenuColorBlack);
    g_System.drawRect(escMenuPos(), kEscMenuWidth, kEscMenuHeight, menu_manager_->kMenuColorDarkGreen);
    g_System.drawRect({escMenuPos().x + 1, escMenuPos().y + 1}, kEscMenuWidth - 2, kEscMenuHeight - 2, menu_manager_->kMenuColorDarkGreen);

    fs_eng::MenuFont *pFont = getMenuFont(fs_eng::FontManager::SIZE_2);
    std::string title = getMessage("GAME_PAUSED");
    pFont->drawText(escMenuPos().x + (kEscMenuWidth - pFont->textWidth(title)) / 2, escMenuPos().y + 10, title, true);

    std::string items[kEscMenuNbItems] = {
        "RESUME",
        std::string("TIME OF DAY  ") + (pMapTexture_ ? kTimesOfDay[g_Ctx.getTimeOfDay()].name : kPaletteNames[g_Ctx.getMissionPalette()]),
        std::string("FULLSCREEN  ") + (g_System.isFullScreen() ? "ON" : "OFF"),
        // Lighting only exists in hardware mode
        pMapTexture_ ? std::string("LIGHTING  ") + (g_Ctx.isLightingEnabled() ? "ON" : "OFF") : std::string(""),
        pMapTexture_ ? std::string("LIGHT EDITOR") : std::string(""),
        "ABORT MISSION"
    };
    for (int i = 0; i < kEscMenuNbItems; i++) {
        int y = escMenuPos().y + kEscMenuItemsY + i * kEscMenuItemHeight;
        pFont->drawText(escMenuPos().x + (kEscMenuWidth - pFont->textWidth(items[i])) / 2, y, items[i], i == escMenuHover_);
    }
}

int GameplayMenu::escMenuItemAt(Point2D point) {
    if (point.x < escMenuPos().x || point.x >= escMenuPos().x + kEscMenuWidth) {
        return -1;
    }
    int dy = point.y - (escMenuPos().y + kEscMenuItemsY);
    if (dy < 0) {
        return -1;
    }
    int item = dy / kEscMenuItemHeight;
    return item < kEscMenuNbItems ? item : -1;
}

void GameplayMenu::handleEscMenuClick(int item, int button) {
    switch (item) {
    case kEscMenuResume:
        showEscMenu(false);
        break;
    case kEscMenuTimeOfDay:
        {
            // Left click for the next one, right click for the previous one
            if (pMapTexture_) {
                int step = button == kMouseRightButton ? kNbTimesOfDay - 1 : 1;
                g_Ctx.updateTimeOfDay((g_Ctx.getTimeOfDay() + step) % kNbTimesOfDay);
                applyTimeOfDay();
            } else {
                int step = button == kMouseRightButton ? kNbPalettes - 1 : 1;
                g_Ctx.updateMissionPalette((g_Ctx.getMissionPalette() + step) % kNbPalettes);
                applyMissionPalette();
            }
        }
        break;
    case kEscMenuFullScreen:
        g_System.setFullScreen(!g_System.isFullScreen());
        g_Ctx.updateFullScreen(g_System.isFullScreen());
        break;
    case kEscMenuLighting:
        if (pMapTexture_) {
            g_Ctx.updateLighting(!g_Ctx.isLightingEnabled());
            map_renderer_.setLightingEnabled(g_Ctx.isLightingEnabled());
            applyTimeOfDay();
        }
        break;
    case kEscMenuLightEditor:
        if (pMapTexture_) {
            openLightEditor();
        }
        break;
    case kEscMenuAbort:
        escMenuShown_ = false;
        paused_ = false;
        if (mission_->isRunning()) {
            mission_->endWithStatus(fs_knl::Mission::kMissionStatusAborted);
        }
        // Display only the menu up animation
        menu_manager_->gotoMenu(fs_game_menus::kMenuIdFliMissionAborted);
        break;
    default:
        break;
    }
}

void GameplayMenu::applyMissionPalette() {
    int paletteId = g_Ctx.getMissionPalette() != 0 ? g_Ctx.getMissionPalette() : g_Session.getSelectedBlock().paletteId;
    fs_eng::TileManager *pTileManager = mission_->get_map()->getTileManager();
    pTileManager->setPalette(paletteId);
    missionPalette_ = pTileManager->getPalette();
    g_SpriteMgr.setPalette(missionPalette_);
    mm_renderer_.updatePalette(missionPalette_);
}

void GameplayMenu::applyTimeOfDay() {
    const TimeOfDayGrade &grade = kTimesOfDay[g_Ctx.getTimeOfDay()];

    // Multiplying can only darken : to make the map brighter, part of it is added again
    gradeMultiply_ = grade.multiply;
    uint8_t add = static_cast<uint8_t>(255 * grade.add);
    gradeAdd_ = {add, add, add, 255};
    hasGradeAdd_ = grade.add > 0;

    // The lighting is weaker when the scene is dark
    map_renderer_.setSceneBrightness(grade.brightness);
}

//! The light editor panel is in the top right corner of the map area
const int kLightEditorWidth = 260;
const int kLightEditorItemsY = 40;
const int kLightEditorItemHeight = 18;
const int kLightEditorHeight = kLightEditorItemsY + 6 * kLightEditorItemHeight + 44;

static Point2D lightEditorPos() {
    return {MapRenderer::kGameplayPanelWidth + mapAreaWidth() - kLightEditorWidth - 8, 8};
}
//! Radius of the light in the editor : 0 is automatic, then from kLightEditorMinRadius by steps
const int kLightEditorMinRadius = 20;
const int kLightEditorMaxRadius = 160;
const int kLightEditorRadiusStep = 10;
//! Colours of light that can be chosen in the editor (plus the automatic one, from the tile)
struct LightColourPreset {
    const char *name;
    fs_eng::FSColor colour;
};
const LightColourPreset kLightColours[] = {
    {"WARM",   {255, 215, 150, 255}},
    {"WHITE",  {255, 250, 235, 255}},
    {"COOL",   {170, 210, 255, 255}},
    {"RED",    {255,  70,  60, 255}},
    {"ORANGE", {255, 150,  50, 255}},
    {"GREEN",  {120, 255, 120, 255}},
    {"BLUE",   { 80, 140, 255, 255}},
    {"PINK",   {255, 130, 220, 255}}
};
const int kNbLightColours = 8;

//! Returns the index of the colour preset for the setting : -1 for automatic or a colour not in the presets
static int lightColourIndex(const MapLighting::TileSetting &setting) {
    if (!setting.hasColour) {
        return -1;
    }
    for (int i = 0; i < kNbLightColours; i++) {
        const fs_eng::FSColor &c = kLightColours[i].colour;
        if (c.r == setting.colour.r && c.g == setting.colour.g && c.b == setting.colour.b) {
            return i;
        }
    }
    return -1;
}

void GameplayMenu::openLightEditor() {
    // The editor shows the lighting so it must be on
    if (!g_Ctx.isLightingEnabled()) {
        g_Ctx.updateLighting(true);
        map_renderer_.setLightingEnabled(true);
        applyTimeOfDay();
    }
    // The game stays paused as with the Escape menu
    escMenuShown_ = false;
    escMenuHover_ = -1;
    lightEditor_ = true;
    lightEditorHover_ = -1;
    lightEditorStatus_ = "CLICK ON A TILE";
    g_System.usePointerCursor();
}

void GameplayMenu::closeLightEditor() {
    lightEditor_ = false;
    paused_ = pausedBeforeEscMenu_;
    if (!paused_) {
        g_MusicMgr.resume();
    }
    handleMouseMotion({last_motion_x_, last_motion_y_}, 0);
}

int GameplayMenu::lightEditorItemAt(Point2D point) {
    Point2D pos = lightEditorPos();
    if (point.x < pos.x || point.x >= pos.x + kLightEditorWidth || point.y < pos.y || point.y >= pos.y + kLightEditorHeight) {
        return -1;
    }
    int dy = point.y - (pos.y + kLightEditorItemsY);
    if (dy >= 0 && dy < kLightEditorNbItems * kLightEditorItemHeight) {
        return dy / kLightEditorItemHeight;
    }
    return kLightEditorNbItems;
}

void GameplayMenu::handleLightEditorClick(Point2D point, int button) {
    int item = lightEditorItemAt(point);
    if (item == -1) {
        // A click on the map selects the tile under the mouse
        if (point.x >= MapRenderer::kGameplayPanelWidth) {
            Point2D tilePos;
            const fs_eng::Tile *pTile = map_renderer_.lighting().tileAt(unzoomPoint(point), &tilePos);
            lightEditorTile_ = pTile != nullptr ? pTile->id() : -1;
            lightEditorStatus_ = pTile != nullptr ? "" : "CLICK ON A TILE";
        }
        return;
    }

    MapLighting &lighting = map_renderer_.lighting();
    // Left click for the next value, right click for the previous one
    int step = button == kMouseRightButton ? -1 : 1;
    if (item == kLightEditorSave) {
        lightEditorStatus_ = lighting.saveTileSettings() ? "SAVED" : "COULD NOT SAVE";
        return;
    }
    if (item == kLightEditorClose) {
        closeLightEditor();
        return;
    }
    if (lightEditorTile_ == -1 || item >= kLightEditorNbItems) {
        return;
    }

    MapLighting::TileSetting setting = lighting.tileSetting(lightEditorTile_);
    switch (item) {
    case kLightEditorGlow:
        setting.glow = static_cast<MapLighting::Glow>((static_cast<int>(setting.glow) + 3 + step) % 3);
        break;
    case kLightEditorLight:
        setting.light = static_cast<MapLighting::LightMode>((static_cast<int>(setting.light) + 3 + step) % 3);
        break;
    case kLightEditorRadius:
        {
            // Automatic, then the radius from the smallest to the biggest
            int nbValues = (kLightEditorMaxRadius - kLightEditorMinRadius) / kLightEditorRadiusStep + 2;
            int index = setting.radius == 0 ? 0 : (setting.radius - kLightEditorMinRadius) / kLightEditorRadiusStep + 1;
            index = (std::clamp(index, 0, nbValues - 1) + nbValues + step) % nbValues;
            setting.radius = index == 0 ? 0 : kLightEditorMinRadius + (index - 1) * kLightEditorRadiusStep;
        }
        break;
    case kLightEditorColour:
        {
            // Automatic is -1, then the presets
            int index = (lightColourIndex(setting) + 1 + kNbLightColours + 1 + step) % (kNbLightColours + 1) - 1;
            setting.hasColour = index != -1;
            setting.colour = index != -1 ? kLightColours[index].colour : fs_eng::FSColor {255, 255, 255, 255};
        }
        break;
    default:
        break;
    }
    lighting.setTileSetting(lightEditorTile_, setting);
    lightEditorStatus_ = "NOT SAVED";
}

void GameplayMenu::drawLightEditorTiles() {
    const fs_eng::FSColor kHoverColour = {255, 255, 255, 255};
    const fs_eng::FSColor kSelectedColour = {255, 220, 0, 255};
    // All the places where the selected tile is drawn
    for (const auto &drawn : map_renderer_.drawnTiles()) {
        if (drawn.first->id() == lightEditorTile_) {
            g_System.drawRect(drawn.second, fs_eng::Tile::kTileWidth, fs_eng::Tile::kTileHeight, kSelectedColour);
        }
    }
    Point2D mouse = {last_motion_x_, last_motion_y_};
    if (mouse.x >= MapRenderer::kGameplayPanelWidth && lightEditorItemAt(mouse) == -1) {
        Point2D tilePos;
        if (map_renderer_.lighting().tileAt(unzoomPoint(mouse), &tilePos) != nullptr) {
            g_System.drawRect(tilePos, fs_eng::Tile::kTileWidth, fs_eng::Tile::kTileHeight, kHoverColour);
        }
    }
}

void GameplayMenu::drawLightEditorPanel() {
    Point2D pos = lightEditorPos();
    g_System.drawFillRect(pos, kLightEditorWidth, kLightEditorHeight, menu_manager_->kMenuColorBlack);
    g_System.drawRect(pos, kLightEditorWidth, kLightEditorHeight, menu_manager_->kMenuColorDarkGreen);
    g_System.drawRect(pos.add(1, 1), kLightEditorWidth - 2, kLightEditorHeight - 2, menu_manager_->kMenuColorDarkGreen);

    fs_eng::MenuFont *pFont = getMenuFont(fs_eng::FontManager::SIZE_1);
    pFont->drawText(pos.x + 10, pos.y + 8, "LIGHT EDITOR", true);

    // Under the title, the tile under the mouse and the selected one
    Point2D tilePos;
    const fs_eng::Tile *pHover = nullptr;
    Point2D mouse = {last_motion_x_, last_motion_y_};
    if (mouse.x >= MapRenderer::kGameplayPanelWidth && lightEditorItemAt(mouse) == -1) {
        pHover = map_renderer_.lighting().tileAt(unzoomPoint(mouse), &tilePos);
    }
    std::string tileInfo = lightEditorTile_ != -1 ? "TILE " + std::to_string(lightEditorTile_) : std::string("NO TILE");
    if (pHover != nullptr) {
        tileInfo += "   MOUSE " + std::to_string(pHover->id());
    }
    pFont->drawText(pos.x + 10, pos.y + 22, tileInfo, false);

    std::string values[kLightEditorNbItems];
    if (lightEditorTile_ != -1) {
        MapLighting::TileSetting setting = map_renderer_.lighting().tileSetting(lightEditorTile_);
        const char *kGlowNames[] = {"NONE", "BRIGHT", "FULL"};
        const char *kLightNames[] = {"AUTO", "NONE", "ON"};
        values[kLightEditorGlow] = kGlowNames[static_cast<int>(setting.glow)];
        values[kLightEditorLight] = kLightNames[static_cast<int>(setting.light)];
        values[kLightEditorRadius] = setting.radius == 0 ? std::string("AUTO") : std::to_string(setting.radius);
        int colourIndex = lightColourIndex(setting);
        values[kLightEditorColour] = !setting.hasColour ? std::string("AUTO") :
                colourIndex != -1 ? std::string(kLightColours[colourIndex].name) : std::string("CUSTOM");
        if (setting.hasColour) {
            int y = pos.y + kLightEditorItemsY + kLightEditorColour * kLightEditorItemHeight;
            g_System.drawFillRect({pos.x + kLightEditorWidth - 26, y + 1}, 14, 10, setting.colour);
        }
    }
    const char *kLabels[kLightEditorNbItems] = {"GLOW", "LIGHT", "RADIUS", "COLOUR", "SAVE", "CLOSE"};
    for (int i = 0; i < kLightEditorNbItems; i++) {
        int y = pos.y + kLightEditorItemsY + i * kLightEditorItemHeight;
        bool lighted = i == lightEditorHover_;
        pFont->drawText(pos.x + 10, y, kLabels[i], lighted);
        if (!values[i].empty()) {
            pFont->drawText(pos.x + 90, y, values[i], lighted);
        }
    }
    int y = pos.y + kLightEditorItemsY + kLightEditorNbItems * kLightEditorItemHeight + 4;
    pFont->drawText(pos.x + 10, y, lightEditorStatus_, false);
    pFont->drawText(pos.x + 10, y + 16, "LEFT OR RIGHT CLICK", false);
}

void GameplayMenu::drawPausePanel() {
    std::string str_paused = getMessage("GAME_PAUSED");
    fs_eng::MenuFont *font_used = getMenuFont(fs_eng::FontManager::SIZE_1);
    int txt_width = font_used->textWidth(str_paused);
    // In the middle of the map
    int txt_posx = MapRenderer::kGameplayPanelWidth + mapAreaWidth() / 2 - txt_width / 2;
    int txt_height = font_used->textHeight(false);
    int txt_posy = fs_eng::kScreenHeight / 2 - txt_height / 2;

    g_System.drawFillRect({txt_posx - 10, txt_posy - 5},
        txt_width + 20, txt_height + 10, menu_manager_->kMenuColorBlack);
    fs_eng::FSColor aColor;
    menu_manager_->getColorFromMenuPalette(11, aColor);
    gameFont()->drawText(txt_posx, txt_posy, str_paused, aColor);
}

void GameplayMenu::drawWeaponSelectors() {
    fs_knl::PedInstance *p = NULL;

    p = selection_.leader();

    if (p) {
        bool draw_pw = true;
        for (uint8_t j = 0; j < 2; j++) {
            for (uint8_t i = 0; i < 4; i++) {
                fs_knl::WeaponInstance *wi = NULL;
                int s = 1601;
                // NOTE: weapon selectors can be drawn by drawFrame instead
                // of using current draw(), animations are folowing:
                // 285,286 empty selector :: 287 persuadatron 289
                // 291 pistol 293 :: 295 gauss gun 297 :: 299 shotgun 301
                // 303 uzi 305 :: 307 minigun 309 :: 311 laser gun 313
                // 315 flamer 317 :: 319 long range 321 :: 323 scanner 325
                // 327 medikit 329 :: 331 time bomb 333 :: 343 access card 345
                // 351 energy shield 353

                if (i + j * 4u < p->numWeapons()) {
                    wi = p->weapon(i + j * 4u);
                    s = wi->getClass()->selector();
                    if (p->selectedWeapon() && p->selectedWeapon() == wi)
                        s += 40;
                } else if (draw_pw) {
                    if (target_ && target_->nature() == fs_knl::MapObject::kNatureWeapon
                        && target_->isDrawable())
                    {
                        // player is pointing a weapon on the ground and there's space
                        // in the inventory to display its icon
                        wi = (fs_knl::WeaponInstance *)target_;
                        draw_pw = false;
                        s = wi->getClass()->selector() + 40;
                    }
                }

                g_SpriteMgr.drawSprite(s, {32 * i, 2 + 46 + 44 + 10 + 46 + 44 + 15 + j * 32});

                // draw ammo bars
                if (wi) {
                    int n;

                    if (!wi->usesAmmo())
                        continue;
                    if (wi->ammoCapacity() == 0)
                        n = 25;
                    else
                        n = 25 * wi->ammoRemaining() / wi->ammoCapacity();

                    g_System.drawFillRect({32 * i + 3, 46 + 44 + 10 + 46 + 44 + 15 + j * 32 + 23 + 2},
                        n, 5, menu_manager_->kMenuColorWhiteBlue);
                }
            }
        }
    } else {
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 4; i++) {
                int s = 1601;

                g_SpriteMgr.drawSprite(s, {32 * i, 2 + 46 + 44 + 10 + 46 + 44 + 15 + j * 32});
            }

    }
}

/*!
 * Select the given agent.
 * \param agentNo Agent id
 * \param addToGroup If true, agent is added to the current selection.
 * If not, the selection is emptied and filled with the new agent.
 */
void GameplayMenu::handleAgentKey(size_t agentNo, bool ctrl) {
    uint32_t now = g_System.getTicks();
    bool doublePress = !ctrl && lastAgentKey_ == static_cast<int>(agentNo) &&
                        now - lastAgentKeyTicks_ <= kDoublePressDelay;

    selectAgent(agentNo, ctrl);

    fs_knl::PedInstance *pAgent = mission_->getSquad()->member(agentNo);
    if (doublePress && pAgent && pAgent->isAlive()) {
        centerViewOnPed(pAgent);
        // A third press starts a new double press
        lastAgentKey_ = -1;
    } else {
        lastAgentKey_ = static_cast<int>(agentNo);
    }
    lastAgentKeyTicks_ = now;
}

void GameplayMenu::selectAgent(size_t agentNo, bool addToGroup) {
    // TODO: when agent deselected and he was shooting, he should stop
    if (selection_.selectAgent(agentNo, addToGroup)) {
        updateSelectAll();
        centerMinimapOnLeader();
        highlightLeaderMarker();
        g_SoundMgr.play(fs_eng::SPEECH_SELECTED);
    }
}

/*!
 * Selects all agents.
 * \param invert If true, selects only the agents that were not already
 * selected.
 */
void GameplayMenu::selectAllAgents() {
    bool prv_state = isButtonSelectAllPressed_;
    isButtonSelectAllPressed_ = !isButtonSelectAllPressed_;
    selection_.selectAllAgents(isButtonSelectAllPressed_);
    updateSelectAll();
    if (isButtonSelectAllPressed_ != prv_state) {
        g_SoundMgr.play(fs_eng::SPEECH_SELECTED);
    }
}

/*!
 * Make the current leader marker blinks.
 * All other agents not.
 */
void GameplayMenu::highlightLeaderMarker()
{
    for (size_t i = fs_knl::Squad::kSlot1; i < fs_knl::Squad::kMaxSlot; i++) {
        // draw animation only for leader
        //mission_->sfxObjects(4 + i)->setDrawAllFrames(selection_.getLeaderSlot() == i);
        //mission_->sfxObjects(4 + i)->playMainAnimation();
        if (selection_.getLeaderSlot() == i) {
            mission_->sfxObjects(4 + i)->playMainAnimation();
        } else {
            mission_->sfxObjects(4 + i)->resetAnimation();
        }
    }
}

/**
 * Updating position for visual markers for all agents.
 * \return void
 *
 */
void GameplayMenu::updateMarkersPosition() {
    for (size_t i = 0; i < fs_knl::Squad::kMaxSlot; i++) {
        if (mission_->sfxObjects(i + 4)->isDrawable()) {
            fs_knl::PedInstance *pAgent = mission_->getSquad()->member(i);
            if (pAgent != NULL && pAgent->isAlive()) {
                fs_knl::TilePoint agentPos = pAgent->position();
                agentPos.ox -= 16;
                agentPos.oz += 256;

                mission_->sfxObjects(i + 4)->setPosition(agentPos);
            }
        }
    }
}

/*!
 * This method checks among the squad to see if an agent died and deselects him.
 */
void GameplayMenu::updateSelectionForDeadAgent(fs_knl::PedInstance *pPed) {
    // Deselects dead agent
    selection_.deselectAgent(pPed);
    // hide dead agent's marker
    mission_->sfxObjects(pPed->id() + 4)->setDrawable(false);

    // if selection is empty after agent's death
    // selects the first selectable agent
    if (selection_.size() == 0) {
        for (size_t i = fs_knl::Squad::kSlot1; i < fs_knl::Squad::kMaxSlot; i++) {
            if (selection_.selectAgent(i, false)) {
                // Agent has been selected -> quit
                break;
            }
        }
    }

    // anyway updates markers
    highlightLeaderMarker();
    updateSelectAll();
}

/*!
 * Updates the minimap.
 */
void GameplayMenu::centerMinimapOnLeader() {
    // Centers the minimap on the selection leader
    if (selection_.size() > 0) {
        fs_knl::PedInstance *pAgent = mission_->ped(selection_.getLeaderSlot());
        mm_renderer_.centerOn(pAgent);
    }
}

/*!
 * Update the select all button's state
 */
void GameplayMenu::updateSelectAll() {
    uint8_t nbAgentAlive = 0;
    // count the number of remaining agents
    for (size_t indx = 0; indx < fs_knl::Squad::kMaxSlot; indx++) {
        fs_knl::PedInstance *pAgent = mission_->getSquad()->member(indx);
        if (pAgent && pAgent->isAlive()) {
            nbAgentAlive++;
        }
    }

    // if number of agents alive is the same as number of selected agents
    // then button is pressed.
    isButtonSelectAllPressed_ = ((nbAgentAlive == selection_.size()
        && nbAgentAlive != 0) || nbAgentAlive == 1);
}

/*!
 *
 */
void GameplayMenu::handleWeaponSelection(uint8_t selectorIndex, bool ctrl) {
    fs_knl::PedInstance *pLeader = selection_.leader();

    if (selectorIndex < pLeader->numWeapons()) {
        fs_knl::WeaponInstance *wi = pLeader->weapon(selectorIndex);
        if (pLeader->selectedWeapon() == wi) {
            // Player clicked on an already selected weapon -> deselect
            selection_.deselectWeaponOfSameCategory(wi->getClass());
        } else {
            selection_.selectWeaponFromLeader(selectorIndex, ctrl);
        }
    }
    g_SoundMgr.play(fs_eng::SPEECH_SELECTED);
}

/**
 * Method to intercept game events.
 */
void GameplayMenu::onPoliceWarningEmittedEvent([[maybe_unused]] fs_knl::PoliceWarningEmittedEvent *pEvt) {
    if (canPlayPoliceWarnSound_) {
        // warn
        g_SoundMgr.play(fs_eng::PUTDOWN_WEAPON);
        canPlayPoliceWarnSound_ = false;
    }
}

void GameplayMenu::onAgentDiedEvent(fs_knl::AgentDiedEvent *pEvt) {
    LOG(Log::k_FLG_GAME, "GameplayMenu", "onAgentDiedEvent", ("AgentDiedEvent received"))
    // checking agents, if all are dead -> mission failed
    if (mission_->getSquad()->isAllDead()) {
        mission_->endWithStatus(fs_knl::Mission::kMissionStatusFailed);
    }

    // Anyway update selection
    updateSelectionForDeadAgent(pEvt->pPed);
}

void GameplayMenu::onShootingWeaponSelectedEvent(fs_knl::ShootingWeaponSelectedEvent *pEvt) {
    if (pEvt->isSelected) {
        LOG(Log::k_FLG_GAME, "GameplayMenu", "onShootingWeaponSelectedEvent", ("shooting weapon was selected"))
        fs_knl::PedInstance *pPedSource = pEvt->pPed;
        mission_->addArmedPed(pPedSource);
        for (size_t i = 0; i < mission_->numPeds(); i++) {
            fs_knl::PedInstance *pPed = mission_->ped(i);
            if (pPed != pPedSource) {
                pPed->behaviour().handleBehaviourEvent(fs_knl::Behaviour::kBehvEvtWeaponOut, pPedSource);
            }
        }
    } else {
        LOG(Log::k_FLG_GAME, "GameplayMenu", "onShootingWeaponSelectedEvent", ("shooting weapon was deselected"))
        fs_knl::PedInstance *pPedSource = pEvt->pPed;
        mission_->removeArmedPed(pPedSource);
        for (size_t i = 0; i < mission_->numPeds(); i++) {
            fs_knl::PedInstance *pPed = mission_->ped(i);
            if (pPed != pPedSource) {
                pPed->behaviour().handleBehaviourEvent(fs_knl::Behaviour::kBehvEvtWeaponCleared, pPedSource);
            }
        }
    }
}

/*!
 * Call when receiving the event that tells the mission has ended.
 * @param pEvt MissionEndedEvent
 */
void GameplayMenu::onMissionEndedEvent(fs_knl::MissionEndedEvent *pEvt) {
    if (pEvt->status == fs_knl::Mission::kMissionStatusCompleted) {
        LOG(Log::k_FLG_GAME, "Mission", "endWithStatus()", ("Mission succeeded"));
        g_SoundMgr.play(fs_eng::SPEECH_MISSION_COMPLETED);
        g_MusicMgr.playSong(fs_eng::MusicManager::kMusicSongMissionCompleted);
    } else if (pEvt->status == fs_knl::Mission::kMissionStatusFailed) {
        LOG(Log::k_FLG_GAME, "Mission", "endWithStatus()", ("Mission failed"));
        g_SoundMgr.play(fs_eng::SPEECH_MISSION_FAILED);
        g_MusicMgr.playSong(fs_eng::MusicManager::kMusicSongMissionFailed);
    }
}

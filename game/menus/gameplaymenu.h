/*
 *  FreeSynd - a remake of the classic Bullfrog game "Syndicate".
 *
 *   Copyright (C) 2005  Stuart Binge  <skbinge@gmail.com>
 *   Copyright (C) 2005  Joost Peters  <joostp@users.sourceforge.net>
 *   Copyright (C) 2006  Trent Waddington <qg@biodome.org>
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

#ifndef GAMEPLAYMENU_H
#define GAMEPLAYMENU_H

#include "fs-engine/menus/menumanager.h"
#include "fs-engine/events/event.h"
#include "agentselectorrenderer.h"
#include "maprenderer.h"
#include "minimaprenderer.h"
#include "squadselection.h"


/*!
 * Gameplay Menu class.
 */
class GameplayMenu : public fs_eng::Menu {
public:
    GameplayMenu(fs_eng::MenuManager *m);
    //! Update the menu state
    bool handleTick(uint32_t elapsed) override;
    bool handleBeforeShow() override;
    void handleRender() override;
    void handleLeave() override;
    //! The map uses the whole width of a wide screen
    bool isWideScreen() override { return true; }

protected:
    /**
     * @name Mouse/Key events handling
     */
    ///@{
    bool handleUnMappedKey(const fs_eng::FS_Key key) override;

    void handleMouseMotion(Point2D point, uint32_t state) override;
    bool handleMouseDown(Point2D point, int button) override;
    void handleMouseUp(Point2D point, int button) override;
    //! The mouse wheel zooms the map in hardware mode
    void handleMouseWheel(Point2D point, int delta) override;
    //! Converts a point on the screen into the map coordinates without zoom
    Point2D unzoomPoint(Point2D point);
    //! Handles the user's click on weapon selector
    void handleClickOnWeaponSelector(Point2D point, int button);
    //! sets IPA level for defined agent, selector
    void setIPAForAgent(size_t slot, IPAStim::IPAType ipa_type, uint8_t percentage);
    //! Handles the user's click on the map
    void handleClickOnMap(Point2D point, int button);
    //! Handles the user's click on the minimap
    void handleClickOnMinimap(Point2D point);
    ///@}

    /**
     * @name In game events handling
     */
    ///@{
    //! Handle when an agent has died
    void onAgentDiedEvent(fs_knl::AgentDiedEvent *pEvt);
    //! Handle when a shooting weapon was selected or deselected
    void onShootingWeaponSelectedEvent(fs_knl::ShootingWeaponSelectedEvent *pEvt);
    //!
    void onPoliceWarningEmittedEvent(fs_knl::PoliceWarningEmittedEvent *pEvt);
    //! When mission ends
    void onMissionEndedEvent(fs_knl::MissionEndedEvent *pEvt);
    ///@}

    /**
     * @name State update
     */
    ///@{
    //! Update the target value for adrenaline etc for an agent
    void updateIPALevelMeters(uint32_t elapsed);

    void updateMarkersPosition();
    //! Get the current hint and manage animation
    void updateMissionHint(uint32_t elapsed);
    ///@}

    /**
     * @name Draw screen elements
     */
    ///@{
    //!
    void drawSelectAllButton();
    void drawMissionHint();
    void drawWeaponSelectors();
    void drawPausePanel();
    //! Draws the map, zoomed in hardware mode
    void renderMap();
    /*!
     * While the key B is held, buildings are see-through from the level of the
     * selection leader : returns that level, or -1 when the view is normal.
     */
    int seeThroughLevel();
    //! Shows a marker on the floor where the selection has been ordered to go
    void showMoveMarker(const fs_knl::TilePoint &pos);
    void drawMoveMarker();
    //! Development : saves the sprites with the mission palette in a PNG file, death animations first
    void exportSpriteSheet(const std::string &path);
    //! Draws the menu opened with Escape
    void drawEscMenu();
    ///@}

    /**
     * @name Menu opened with Escape during a mission : resume, time of day, abort
     */
    ///@{
    //! Items of the menu
    enum EscMenuItem {
        kEscMenuResume = 0,
        kEscMenuTimeOfDay = 1,
        kEscMenuFullScreen = 2,
        kEscMenuLighting = 3,
        kEscMenuLightEditor = 4,
        kEscMenuAbort = 5,
        kEscMenuNbItems = 6
    };
    //! Opens or closes the menu. The game is paused while it's open
    void showEscMenu(bool show);
    //! Returns the item under the given point or -1
    int escMenuItemAt(Point2D point);
    void handleEscMenuClick(int item, int button);
    //! Software mode : reloads the palette of the mission when the time of day is changed
    void applyMissionPalette();
    //! Hardware mode : computes the color grade of the map for the time of day
    void applyTimeOfDay();
    ///@}

    /**
     * @name Light editor (hardware mode), opened from the Escape menu : the game is paused,
     * a click on the map selects a tile and the panel changes its lighting settings.
     */
    ///@{
    //! Items of the panel
    enum LightEditorItem {
        kLightEditorGlow = 0,
        kLightEditorLight = 1,
        kLightEditorRadius = 2,
        kLightEditorColour = 3,
        kLightEditorSave = 4,
        kLightEditorClose = 5,
        kLightEditorNbItems = 6
    };
    void openLightEditor();
    void closeLightEditor();
    //! Returns the item of the panel under the given point, -1 if none, kLightEditorNbItems if on the panel
    int lightEditorItemAt(Point2D point);
    void handleLightEditorClick(Point2D point, int button);
    //! Draws the outline of the tile under the mouse and of the selected tile, in the map texture
    void drawLightEditorTiles();
    void drawLightEditorPanel();
    ///@}

    //! Scrolls the view with the keys held down and the edges of the screen. Returns true if it moved
    bool scrollView();
    
    //! Scroll the map horizontally.
    bool scrollOnX();
    //! Scroll the map vertically.
    bool scrollOnY();
    //void improveScroll(int &newScrollX, int &newScrollY);
    void initWorldCoords();
    //! Limits of the view in tiles : the whole map in hardware mode, the mission area in software mode
    int scrollMinX();
    int scrollMaxX();
    int scrollMinY();
    int scrollMaxY();
    //! Moves the view so the given ped is in the middle of the map
    void centerViewOnPed(fs_knl::PedInstance *pPed);
    //! Selects an agent with keys 1 to 4. Pressing the key twice centers the view on the agent
    void handleAgentKey(size_t agentNo, bool ctrl);
    //! Selects/deselects an agent
    void selectAgent(size_t agentNo, bool addToGroup);
    //! Selects/deselects all agents
    void selectAllAgents();
    //! Reacts to a weapon selection/deselection
    void handleWeaponSelection(uint8_t weapon_idx, bool ctrl);

    //! Deselect agent if he died
    void updateSelectionForDeadAgent(fs_knl::PedInstance *p_ped);
    //! updates visual markers for our agents
    void highlightLeaderMarker();
    //! Set pLocWToSet param with point on the map where player clicked to shoot
    bool getAimedAt(int x, int y, fs_knl::WorldPoint *pLocWToSet);
    void stopShootingEvent();
    //! Centers the minimap on the selection leader
    void centerMinimapOnLeader();
    //! Update the select all button state
    void updateSelectAll();

protected:
    /*! Origin of the minimap on the screen.*/
    static const Point2D kMiniMapScreenPos;

    uint32_t tick_count_, last_animate_tick_;
    int last_motion_x_, last_motion_y_;
    //! Text of the hint to display
    std::string hint_;
    //! Color to draw the hint text
    uint8_t hintColor_;
    //! Yellow rect displayed when the objective name is displayed to accentuate text
    bool drawHintYellowRect_;
    /*!
     * Timer to alternate between agent action and mission objective
     * When state is true, we display ped status.
     * When state if false, we display mission's objective
     */
    fs_utl::BoolTimer hintTimer_;
    //! Timer to control blinking color of hint when displaying objectives.
    fs_utl::BoolTimer hintColorTimer_;
    //! Timer to control the display of Press space message when mission is over
    fs_utl::BoolTimer hintSpaceTimer_;

    fs_knl::Mission *mission_;

    /*! This is a projection in 2D of a point on a the map. This point represents the top
     * left corner of the screen and it moves inside the map's borders.*/
    Point2D displayOriginPt_;
    /*! Holds the amount of scroll on the X axis.*/
    int scroll_x_;
    /*! Holds the amount of scroll on the Y axis.*/
    int scroll_y_;
    /*! Agent selection manager.*/
    SquadSelection selection_;
    /*! Object mouse cursor is above*/
    fs_knl::ShootableMapObject *target_;
    //! The palette of colors used for this mission
    fs_eng::Palette missionPalette_;
    /*! This renderer is in charge of drawing the map.*/
    MapRenderer map_renderer_;
    /*!
     * Hardware mode : the map is drawn in this texture, with the coordinates
     * of the original game, then a part of it is scaled on screen for the zoom.
     */
    std::unique_ptr<fs_eng::FSTexture> pMapTexture_;
    //! Maximum zoom of the map
    static const double kMaxZoom;
    //! Zoom of the map : 1 is the original size
    double zoom_;
    //! True when the menu opened with Escape is shown
    bool escMenuShown_;
    //! Item of the Escape menu under the mouse or -1
    int escMenuHover_;
    //! True if the game was paused (with P) before the Escape menu was opened
    bool pausedBeforeEscMenu_;
    //! True when the light editor is shown
    bool lightEditor_ = false;
    //! Id of the tile edited in the light editor or -1
    int lightEditorTile_ = -1;
    //! Item of the light editor panel under the mouse or -1
    int lightEditorHover_ = -1;
    //! Message shown at the bottom of the light editor panel
    std::string lightEditorStatus_;
    //! Where the last move order goes and when it was given (0 when no marker is shown)
    fs_knl::TilePoint moveMarkerPos_;
    uint32_t moveMarkerTicks_ = 0;
    /*!
     * Hardware mode : color grade of the map for the time of day. The map is drawn
     * multiplied by gradeMultiply_, then gradeAdd_ times the map is added to brighten it.
     */
    fs_eng::FSColor gradeMultiply_;
    fs_eng::FSColor gradeAdd_;
    bool hasGradeAdd_;
    //! Agent selected with the last key 1 to 4 (-1 if none) and when, to detect a double press
    int lastAgentKey_;
    uint32_t lastAgentKeyTicks_;
    //! Top left corner of the part of the map shown when zoomed, in coordinates without zoom
    double zoomOriginX_;
    double zoomOriginY_;
    /*! This renderer is in charge of drawing the minimap.*/
    GamePlayMinimapRenderer mm_renderer_;
    /*! This renderer is in charge of drawing the IPA meters.*/
    AgentSelectorRenderer agt_sel_renderer_;
    //! A flag to keep track of the state of the select all button
    bool isButtonSelectAllPressed_;
    /*! Flag to store the fact that player is currently shooting.*/
    bool isPlayerShooting_;
    /*! Flag to play the sound of police warning an agent.*/
    bool canPlayPoliceWarnSound_;
    /*! Delay between 2 police warnings.*/
    fs_utl::Timer warningTimer_;

    //! Handles used for removing listener
    ListenerHandle handleAgentDied_;
    ListenerHandle handleWeaponSelected_;
    ListenerHandle handleAgentWarned_;
    ListenerHandle handleMissionEnded_;

    // when ipa is manipulated this represents
    struct IPA_manipulation {
        // ipa that is manipulated
        int8_t ipa_chng;
        // agent to base IPA's update on
        size_t agent_used;
    } ipa_chng_;
};

#endif

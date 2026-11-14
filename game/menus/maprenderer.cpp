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

#include "menus/maprenderer.h"

#include <cmath>

#include "fs-engine/gfx/animationmanager.h"
#include "fs-engine/gfx/tilemanager.h"
#include "fs-engine/system/system.h"
#include "fs-kernel/model/mission.h"
#include "fs-kernel/model/vehicle.h"
#include "fs-kernel/model/static.h"
#include "fs-kernel/model/squad.h"
#include "fs-kernel/mgr/agentmanager.h"

#include "menus/squadselection.h"
#include "fs-engine/config.h"

const int MapRenderer::kGameplayPanelWidth = 129;
//! Lights of objects are above the position where the object is drawn (on a wall)
const int kObjectLightHeight = -24;
//! Height of the weapon above the feet of a ped, on screen
const int kMuzzleHeight = -18;
//! Headlights : how far from the centre of the vehicle and how high, on screen
const int kHeadlightDistance = 26;
const int kHeadlightHeight = -8;
//! Opacity of the tiles that are see-through (key B held)
const uint8_t kSeeThroughAlpha = 60;
//! Colour of the outline of where peds can walk
const fs_eng::FSColor kWalkableOutlineColour = {90, 255, 90, 255};
//! Colour of the drops of blood
const fs_eng::FSColor kBloodColour = {150, 10, 10, 255};
//! Size of the drops of blood in pixels
const int kBloodDropSize = 2;

/*!
 * Returns the move on screen, in pixels, for a move of the given length on the map
 * in the given direction (0 to 255, 0 is along the Y axis, 64 along the X axis).
 */
int MapRenderer::directionOnScreenX(int direction, int length) {
    double angle = direction * 2.0 * fs_utl::kPI / 256.0;
    // On the map : x = sin, y = cos. On screen, X moves right and down, Y moves left and down
    return static_cast<int>((std::sin(angle) - std::cos(angle)) * length * 0.7071);
}

int MapRenderer::directionOnScreenY(int direction, int length) {
    double angle = direction * 2.0 * fs_utl::kPI / 256.0;
    return static_cast<int>((std::sin(angle) + std::cos(angle)) * length * 0.7071 / 2);
}

Point2D MapRenderer::floorPointOnScreen(const fs_knl::TilePoint &pos, const Point2D &viewport) {
    Point2D mapPos;
    pMap_->tileToScreenPoint(fs_knl::TilePoint(pos.tx, pos.ty, 0, pos.ox, pos.oy), &mapPos);
    // Same as the feet of a ped standing at this point
    return {mapPos.x - viewport.x + kGameplayPanelWidth,
            mapPos.y - viewport.y + fs_eng::Tile::kTileHeight / 3 - pos.tz * fs_eng::Tile::kTileHeight / 3
                - pos.oz * fs_eng::Tile::kTileHeight / 3 / 128};
}

Point2D MapRenderer::tileCornerOnScreen(int x, int y, int z, const Point2D &viewport) {
    Point2D mapPos;
    pMap_->tileToScreenPoint(fs_knl::TilePoint(x, y, 0, 0, 0), &mapPos);
    // Same as the feet of a ped standing at the level z
    return {mapPos.x - viewport.x + kGameplayPanelWidth,
            mapPos.y - viewport.y + fs_eng::Tile::kTileHeight / 3 - z * fs_eng::Tile::kTileHeight / 3};
}

/*!
 * The outline goes along the sides of walkable tiles that have no walkable neighbour
 * on the same level (or just above or below, for stairs).
 */
void MapRenderer::drawWalkableOutline(const Point2D &viewport) {
    if (seeThroughLevel_ < 0) {
        return;
    }
    const int z = seeThroughLevel_;
    auto walkableNear = [this, z](int x, int y) {
        return pMission_->isWalkable(x, y, z) || pMission_->isWalkable(x, y, z - 1) || pMission_->isWalkable(x, y, z + 1);
    };
    for (int y = 0; y < pMap_->maxY(); y++) {
        for (int x = 0; x < pMap_->maxX(); x++) {
            if (!pMission_->isWalkable(x, y, z)) {
                continue;
            }
            Point2D topLeft = tileCornerOnScreen(x, y, z, viewport);
            if (topLeft.x < kGameplayPanelWidth - 64 || topLeft.x > g_System.screenWidth() + 64 ||
                    topLeft.y < -64 || topLeft.y > fs_eng::kScreenHeight + 64) {
                continue;
            }
            Point2D right = tileCornerOnScreen(x + 1, y, z, viewport);
            Point2D bottom = tileCornerOnScreen(x + 1, y + 1, z, viewport);
            Point2D left = tileCornerOnScreen(x, y + 1, z, viewport);
            if (!walkableNear(x, y - 1)) {
                g_System.drawLine(topLeft, right, kWalkableOutlineColour);
            }
            if (!walkableNear(x + 1, y)) {
                g_System.drawLine(right, bottom, kWalkableOutlineColour);
            }
            if (!walkableNear(x, y + 1)) {
                g_System.drawLine(bottom, left, kWalkableOutlineColour);
            }
            if (!walkableNear(x - 1, y)) {
                g_System.drawLine(left, topLeft, kWalkableOutlineColour);
            }
        }
    }
}

/*!
 * Draws the drops of blood sprayed by hits.
 */
void MapRenderer::drawBloodDrops(const Point2D &viewport) {
    for (const fs_knl::Mission::BloodDrop &drop : pMission_->bloodDrops()) {
        int x = static_cast<int>(drop.x);
        int y = static_cast<int>(drop.y);
        // tileToScreenPoint only uses the position on the ground : the height is applied below
        fs_knl::TilePoint tilePos(x / 256, y / 256, 0, x % 256, y % 256);
        Point2D mapPos;
        pMap_->tileToScreenPoint(tilePos, &mapPos);
        Point2D screenPos = {mapPos.x - viewport.x + kGameplayPanelWidth,
                             mapPos.y - viewport.y + fs_eng::Tile::kTileHeight / 3 - static_cast<int>(drop.z) * fs_eng::Tile::kTileHeight / 3 / 128};
        // The drop fades out quickly
        fs_eng::FSColor colour = kBloodColour;
        colour.a = static_cast<uint8_t>(255 * drop.life / drop.maxLife);
        // Each drop is a 2x2 square, like the pixels of the original art
        Point2D square[4] = {screenPos, screenPos.add(kBloodDropSize, 0), screenPos.add(kBloodDropSize, kBloodDropSize), screenPos.add(0, kBloodDropSize)};
        g_System.drawFillPolygon(square, 4, colour);
    }
}

void MapRenderer::init(fs_knl::Mission *pMission, SquadSelection *pSelection, bool withLighting, bool withEffects) {
    withEffects_ = withEffects;
    pMission_ = pMission;
    pMap_ = pMission->get_map();
    pSelection_ = pSelection;
    lighting_.init(pMap_, withLighting);
}

/**
 * Draw tiles and map objects.
 */
void MapRenderer::render(const Point2D &viewport) {
    lighting_.clearLights();
    glowingObjects_.clear();
    // TODO: list of bugs to fix in rendering
    //  - Some advert panels lack a corner
    fs_knl::TilePoint mtp = pMap_->screenToTilePoint(viewport.x, viewport.y);
    int sw = mtp.tx;
    int chk = g_System.screenWidth() / (fs_eng::Tile::kTileWidth / 2) + 2
        + fs_eng::kScreenHeight / (fs_eng::Tile::kTileHeight / 3) + pMap_->maxZ() * 2;
    int sh = mtp.ty - 8;

    int shm = sh + chk;

    DEBUG_SPEED_INIT

    listObjectsToDraw(viewport);

    int cmw = viewport.x + g_System.screenWidth() -
                kGameplayPanelWidth + 128;
    int cmh = viewport.y + fs_eng::kScreenHeight + 128;
    int cmx = viewport.x - kGameplayPanelWidth;
     //  z = 0 - is minimap data and mapdata
    int chky = sh < 0 ? 0 : sh;
    int zr = shm + pMap_->maxZ() + 1;
    for (int inc = 0; inc < zr; ++inc) {
        int ye = sh + inc;
        int ys = ye - pMap_->maxZ() - 2;
        int tile_z = pMap_->maxZ() + 1;  // the Z coord of the next tile to draw
        for (int yb = ys; yb < ye; ++yb) {
            if (yb < 0 || yb < sh || yb >= shm) {
                --tile_z;
                continue;
            }
            int tile_y = yb;  // The Y coord of the tile to draw
            for (int tile_x = sw; tile_y >= chky && tile_x < pMap_->maxX(); ++tile_x) {
                if (tile_x < 0 || tile_y >= pMap_->maxY()) {
                    --tile_y;
                    continue;
                }
                int screen_w = (pMap_->maxX() + (tile_x - tile_y)) * (fs_eng::Tile::kTileWidth / 2);
                int coord_h = ((pMap_->maxZ() + tile_x + tile_y) - (tile_z - 1)) * (fs_eng::Tile::kTileHeight / 3);
                if (screen_w >= viewport.x - fs_eng::Tile::kTileWidth * 2
                    && screen_w + fs_eng::Tile::kTileWidth * 2 < cmw
                    && coord_h >= viewport.y - fs_eng::Tile::kTileHeight * 2
                    && coord_h + fs_eng::Tile::kTileHeight * 2 < cmh) {
#if 0
                    if (z > 2)
                        continue;
#endif
                    // draw a tile
                    if (tile_z < pMap_->maxZ()) {
                        fs_eng::Tile *pTile = pMap_->getTileAt(tile_x, tile_y, tile_z);
                        if (pTile->notTransparent()) {
                            int dx = 0, dy = 0;
                            if (screen_w - viewport.x < 0)
                                dx = -(screen_w - viewport.x);
                            if (coord_h - viewport.y < 0)
                                dy = -(coord_h - viewport.y);
                            if (dx < fs_eng::Tile::kTileWidth && dy < fs_eng::Tile::kTileHeight) {
                                if (lighting_.isEnabled()) {
                                    pMap_->getTileManager()->setColorModulation(lighting_.tileColour(pTile, tile_x, tile_y, tile_z));
                                }
                                if (seeThroughLevel_ >= 0) {
                                    pMap_->getTileManager()->setAlpha(tile_z >= seeThroughLevel_ ? kSeeThroughAlpha : 255);
                                }
                                pMap_->getTileManager()->drawTile(pTile, screen_w - cmx, coord_h - viewport.y);
                                if (lighting_.isEnabled()) {
                                    lighting_.addTileLights(pTile, {screen_w - cmx, coord_h - viewport.y});
                                }
                            }
                        }
                    }

                    // draw everything that's on the tile
                    if (tile_z - 1 >= 0) {
                        fs_knl::TilePoint currentTile(tile_x, tile_y, tile_z - 1);
                        Point2D screenPos = {screen_w - cmx + fs_eng::Tile::kTileWidth / 2,
                            coord_h - viewport.y + fs_eng::Tile::kTileHeight / 3 * 2};

                        drawObjectsOnTile(currentTile, screenPos);
                    }
                }
                --tile_y;
            }
            --tile_z;
        }
    }

    freeUnreleasedResources();
    if (seeThroughLevel_ >= 0) {
        pMap_->getTileManager()->setAlpha(255);
    }
    if (withEffects_) {
        drawBloodDrops(viewport);
    }
    if (lighting_.isEnabled()) {
        pMap_->getTileManager()->setColorModulation({255, 255, 255, 255});
        g_AnimMgr.setColorModulation({255, 255, 255, 255});
    }

#ifdef _DEBUG
    if (g_System.getKeyModState() & fs_eng::KMD_LALT) {
        fs_eng::FSColor yellow {227, 219, 40, 0xFF};
        for (SquadSelection::Iterator it = pSelection_->begin();
            it != pSelection_->end(); ++it) {
            (*it)->showPath(viewport.x, viewport.y, yellow);
        }
    }
#endif

    DEBUG_SPEED_LOG("MapRenderer::render")
}

/*!
 * Lit windows and neon signs emit light. The glass of windows shines.
 */
void MapRenderer::addObjectLight(fs_knl::MapObject *pObject, const Point2D &screenPos) {
    if (pObject->is(fs_knl::MapObject::kNaturePed)) {
        // The flash of a weapon lights up the area and glows even by day
        fs_eng::FSColor flash;
        if (static_cast<fs_knl::PedInstance *>(pObject)->hasMuzzleFlash(&flash)) {
            // Peds are drawn with their offset in the tile
            Point2D gunPos = pObject->drawPosition(screenPos).add(directionOnScreenX(pObject->direction(), 8),
                                           kMuzzleHeight + directionOnScreenY(pObject->direction(), 8));
            lighting_.addLight(gunPos, 80, flash);
            lighting_.addGlow(gunPos, 12, flash);
        }
        return;
    }
    if (pObject->is(fs_knl::MapObject::kNatureVehicle)) {
        fs_knl::Vehicle *pVehicle = static_cast<fs_knl::Vehicle *>(pObject);
        if (!pVehicle->isDead()) {
            // Headlights : a cone of light in front of the vehicle. Vehicles are drawn
            // lower than other objects, with their offset in the tile, and their
            // sprites only face 4 directions
            int dir = pObject->getDiscreteDirection(4) * 64;
            Point2D centre = pObject->drawPosition(screenPos.add(0, fs_eng::Tile::kTileHeight / 3));
            Point2D front = centre.add(directionOnScreenX(dir, kHeadlightDistance),
                                       kHeadlightHeight + directionOnScreenY(dir, kHeadlightDistance));
            double angle = std::atan2(directionOnScreenY(dir, 100), directionOnScreenX(dir, 100)) * 180.0 / fs_utl::kPI;
            lighting_.addConeLight(front, angle, 150, {255, 245, 215, 255});
            lighting_.addLight(front, 14, {255, 245, 215, 255});
            // Rear lights
            Point2D back = centre.add(-directionOnScreenX(dir, kHeadlightDistance),
                                      kHeadlightHeight - directionOnScreenY(dir, kHeadlightDistance));
            lighting_.addLight(back, 16, {255, 40, 30, 255});
        }
        return;
    }
    if (!pObject->is(fs_knl::MapObject::kNatureStatic)) {
        return;
    }
    fs_knl::Static *pStatic = static_cast<fs_knl::Static *>(pObject);
    if (pStatic->type() == fs_knl::Static::smt_AnimatedWindow &&
            static_cast<fs_knl::AnimWindow *>(pStatic)->isLightOn()) {
        lighting_.addLight(screenPos.add(0, kObjectLightHeight), 40, {255, 215, 150, 255});
        // The lit window is painted in the tiles of the wall : its bright pixels shine
        Point2D topLeft;
        int width, height;
        static_cast<fs_knl::AnimWindow *>(pStatic)->windowBounds(screenPos, &topLeft, &width, &height);
        lighting_.addWindowGlow(topLeft, width, height);
    } else if (pStatic->type() == fs_knl::Static::smt_Window) {
        // The glass of windows shines
        glowingObjects_.push_back({pObject, screenPos});
    } else if (pStatic->type() == fs_knl::Static::smt_NeonSign) {
        lighting_.addLight(screenPos.add(0, kObjectLightHeight), 56, {255, 130, 220, 255});
    }
}

int MapRenderer::tileHashKey(fs_knl::MapObject * m) {
    return tileHashKey(m->position());
}

void MapRenderer::listObjectsToDraw(const Point2D &viewport) {
    /*if (tilex < 0)
        tilex = 0;
    if (tiley < 0)
        tiley = 0;
    if (maxtilex >= pMap_->maxX())
        maxtilex = pMap_->maxX();
    if (maxtiley >= pMap_->maxY())
        maxtiley = pMap_->maxY();*/


    // Include peds
    for (size_t i = 0; i < pMission_->numPeds(); i++) {
        fs_knl::PedInstance *pPed = pMission_->ped(i);
        if (pPed->isDrawable() && isObjectInsideDrawingArea(pPed, viewport)) {
            addObjectToDraw(pPed);
        }
    }

    // vehicles
    for (size_t i = 0; i < pMission_->numVehicles(); i++) {
        fs_knl::Vehicle *pVehicle = pMission_->vehicle(i);
        if (isObjectInsideDrawingArea(pVehicle, viewport)) {
            addObjectToDraw(pVehicle);
        }
    }

    // weapons
    for (size_t i = 0; i < pMission_->numWeaponsOnGround(); i++) {
        fs_knl::WeaponInstance *pWeapon = pMission_->weaponOnGround(i);
        if (pWeapon->isDrawable() && isObjectInsideDrawingArea(pWeapon, viewport)) {
            addObjectToDraw(pWeapon);
        }
    }

    // statics
    for (size_t i = 0; i < pMission_->numStatics(); i++) {
        fs_knl::Static *pStatic = pMission_->statics(i);
        if (isObjectInsideDrawingArea(pStatic, viewport)) {
            addObjectToDraw(pStatic);
        }
    }

    // sfx objects
    for (size_t i = 0; i < pMission_->numSfxObjects(); i++) {
        fs_knl::SFXObject *pSfx = pMission_->sfxObjects(i);
        if (pSfx->isDrawable() && isObjectInsideDrawingArea(pSfx, viewport)) {
            addObjectToDraw(pSfx);
        }
    }
}

/**
 * Return true if the object appears on the screen and so should be drawn.
 * \param pObject MapObject*
 * \param viewport const Point2D&
 * \return bool
 *
 */
bool MapRenderer::isObjectInsideDrawingArea(fs_knl::MapObject *pObject, const Point2D &viewport) {
    Point2D objectViewport;
    pMission_->get_map()->tileToScreenPoint(pObject->position(), &objectViewport);

    // Limits are larger than screen size in order to have a smooth display
    // of appearance/disappearance of objects on screen. Otherwise they popup when
    // entering the display screen.
    return  objectViewport.x > (viewport.x - fs_eng::Tile::kTileWidth / 2) && objectViewport.y > viewport.y &&
            objectViewport.x <= (viewport.x + g_System.screenWidth() - kGameplayPanelWidth + 10) &&
            objectViewport.y <= (viewport.y + fs_eng::kScreenHeight + pObject->position().tz * 48);
}

/**
 * Draw all objects on the given tile.
 * \param tilePos const TilePoint& tile coordinates
 * \param screenPos const Point2D& position of tile on the screen
 * \return int number of objects for debug
 *
 */
int MapRenderer::drawObjectsOnTile(const fs_knl::TilePoint & tilePos, const Point2D &screenPos) {
    int tileKey = tileHashKey(tilePos);
    int nbDrawnObjects = 0;

    std::map<int, ObjectToDraw *>::iterator it = objectsByTile_.find(tileKey);
    if(it != objectsByTile_.end()) {
        ObjectToDraw *pObj = it->second;
        objectsByTile_.erase(it);
        if (lighting_.isEnabled()) {
            g_AnimMgr.setColorModulation(lighting_.objectColour(tilePos.tx, tilePos.ty, tilePos.tz));
        }
        while(pObj != NULL) {
            pObj->getObject()->draw(screenPos);
            if (lighting_.isEnabled()) {
                addObjectLight(pObj->getObject(), screenPos);
            }
            ObjectToDraw *pNext = pObj->getNext();
            pool_.releaseResource(pObj);
            pObj = pNext;
            nbDrawnObjects++;
        }
    }

    return nbDrawnObjects;
}


/**
 * Adds an object to the list of objects to draw for the tile it's on.
 * For a given tile object are sorted from back to front so that
 * objects in the back are drawn first.
 * \param pObjectToAdd MapObject* Object to add
 * \return void
 *
 */
void MapRenderer::addObjectToDraw(fs_knl::MapObject *pObjectToAdd) {
    int tileKey;
    ObjectToDraw *pNewEntry = pool_.getResource();
    pNewEntry->setObject(pObjectToAdd);

    if (pObjectToAdd->is(fs_knl::MapObject::kNatureVehicle)) {
        // vehicle are associated with the tile just above (z+1)
        // because it is bigger than a tile so all tiles below must be drawn first
        fs_knl::TilePoint vehiclePos( pObjectToAdd->position());
        vehiclePos.tz += 1;
        tileKey = tileHashKey(vehiclePos);
    } else {
        tileKey = tileHashKey(pObjectToAdd);
    }

    std::map<int, ObjectToDraw *>::iterator element = objectsByTile_.find(tileKey);
    if(element == objectsByTile_.end()) {
        // no element has been set with the tile so add the first element
        objectsByTile_[tileKey] = pNewEntry;
    } else {
        // there is at leastone element already set with the tile
        ObjectToDraw *pObjectInList = element->second;
        if (pObjectToAdd->isBehindObjectOnSameTile(pObjectInList->getObject())) {
            // first case is when the new object should be first in the list
            pNewEntry->setNext(pObjectInList);
            objectsByTile_[tileKey] = pNewEntry;
        } else {
            // second case is when new object is somewhere in the list
            while (pObjectInList != NULL) {
                if (pObjectInList->getNext() != NULL) {
                    if (pObjectToAdd->isBehindObjectOnSameTile(pObjectInList->getNext()->getObject())) {
                        pObjectInList->insertNext(pNewEntry);
                        break;
                    } else {
                        pObjectInList = pObjectInList->getNext();
                    }
                } else {
                    pObjectInList->setNext(pNewEntry);
                    break;
                }
            }
        }
    }
}

/**
 * Objects that were listed for drawing may be bigger than objects really drawn.
 * So remove those objects from the list.
 * \return void
 *
 */
void MapRenderer::freeUnreleasedResources() {
    int nbFreed = 0;
    std::map<int, ObjectToDraw *>::iterator itr = objectsByTile_.begin();
    while (itr != objectsByTile_.end()) {
        std::map<int, ObjectToDraw *>::iterator toErase = itr;
        ++itr;
        ObjectToDraw *pObj = toErase->second;
        while(pObj != NULL) {
            ObjectToDraw *pNext = pObj->getNext();
            pool_.releaseResource(pObj);
            nbFreed++;
            pObj = pNext;
        }
        objectsByTile_.erase(toErase);
    }
}

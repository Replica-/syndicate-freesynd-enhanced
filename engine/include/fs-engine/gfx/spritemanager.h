/*
 *  FreeSynd - a remake of the classic Bullfrog game "Syndicate".
 *
 *   Copyright (C) 2005  Stuart Binge  <skbinge@gmail.com>
 *   Copyright (C) 2005  Joost Peters  <joostp@users.sourceforge.net>
 *   Copyright (C) 2006  Trent Waddington <qg@biodome.org>
 *   Copyright (C) 2024-2025  Benoit Blancard <benblan@users.sourceforge.net>
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

#ifndef SPRITEMANAGER_H
#define SPRITEMANAGER_H

#include <list>
#include <vector>
#include <memory>
#include <stack>

#include "sprite.h"
#include "fs-utils/misc/singleton.h"
#include "fs-engine/gfx/fstexture.h"

namespace fs_eng {
/*!
 * This class is used to track the position of a Sprite in
 * the Spriteset texture.
 */
class SpriteInsert {
public:
    //! Position where the sprite is located
    Point2D insertedAt;
    //! Width of the sprite
    int width;
    //! Height of the sprite
    int height;
};

/*!
 * Sprite manager class.
 */
class SpriteManager {
public:
    /*!
     * This is the width for the texture that holds the sprites.
     * It has be chosen to use 512x512 because it is a power of 2
     * enough to hold all sprites.
     */
    static const size_t kMenuSpritesTextureWidth;

    SpriteManager(bool rle, size_t textureWidth);
    virtual ~SpriteManager();
    //! clear all loaded sprites
    void clear();

    //! Loads sprites from the given files
    void loadSprites(const std::string &tabFile, const std::string &datFile, const fs_eng::Palette &palette);
    //! Returns true if this manager has loaded files
    bool loaded() { return spriteCount_ != 0; }
    //! Returns the number of loaded sprites
    int spriteCount() { return spriteCount_; }

    //! Change the palette
    bool setPalette(const fs_eng::Palette &newPalette);
    void setColorModulation( fs_eng::FSColor color ) { spritesetTexture_->setColorModulation(color); }
    void setBlendMode(FSTexture::BlendMode mode) { spritesetTexture_->setBlendMode(mode); }
    //! Return the sprite at given index
    Sprite *sprite(int spriteNum);

    //! Draw a sprite with given Id at given position on screen. Sprite can be flipped or stretched
    bool drawSprite(int spriteNum, int x, int y, bool flipped = false, bool x2 = false);

    bool drawTexture(Point2D src, Point2D dst, int width, int height);

    /*!
     * Creates a mask to recolor the pixels of the given color index in the given
     * sprites. The mask can then be drawn with any color, not limited by the palette.
     */
    bool createTintMask(const std::vector<int> &spriteIds, uint8_t colorIndex);
    //! Returns true if the sprite has pixels in the tint mask
    bool hasTintMask(int spriteNum) {
        return spriteNum >= 0 && spriteNum < static_cast<int>(tintedSprites_.size()) && tintedSprites_[spriteNum];
    }
    //! Draws the tint mask of the sprite with the given color, on top of the sprite
    void drawSpriteTint(int spriteNum, int x, int y, bool flipped, fs_eng::FSColor color);

protected:

    //! Load sprites from the given files
    bool loadSprites(const uint8_t * tabData, const uint8_t * spriteData, const fs_eng::Palette &palette);
    //! Sort tab entries from the tab file by height then width
    void readAndSortTabEntries(const uint8_t * tabData, std::list<SpriteTabEntry>& spriteList);
    //! Read a sprite pixels and copy those pixels at the right location in the sprite set buffer
    Sprite readSpriteDataAndCopyToBuffer(const uint8_t *spriteData, SpriteTabEntry entry, std::stack<SpriteInsert> &spriteStack, uint8_t *spriteBuffer);
    //! Compute the location where to place the sprite in the buffer
    void getInsertPoint(Sprite &sprite, std::stack<SpriteInsert> &spriteStack);

protected:
    //! The list of sprites
    Sprite *sprites_;
    //! Total number of sprites
    int spriteCount_;
    //! True means the sprites are stored using RLE in the original file
    bool isRle_;
    //! A texture that store all the sprites with an optimization of placements
    std::unique_ptr<FSTexture> spritesetTexture_;
    //! Size of the texture that holds the spriteset
    size_t textureWidth_;
    //! Color indices of the spriteset, kept to build tint masks
    std::vector<uint8_t> pixels_;
    //! The pixels to recolor, in white (see createTintMask())
    std::unique_ptr<FSTexture> tintTexture_;
    //! Tells for each sprite if it has pixels in the tint mask
    std::vector<bool> tintedSprites_;
};
}

#endif

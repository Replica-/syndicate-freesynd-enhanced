/*
 *  FreeSynd - a remake of the classic Bullfrog game "Syndicate".
 *
 *   Copyright (C) 2005  Stuart Binge  <skbinge@gmail.com>
 *   Copyright (C) 2005  Joost Peters  <joostp@users.sourceforge.net>
 *   Copyright (C) 2006  Trent Waddington <qg@biodome.org>
 *   Copyright (C) 2006  Tarjei Knapstad <tarjei.knapstad@gmail.com>
 *   Copyright (C) 2010  Bohdan Stelmakh <chamel@users.sourceforge.net>
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

#include "fs-engine/gfx/logomanager.h"

#include <climits>

#include "fs-utils/log/log.h"
#include "fs-utils/io/file.h"
#include "fs-engine/appcontext.h"
#include "fs-engine/system/system.h"

namespace fs_eng {

fs_eng::PaletteMenuColorIndex g_Colours[8] = { 
    fs_eng::kMenuPaletteColorRed,
    fs_eng::kMenuPaletteColorBlueGrey,
    fs_eng::kMenuPaletteColorOrange, 
    fs_eng::kMenuPaletteColorLightOrange,
    fs_eng::kMenuPaletteColorYellow,
    fs_eng::kMenuPaletteColorWhiteBlue,
    fs_eng::kMenuPaletteColorLightBrown,
    fs_eng::kMenuPaletteColorDarkBlue
 };

//! Company colors added in hardware mode, after the original ones
const fs_eng::FSColor g_ExtraColours[] = {
    {200,  20,  40, 255},   // Crimson
    {255,  40, 130, 255},   // Hot pink
    {255, 140, 190, 255},   // Pink
    {220,  40, 200, 255},   // Magenta
    {130,  40, 200, 255},   // Purple
    {180, 120, 255, 255},   // Lavender
    { 90, 170, 255, 255},   // Sky blue
    { 40, 220, 230, 255},   // Cyan
    {  0, 150, 150, 255},   // Teal
    {120, 240, 190, 255},   // Mint
    { 20, 170,  80, 255},   // Emerald
    {150, 230,  40, 255},   // Lime
    {240, 190,  30, 255},   // Gold
    {240, 240, 240, 255},   // White
    {160, 165, 175, 255},   // Silver
    { 60,  60,  70, 255}    // Charcoal
};

const int LogoManager::kMaxColour = 8;
const int LogoManager::kLogoBigWidth = 32;
const int LogoManager::kLogoSmallWidth = 16;
const int LogoManager::kNumOfLogosPerRow = 32;

LogoManager::LogoManager()
: data_all_logos_(nullptr),
  logoPositions_(nullptr)
{
}

LogoManager::~LogoManager()
{
    if (data_all_logos_)
        delete[] data_all_logos_;

    if (logoPositions_)
        delete[] logoPositions_;
}

/*!
 * Load logos from original game files
 * @param paletteColors Palette used for logos ie the menu palette
 * @throw InitializationFailedException if any problem during reading initialization
 */
void LogoManager::loadLogos(const fs_eng::Palette &palette) {
    size_t fileSize;

    LOG(Log::k_FLG_GFX, "LogoManager", "loadLogos", ("Loading logos..."))

    data_all_logos_ = fs_utl::File::loadOriginalFile("mlogos.dat", fileSize);
    if (data_all_logos_ == nullptr) {
        throw InitializationFailedException("Could not read mlogos.dat");
    }

    numberLogo_ = int(fileSize) / (kLogoBigWidth * kLogoBigWidth);

    // We store all logos in a temporaty buffer before copying everything in the texture
    // We only use the big logos and use SDL to strech texture
    const int textureWidth = kNumOfLogosPerRow * kLogoBigWidth;
    // there are 40 logos : so we use 64px height to get a power of 2
    const int textureHeight = 64;
    
    uint8_t *logosBuffer = new uint8_t[textureWidth * textureWidth];
    logoPositions_ = new Point2D*[numberLogo_];

    copyLogosPixelsToBuffer(data_all_logos_, logosBuffer);

    logosTexture_ = g_System.createTexture();
    bool res = logosTexture_->create8bitsSurfaceFromData(logosBuffer, 
                                            textureWidth, 
                                            textureHeight, 254);
    
    delete [] logosBuffer;

    if (!res) {
        throw InitializationFailedException("Could not create logo texture");
    }

    if (!logosTexture_->setPalette(palette)) {
        throw InitializationFailedException("Could not set palette");
    }
    
    if (!logosTexture_->loadTextureFromSurface()) {
        throw InitializationFailedException("Could not load texture");
    }
    
    loadColours(palette);

    LOG(Log::k_FLG_GFX, "LogoManager", "loadLogos", ("%i logos loaded", numberLogo_))
}

/*!
 * Copies the content of a tile to a common temporary buffer that will be used
 * to initialize the tile texture. The buffer stores tiles as a 16x16 tileset.
 * Each tile is copied to the buffer at a position given by its id.
 * @param bigLogosData pixels of all big logos
 * @param logosBuffer Buffer to copy the tile to.
 */
void LogoManager::copyLogosPixelsToBuffer(const uint8_t *bigLogosData, uint8_t *logosBuffer) {
    for (int i=0; i < numberLogo_; i++) {
        // Coords in the destination surface
        int row = i / kNumOfLogosPerRow;
        int col = i - (row * kNumOfLogosPerRow);

        // start of the logo pixels in the destination surface (upper left corner of logo)
        int logoOffsetDest = (col * kLogoBigWidth) + (row * kNumOfLogosPerRow * kLogoBigWidth * kLogoBigWidth);
        int logoOffsetSrc = i * kLogoBigWidth * kLogoBigWidth;

        // Copy pixels line by line
        for (int j=0; j < kLogoBigWidth; j++) {
            int lineOffsetDest = j * kLogoBigWidth * kNumOfLogosPerRow;
            int lineOffsetSrc = j * kLogoBigWidth;
            memcpy(logosBuffer + logoOffsetDest + lineOffsetDest, bigLogosData + logoOffsetSrc + lineOffsetSrc, kLogoBigWidth);
        }

        logoPositions_[i] = new Point2D();
        logoPositions_[i]->x = col * kLogoBigWidth;
        logoPositions_[i]->y = row * kLogoBigWidth;
    }
}

/*!
 *
 * \param colourIdx int must be positive and less than kMaxColour
 * \return uint8_t The color value
 *
 */
uint8_t LogoManager::getColorAtIndex(int colourIdx) {
    if (colourIdx < 0 || colourIdx >= kMaxColour) {
        colourIdx = 0;
    }
    return g_Colours[colourIdx];
}

void LogoManager::loadColours(const fs_eng::Palette &palette) {
    colours_.clear();
    for (int i = 0; i < kMaxColour; i++) {
        fs_eng::FSColor color = palette[g_Colours[i]];
        color.a = 255;
        colours_.push_back(color);
    }
    if (g_Ctx.isHardwareRendering()) {
        for (const fs_eng::FSColor &color : g_ExtraColours) {
            colours_.push_back(color);
        }
    }
}

int LogoManager::closestOriginalColour(int colourIdx) {
    const fs_eng::FSColor &target = g_ExtraColours[colourIdx - kMaxColour];
    int closest = 0;
    int closestDistance = INT_MAX;
    for (int i = 0; i < kMaxColour; i++) {
        int dr = colours_[i].r - target.r;
        int dg = colours_[i].g - target.g;
        int db = colours_[i].b - target.b;
        int distance = dr * dr + dg * dg + db * db;
        if (distance < closestDistance) {
            closestDistance = distance;
            closest = i;
        }
    }
    return closest;
}

/*!
 * Returns the RGB value of a company color.
 * \param colourIdx Index of the color. A color only available in hardware mode
 * is replaced by the closest original color in software mode.
 */
fs_eng::FSColor LogoManager::getColour(int colourIdx) {
    const int numExtra = static_cast<int>(sizeof(g_ExtraColours) / sizeof(g_ExtraColours[0]));
    if (colourIdx < 0 || colourIdx >= kMaxColour + numExtra) {
        colourIdx = 0;
    } else if (colourIdx >= numColours()) {
        colourIdx = closestOriginalColour(colourIdx);
    }
    return colours_[colourIdx];
}

int LogoManager::numLogos()
{
    return numberLogo_;
}

/*!
 * Draw a logo at the given position and with the given color
 * @param pos Coordinate on screen where to draw the logo
 * @param logoId Id of the logo to draw
 * @param colourId Id of the company color (see getColour())
 * @param big True to draw a 64x64 logo. Else it will be 32x32
 */
void LogoManager::draw(Point2D pos, int logoId, int colorId, bool big) {
    fs_eng::FSColor color = getColour(colorId);
    // In both case, first we draw a background of the given color
    // and then on top the logo
    if (big) {
        g_System.drawFillRect(pos, kLogoBigWidth*2, kLogoBigWidth*2, color);
        logosTexture_->renderStretch(*logoPositions_[logoId], pos, kLogoBigWidth, kLogoBigWidth, 2);
    } else {
        g_System.drawFillRect(pos, kLogoBigWidth, kLogoBigWidth, color);
        logosTexture_->render(*logoPositions_[logoId], pos, kLogoBigWidth, kLogoBigWidth);
    }
}

}

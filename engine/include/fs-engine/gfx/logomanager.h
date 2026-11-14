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

#ifndef ENGINE_LOGOMANAGER_H
#define ENGINE_LOGOMANAGER_H

#include <memory>
#include <vector>

#include "fs-utils/common.h"
#include "fs-engine/enginecommon.h"
#include "fs-utils/misc/singleton.h"
#include "fs-engine/gfx/fstexture.h"

namespace fs_eng {

/*!
 * This class manages the logo sprites and proposes a method to display a logo.
 * Logos are uploaded from original game files.
 */
class LogoManager : public Singleton<LogoManager>{
public:
    /*! Number of colors for logos in the original game (palette colors).*/
    static const int kMaxColour;

    explicit LogoManager();
    ~LogoManager();

    //! Initialize logoManager by loading the logos
    void loadLogos(const fs_eng::Palette &palette);

    //! Return the palette index of the original color at the given index
    uint8_t getColorAtIndex(int colourIdx);
    //! Returns the number of company colors that can be chosen
    int numColours() { return static_cast<int>(colours_.size()); }
    //! Returns the RGB value of the company color at the given index
    fs_eng::FSColor getColour(int colourIdx);
    //! Returns the number of available logos
    int numLogos();
    //! Draws a logo at given position with the given color in big format or not
    void draw(Point2D pos, int logoId, int colourIdx, bool big);

private:
    //! Copies logos data found in original files into a buffer before copying it to a surface
    void copyLogosPixelsToBuffer(const uint8_t *bigLogosData, uint8_t *logosBuffer);
    //! Fills the list of company colors
    void loadColours(const fs_eng::Palette &palette);
    //! Returns the index of the original color closest to the given extra color
    int closestOriginalColour(int colourIdx);

private:
    /*!
     * Company colors : the original palette colors first then, in hardware
     * mode only, extra colors that are not limited by the palette.
     */
    std::vector<fs_eng::FSColor> colours_;

private:
    /*! Width for a normal logo*/
    static const int kLogoBigWidth;
    /*! Width for a small logo*/
    static const int kLogoSmallWidth;
    /*! How many logos on a line in a texture.*/
    static const int kNumOfLogosPerRow;
    
    //! Number of logos in the game
    int numberLogo_;
    //! This contains the pixels data for all logos and are loaded from a file
    uint8 *data_all_logos_;

    //! A texture that stores the logos big and small
    std::unique_ptr<FSTexture> logosTexture_;
    //! An array of positions for each logo in the texture
    Point2D **logoPositions_;
};

}

#define g_LogoMgr    fs_eng::LogoManager::singleton()

#endif //ENGINE_LOGOMANAGER_H

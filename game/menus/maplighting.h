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

#ifndef MENUS_MAPLIGHTING_H_
#define MENUS_MAPLIGHTING_H_

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fs-utils/common.h"
#include "fs-engine/enginecommon.h"
#include "fs-engine/gfx/tile.h"
#include "fs-engine/gfx/fstexture.h"

namespace fs_knl {
class Map;
}

/*!
 * Adds lighting to the map in hardware mode, computed from the height and the
 * type of the tiles : the sun comes from the top left of the screen, so buildings
 * cast shadows towards the bottom right and slopes facing away from the sun are darker.
 *
 * Lighting is applied by multiplying the colors of each tile (and of the objects
 * standing on it) when they are drawn, so the original drawing order is kept.
 * The strength of the lighting follows the time of day chosen in hardware mode :
 * at dusk and at night, the effects are weaker.
 *
 * Lights (lamps and other bright spots in the tiles, lit windows, neon signs) emit
 * light : the map is multiplied, pixel by pixel, by a light map made of the ambient
 * light of the time of day plus a soft circle of light around each light. They show
 * when the scene is darker than the mission's own look (dusk, night...).
 */
class MapLighting {
public:
    //! How the pixels of a tile shine in the dark
    enum class Glow {
        //! Nothing shines (the default)
        kNone,
        //! The brightest pixels shine
        kBright,
        //! The whole tile keeps its brightness (a pool of light painted on the ground)
        kFull
    };
    //! If a tile lights up what's around it
    enum class LightMode {
        //! When the tile has a few very bright pixels (a lamp)
        kAuto,
        kNone,
        kOn
    };
    /*!
     * Lighting settings of a tile : edited with the light editor (Escape menu in a mission)
     * and saved in data/ref/tile_lights.ini.
     */
    struct TileSetting {
        Glow glow = Glow::kNone;
        LightMode light = LightMode::kAuto;
        //! Radius of the light, 0 for the automatic radius
        int radius = 0;
        //! When true, colour is the colour of the light, else it comes from the tile
        bool hasColour = false;
        fs_eng::FSColor colour = {255, 255, 255, 255};
    };

    //! Returns the settings of a tile (the default ones if it has none)
    TileSetting tileSetting(int tileId);
    //! Changes the settings of a tile : the lighting is updated straight away
    void setTileSetting(int tileId, const TileSetting &setting);
    //! Saves the settings of all the tiles in the config file. Returns false on error
    bool saveTileSettings();
    //! The file where the settings are saved
    const std::string &tileSettingsPath() { return tileSettingsPath_; }
    /*!
     * Returns the tile seen at the given point of the map texture in the last frame (the
     * one in front) or null. pTilePos is set with where the tile was drawn.
     */
    const fs_eng::Tile *tileAt(const Point2D &point, Point2D *pTilePos);

    MapLighting() : pMap_(nullptr), enabled_(false), strength_(0) {}

    //! Computes the lighting for the given map. When not enabled, nothing changes
    void init(fs_knl::Map *pMap, bool enabled);

    bool isEnabled() { return enabled_; }
    //! The tiles drawn in the current frame with their position, in drawing order
    const std::vector<std::pair<const fs_eng::Tile *, Point2D>> &drawnTiles() { return drawnTiles_; }

    //! Sets the strength of the lighting from the brightness of the scene (1 is the mission's own look)
    void setSceneBrightness(double brightness);

    //! Returns the color to multiply the tile at the given position with
    fs_eng::FSColor tileColour(const fs_eng::Tile *pTile, int tileX, int tileY, int tileZ);
    //! Returns the color to multiply the objects standing at the given position with
    fs_eng::FSColor objectColour(int tileX, int tileY, int tileZ);

    //! Removes the lights found while drawing the previous frame
    void clearLights() { lights_.clear(); glows_.clear(); drawnTiles_.clear(); windowGlows_.clear(); }
    /*!
     * A lit window covers the given rectangle of the map texture : the bright pixels of
     * the tiles in it shine in the dark (lit windows are painted in the tiles of the walls).
     */
    void addWindowGlow(const Point2D &topLeft, int width, int height);
    /*!
     * A tile has just been drawn at the given position : adds its lights, and keeps it
     * to draw its bright pixels in the light map.
     */
    void addTileLights(const fs_eng::Tile *pTile, const Point2D &screenPos);
    //! Adds a light at the given position of the map texture. With radiusY, the light is an ellipse
    void addLight(const Point2D &pos, int radius, fs_eng::FSColor colour, int radiusY = 0);
    /*!
     * Adds a cone of light (a headlight) starting at pos, pointing to the given angle on
     * screen (in degrees, clockwise, 0 is to the right) and as long as length.
     */
    void addConeLight(const Point2D &pos, double angle, int length, fs_eng::FSColor colour);
    /*!
     * Adds a glow at the given position : unlike a light, it's added to the colours of the
     * map so it's visible even when the scene is bright (a muzzle flash).
     */
    void addGlow(const Point2D &pos, int radius, fs_eng::FSColor colour);
    /*!
     * Multiplies the map, drawn in pMapTexture, by the ambient light (the colour grade of
     * the time of day) plus the lights added since clearLights().
     * drawGlowingSprites is called while the light map is drawn, with sprites drawn
     * additively : their pixels shine in the dark.
     */
    void applyLights(fs_eng::FSTexture *pMapTexture, fs_eng::FSColor ambient,
                     const std::function<void()> &drawGlowingSprites = nullptr);

private:
    //! Finds the bright spots in each tile : they become lights
    void findTileLights();
    //! Creates the textures with the opaque and the bright pixels of the tiles
    void createTileMasks();
    //! Loads the settings of the tiles from the config file
    void loadTileSettings();
    //! Finds the centre of the opaque pixels of a tile and the colour of its brightest pixels
    void tileCentreAndColour(const fs_eng::Tile *pTile, Point2D *pCentre, fs_eng::FSColor *pColour);
    //! Maximum number of bright pixels for a tile of the given type to emit light
    int maxLightPixels(fs_eng::Tile::EType type);
    //! True if the tile at the given position stops the light
    bool isSolid(int x, int y, int z);
    //! True if the top of the tile at the given position can be in shadow
    bool receivesShadow(int x, int y, int z);
    //! True if the top of the tile at the given position is in the shadow of another tile
    bool computeShadow(int x, int y, int z);
    //! Returns the color for the given amount of light (1.0 = not changed)
    fs_eng::FSColor colourForLight(double light);

private:
    fs_knl::Map *pMap_;
    bool enabled_;
    //! For each tile, true if its top is in shadow
    std::vector<bool> shadows_;
    //! From 0 (no effect) to 1 : depends on the brightness of the scene
    double strength_;

    //! A light in a tile, found from its bright pixels
    struct TileLight {
        bool hasLight;
        //! Position of the light in the tile
        Point2D offset;
        int radius;
        //! Vertical radius : a pool of light is an ellipse
        int radiusY;
        fs_eng::FSColor colour;
    };
    //! The light of each tile, by tile id
    std::vector<TileLight> tileLights_;

    //! A light to draw in the current frame
    struct PointLight {
        Point2D pos;
        //! Horizontal and vertical radius
        int radius;
        int radiusY;
        fs_eng::FSColor colour;
        //! For a cone of light : its direction in degrees (radius is its length)
        bool isCone;
        double angle;
    };
    std::vector<PointLight> lights_;
    //! Glows to add in the current frame
    std::vector<PointLight> glows_;
    //! A soft white circle drawn for each light
    std::unique_ptr<fs_eng::FSTexture> pLightTexture_;
    //! A soft white cone pointing to the right, for headlights
    std::unique_ptr<fs_eng::FSTexture> pConeTexture_;
    //! The ambient light plus the lights, which multiplies the map
    std::unique_ptr<fs_eng::FSTexture> pLightMap_;

    /*!
     * The art shows light with bright colours (lamps, pools of light painted on the
     * ground) : bright pixels keep their brightness in the dark. For each tile drawn,
     * its opaque pixels are drawn in the light map with the ambient light (so it hides
     * the tiles behind), then its bright pixels are added.
     */
    std::unique_ptr<fs_eng::FSTexture> pTileSilhouettes_;
    //! How much each pixel of the tiles shines, from the glow setting of the tile
    std::unique_ptr<fs_eng::FSTexture> pTileBrightness_;
    //! How much each pixel of the tiles shines from its brightness only : used in lit windows
    std::unique_ptr<fs_eng::FSTexture> pTileBrightPixels_;
    //! A rectangle of the map texture covered by a lit window
    struct WindowGlow {
        Point2D topLeft;
        int width;
        int height;
    };
    std::vector<WindowGlow> windowGlows_;
    //! The tiles drawn in the current frame, in the order they were drawn
    std::vector<std::pair<const fs_eng::Tile *, Point2D>> drawnTiles_;
    //! Settings of the tiles that don't use the default ones, by tile id
    std::map<int, TileSetting> tileSettings_;
    //! The file with the settings of the tiles
    std::string tileSettingsPath_;
};

#endif  // MENUS_MAPLIGHTING_H_

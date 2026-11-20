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

#include "menus/maplighting.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "fs-utils/log/log.h"
#include "fs-utils/io/file.h"
#include "fs-engine/gfx/animationmanager.h"
#include "fs-engine/gfx/tilemanager.h"
#include "fs-engine/system/system.h"
#include "fs-kernel/model/map.h"

//! A pixel of a tile is a light when it's this bright (0 to 1)
const double kLightPixelLuminance = 0.78;
//! Minimum number of bright pixels for a tile to emit light
const int kMinLightPixels = 2;
//! A tile with more bright pixels than this is just a bright surface, not a light
const int kMaxLightPixels = fs_eng::Tile::kTileWidth * fs_eng::Tile::kTileHeight / 8;
//! On the ground, bigger bright areas are paint
const int kMaxGroundLightPixels = 48;
/*!
 * A light turned on in the settings of a tile (like a pool of light painted on the
 * ground) is an ellipse like the tile seen from above : twice as wide as high. This
 * is its default width, bigger than the tile so it also lights up a bit around it.
 */
const int kTileLightRadius = 80;
//! The file with the lighting settings of the tiles, in the FreeSynd data folder
const char *kTileSettingsFile = "ref/tile_lights.ini";

static double luminanceOf(const fs_eng::FSColor &c) {
    return (0.299 * c.r + 0.587 * c.g + 0.114 * c.b) / 255.0;
}

//! White pixels on the roads (little lights) are this bright at least, with little colour
const double kRoadLightLuminance = 0.8;
const int kRoadLightMaxTint = 60;
//! The lights of the roads keep this part of their brightness in the dark
const double kRoadLightShine = 0.6;
//! Pixels brighter than this (0 to 1) start to shine in the dark
const double kShineStart = 0.62;
//! Pixels this bright (0 to 1) keep all their brightness in the dark
const double kShineFull = 0.80;
//! Size of the texture with the circle of light
const int kLightTextureSize = 64;
//! Size of the texture with the cone of light : the cone starts in the middle of the left side
const int kConeTextureWidth = 128;
const int kConeTextureHeight = 64;
//! Half of the angle of the cone of light, in radians
const double kConeHalfAngle = 0.45;

//! How far (in tiles) a building can cast its shadow
const int kMaxShadowLength = 8;
//! Light received by a tile in shadow, at full strength
const double kShadowLight = 0.55;
//! Light received by a slope facing away from the sun, at full strength
const double kSlopeAwayLight = 0.8;
//! Strength of the lighting for the mission's own look
const double kDefaultStrength = 0.8;
const fs_eng::FSColor kNoChange = {255, 255, 255, 255};

void MapLighting::init(fs_knl::Map *pMap, bool enabled) {
    pMap_ = pMap;
    enabled_ = enabled;
    shadows_.clear();
    if (!enabled_) {
        return;
    }

    setSceneBrightness(1.0);

    shadows_.resize(static_cast<size_t>(pMap_->maxX()) * pMap_->maxY() * pMap_->maxZ());
    for (int z = 0; z < pMap_->maxZ(); z++) {
        for (int y = 0; y < pMap_->maxY(); y++) {
            for (int x = 0; x < pMap_->maxX(); x++) {
                shadows_[(static_cast<size_t>(z) * pMap_->maxY() + y) * pMap_->maxX() + x] = computeShadow(x, y, z);
            }
        }
    }

    loadTileSettings();
    findTileLights();
    createTileMasks();

    if (!pLightTexture_) {
        // A white circle that fades out from its centre
        std::vector<uint8_t> pixels(kLightTextureSize * kLightTextureSize);
        double half = kLightTextureSize / 2.0;
        for (int y = 0; y < kLightTextureSize; y++) {
            for (int x = 0; x < kLightTextureSize; x++) {
                double d = std::hypot(x + 0.5 - half, y + 0.5 - half) / half;
                double v = d >= 1.0 ? 0.0 : (1.0 - d) * (1.0 - d);
                pixels[y * kLightTextureSize + x] = static_cast<uint8_t>(v * 255);
            }
        }
        fs_eng::Palette greys;
        for (size_t i = 0; i < greys.size(); i++) {
            uint8_t v = static_cast<uint8_t>(i);
            greys[i] = {v, v, v, 255};
        }
        pLightTexture_ = g_System.createTexture();
        pLightTexture_->createStreamingTexture(kLightTextureSize, kLightTextureSize);
        pLightTexture_->updateStreamingTexture(pixels.data(), greys);
        pLightTexture_->setBlendMode(fs_eng::FSTexture::BlendMode::kAdd);

        // A cone that fades out with the distance and towards its sides
        std::vector<uint8_t> conePixels(kConeTextureWidth * kConeTextureHeight);
        for (int y = 0; y < kConeTextureHeight; y++) {
            for (int x = 0; x < kConeTextureWidth; x++) {
                double dist = (x + 0.5) / kConeTextureWidth;
                double angle = std::abs(std::atan2(y + 0.5 - kConeTextureHeight / 2.0, x + 0.5));
                double side = angle >= kConeHalfAngle ? 0.0 : 1.0 - angle / kConeHalfAngle;
                double v = (1.0 - dist) * std::sqrt(side);
                conePixels[y * kConeTextureWidth + x] = static_cast<uint8_t>(std::clamp(v, 0.0, 1.0) * 255);
            }
        }
        pConeTexture_ = g_System.createTexture();
        pConeTexture_->createStreamingTexture(kConeTextureWidth, kConeTextureHeight);
        pConeTexture_->updateStreamingTexture(conePixels.data(), greys);
        pConeTexture_->setBlendMode(fs_eng::FSTexture::BlendMode::kAdd);

        pLightMap_ = g_System.createTexture();
        pLightMap_->createRenderTargetTexture(kMaxMapViewWidth, kMaxMapViewHeight);
    }
}

/*!
 * Returns the maximum number of bright pixels for a tile of the given type to emit
 * light. The bright colour of the palette is also used for paint : road markings
 * and the edges of stairs never emit light, and on the ground only small spots do
 * (not painted areas).
 */
int MapLighting::maxLightPixels(fs_eng::Tile::EType type) {
    switch (type) {
    case fs_eng::Tile::kSlopeSN:
    case fs_eng::Tile::kSlopeNS:
    case fs_eng::Tile::kSlopeEW:
    case fs_eng::Tile::kSlopeWE:
    case fs_eng::Tile::kRoadSideEW:
    case fs_eng::Tile::kRoadSideWE:
    case fs_eng::Tile::kRoadSideSN:
    case fs_eng::Tile::kRoadSideNS:
    case fs_eng::Tile::kRoadCurve:
    case fs_eng::Tile::kRoadPedCross:
    case fs_eng::Tile::kRoadMark:
        return 0;
    case fs_eng::Tile::kGround:
        return kMaxGroundLightPixels;
    default:
        return kMaxLightPixels;
    }
}

/*!
 * A tile emits light if it has a few very bright pixels (a lamp, a lit sign) :
 * the light is at the centre of those pixels, with their average colour.
 */
void MapLighting::findTileLights() {
    fs_eng::TileManager *pTileManager = pMap_->getTileManager();
    const fs_eng::Palette &palette = pTileManager->getPalette();
    int nbLights = 0;

    tileLights_.assign(fs_eng::TileManager::kNumOfTiles, TileLight {false, {0, 0}, 0, 0, kNoChange});
    for (int id = 0; id < fs_eng::TileManager::kNumOfTiles; id++) {
        fs_eng::Tile *pTile = pTileManager->getTile(static_cast<uint8_t>(id));
        if (pTile == nullptr) {
            continue;
        }
        int count = 0;
        long sumX = 0, sumY = 0, sumR = 0, sumG = 0, sumB = 0;
        for (int y = 0; y < fs_eng::Tile::kTileHeight; y++) {
            for (int x = 0; x < fs_eng::Tile::kTileWidth; x++) {
                uint8_t index = pTileManager->pixelIndex(pTile, x, y);
                if (index == fs_eng::TileManager::kTileColorKeyIndex) {
                    continue;
                }
                const fs_eng::FSColor &c = palette[index];
                if ((0.299 * c.r + 0.587 * c.g + 0.114 * c.b) / 255.0 >= kLightPixelLuminance) {
                    count++;
                    sumX += x; sumY += y;
                    sumR += c.r; sumG += c.g; sumB += c.b;
                }
            }
        }
        TileSetting setting = tileSetting(id);
        TileLight &light = tileLights_[id];
        if (setting.light == LightMode::kOn) {
            // A light turned on in the settings lights up the whole tile from its centre
            tileCentreAndColour(pTile, &light.offset, &light.colour);
            light.hasLight = true;
            light.radius = setting.radius > 0 ? setting.radius : kTileLightRadius;
            light.radiusY = light.radius / 2;
        } else if (setting.light == LightMode::kAuto &&
                    count >= kMinLightPixels && count <= maxLightPixels(pTile->type())) {
            // The colour of the light is the average colour of the pixels, at full brightness
            double r = sumR / (double) count, g = sumG / (double) count, b = sumB / (double) count;
            double maxChannel = std::max({r, g, b, 1.0});
            light.hasLight = true;
            light.offset = {static_cast<int>(sumX / count), static_cast<int>(sumY / count)};
            light.radius = setting.radius > 0 ? setting.radius
                                              : std::clamp(static_cast<int>(24 + 6 * std::sqrt(count)), 28, 90);
            light.radiusY = light.radius;
            light.colour = {static_cast<uint8_t>(r * 255 / maxChannel), static_cast<uint8_t>(g * 255 / maxChannel),
                            static_cast<uint8_t>(b * 255 / maxChannel), 255};
        }
        if (light.hasLight) {
            if (setting.hasColour) {
                light.colour = setting.colour;
            }
            nbLights++;
        }
    }
    LOG(Log::k_FLG_GFX, "MapLighting", "findTileLights", ("%d tiles emit light", nbLights))
}

/*!
 * The silhouettes have the opaque pixels of the tiles in white. The brightness of a
 * pixel goes from 0 (it doesn't shine) to 1 (it keeps all its brightness in the dark).
 */
void MapLighting::createTileMasks() {
    fs_eng::TileManager *pTileManager = pMap_->getTileManager();
    const fs_eng::Palette &palette = pTileManager->getPalette();
    const int kTransparent = fs_eng::TileManager::kTileColorKeyIndex;
    const int width = fs_eng::TileManager::kNumOfTilesPerRow * fs_eng::Tile::kTileWidth;
    const int height = fs_eng::TileManager::kNumOfTilesPerCol * fs_eng::Tile::kTileHeight;
    std::vector<uint8_t> silhouettes(static_cast<size_t>(width) * height, kTransparent);
    std::vector<uint8_t> brightness(static_cast<size_t>(width) * height, kTransparent);
    std::vector<uint8_t> brightPixels(static_cast<size_t>(width) * height, kTransparent);

    for (int id = 0; id < fs_eng::TileManager::kNumOfTiles; id++) {
        fs_eng::Tile *pTile = pTileManager->getTile(static_cast<uint8_t>(id));
        if (pTile == nullptr) {
            continue;
        }
        Point2D loc = pTile->textureLocation();
        Glow glow = tileSetting(id).glow;
        // The little white lights painted on the roads glow a bit, unless the tile has settings
        bool roadLights = pTile->isRoad() && tileSettings_.find(id) == tileSettings_.end();
        for (int y = 0; y < fs_eng::Tile::kTileHeight; y++) {
            for (int x = 0; x < fs_eng::Tile::kTileWidth; x++) {
                uint8_t index = pTileManager->pixelIndex(pTile, x, y);
                if (index == kTransparent) {
                    continue;
                }
                size_t i = static_cast<size_t>(loc.y + y) * width + loc.x + x;
                silhouettes[i] = 0;
                double luminance = luminanceOf(palette[index]);
                double brightShine = std::clamp((luminance - kShineStart) / (kShineFull - kShineStart), 0.0, 1.0);
                double shine = brightShine;
                if (glow == Glow::kFull) {
                    // The whole tile keeps its brightness
                    shine = 1.0;
                } else if (roadLights) {
                    const fs_eng::FSColor &c = palette[index];
                    bool white = luminance >= kRoadLightLuminance &&
                                 (std::max)({c.r, c.g, c.b}) - (std::min)({c.r, c.g, c.b}) < kRoadLightMaxTint;
                    shine = white ? kRoadLightShine : 0.0;
                } else if (glow == Glow::kNone) {
                    shine = 0.0;
                }
                // the value 255 is the transparent one
                brightness[i] = static_cast<uint8_t>(shine * 254);
                brightPixels[i] = static_cast<uint8_t>(brightShine * 254);
            }
        }
    }

    fs_eng::Palette whites;
    fs_eng::Palette greys;
    for (size_t i = 0; i < greys.size(); i++) {
        uint8_t v = static_cast<uint8_t>(i);
        whites[i] = {255, 255, 255, 255};
        greys[i] = {v, v, v, 255};
    }
    pTileSilhouettes_ = g_System.createTexture();
    pTileSilhouettes_->create8bitsSurfaceFromData(silhouettes.data(), width, height, kTransparent);
    pTileSilhouettes_->setPalette(whites);
    pTileSilhouettes_->loadTextureFromSurface();
    pTileSilhouettes_->setBlendMode(fs_eng::FSTexture::BlendMode::kBlend);

    pTileBrightness_ = g_System.createTexture();
    pTileBrightness_->create8bitsSurfaceFromData(brightness.data(), width, height, kTransparent);
    pTileBrightness_->setPalette(greys);
    pTileBrightness_->loadTextureFromSurface();
    pTileBrightness_->setBlendMode(fs_eng::FSTexture::BlendMode::kAdd);

    pTileBrightPixels_ = g_System.createTexture();
    pTileBrightPixels_->create8bitsSurfaceFromData(brightPixels.data(), width, height, kTransparent);
    pTileBrightPixels_->setPalette(greys);
    pTileBrightPixels_->loadTextureFromSurface();
    pTileBrightPixels_->setBlendMode(fs_eng::FSTexture::BlendMode::kAdd);
}

void MapLighting::addWindowGlow(const Point2D &topLeft, int width, int height) {
    if (width > 0 && height > 0) {
        windowGlows_.push_back({topLeft, width, height});
    }
}

/*!
 * The centre of a tile is the centre of its opaque pixels. Its colour is the one of
 * its brightest pixels, at full brightness (like the colour of a painted light).
 */
void MapLighting::tileCentreAndColour(const fs_eng::Tile *pTile, Point2D *pCentre, fs_eng::FSColor *pColour) {
    fs_eng::TileManager *pTileManager = pMap_->getTileManager();
    const fs_eng::Palette &palette = pTileManager->getPalette();
    double maxLuminance = 0;
    long sumX = 0, sumY = 0;
    int count = 0;
    for (int y = 0; y < fs_eng::Tile::kTileHeight; y++) {
        for (int x = 0; x < fs_eng::Tile::kTileWidth; x++) {
            uint8_t index = pTileManager->pixelIndex(pTile, x, y);
            if (index != fs_eng::TileManager::kTileColorKeyIndex) {
                maxLuminance = std::max(maxLuminance, luminanceOf(palette[index]));
                sumX += x; sumY += y;
                count++;
            }
        }
    }
    if (count == 0) {
        *pCentre = {fs_eng::Tile::kTileWidth / 2, fs_eng::Tile::kTileHeight / 2};
        *pColour = kNoChange;
        return;
    }
    *pCentre = {static_cast<int>(sumX / count), static_cast<int>(sumY / count)};
    double r = 0, g = 0, b = 0;
    for (int y = 0; y < fs_eng::Tile::kTileHeight; y++) {
        for (int x = 0; x < fs_eng::Tile::kTileWidth; x++) {
            uint8_t index = pTileManager->pixelIndex(pTile, x, y);
            if (index != fs_eng::TileManager::kTileColorKeyIndex && luminanceOf(palette[index]) >= maxLuminance * 0.85) {
                r += palette[index].r; g += palette[index].g; b += palette[index].b;
            }
        }
    }
    double maxChannel = std::max({r, g, b, 1.0});
    *pColour = {static_cast<uint8_t>(r * 255 / maxChannel), static_cast<uint8_t>(g * 255 / maxChannel),
                static_cast<uint8_t>(b * 255 / maxChannel), 255};
}

MapLighting::TileSetting MapLighting::tileSetting(int tileId) {
    auto it = tileSettings_.find(tileId);
    return it != tileSettings_.end() ? it->second : TileSetting();
}

void MapLighting::setTileSetting(int tileId, const TileSetting &setting) {
    tileSettings_[tileId] = setting;
    if (enabled_) {
        findTileLights();
        createTileMasks();
    }
}

static const char *glowName(MapLighting::Glow glow) {
    return glow == MapLighting::Glow::kBright ? "bright" : glow == MapLighting::Glow::kFull ? "full" : "none";
}

static const char *lightModeName(MapLighting::LightMode mode) {
    return mode == MapLighting::LightMode::kNone ? "none" : mode == MapLighting::LightMode::kOn ? "on" : "auto";
}

/*!
 * Each line of the file has the settings of one tile, like :
 * tile=207 glow=full light=on radius=80 colour=auto
 * Lines starting with # are comments.
 */
void MapLighting::loadTileSettings() {
    tileSettings_.clear();
    tileSettingsPath_ = fs_utl::File::getFreesyndDataFullPath(kTileSettingsFile);
    std::ifstream in(tileSettingsPath_);
    if (!in) {
        LOG(Log::k_FLG_GFX, "MapLighting", "loadTileSettings", ("No tile lighting settings in %s", tileSettingsPath_.c_str()))
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream words(line);
        std::string word;
        int tileId = -1;
        TileSetting setting;
        while (words >> word) {
            size_t equal = word.find('=');
            if (equal == std::string::npos) {
                continue;
            }
            std::string key = word.substr(0, equal);
            std::string value = word.substr(equal + 1);
            if (key == "tile") {
                tileId = std::atoi(value.c_str());
            } else if (key == "glow") {
                setting.glow = value == "bright" ? Glow::kBright : value == "full" ? Glow::kFull : Glow::kNone;
            } else if (key == "light") {
                setting.light = value == "none" ? LightMode::kNone : value == "on" ? LightMode::kOn : LightMode::kAuto;
            } else if (key == "radius") {
                setting.radius = std::max(0, std::atoi(value.c_str()));
            } else if (key == "colour") {
                int r, g, b;
                if (std::sscanf(value.c_str(), "%d,%d,%d", &r, &g, &b) == 3) {
                    setting.hasColour = true;
                    setting.colour = {static_cast<uint8_t>(std::clamp(r, 0, 255)), static_cast<uint8_t>(std::clamp(g, 0, 255)),
                                      static_cast<uint8_t>(std::clamp(b, 0, 255)), 255};
                }
            }
        }
        if (tileId >= 0 && tileId < fs_eng::TileManager::kNumOfTiles) {
            tileSettings_[tileId] = setting;
        }
    }
    LOG(Log::k_FLG_GFX, "MapLighting", "loadTileSettings",
        ("%d tile lighting settings read from %s", static_cast<int>(tileSettings_.size()), tileSettingsPath_.c_str()))
}

bool MapLighting::saveTileSettings() {
    std::ofstream out(tileSettingsPath_);
    if (!out) {
        FSERR(Log::k_FLG_GFX, "MapLighting", "saveTileSettings", ("Could not write %s", tileSettingsPath_.c_str()))
        return false;
    }
    out << "# Lighting of the map tiles in hardware mode, by tile number. Edit it in a mission\n";
    out << "# with the light editor (Escape menu), or by hand :\n";
    out << "#   glow   = how the pixels shine in the dark : none (the default), bright (the brightest ones), full (the whole tile)\n";
    out << "#   light  = if the tile lights up what's around it : auto (when it has a lamp), none, on\n";
    out << "#   radius = size of the light in pixels, 0 for automatic\n";
    out << "#   colour = colour of the light : r,g,b or auto (from the tile)\n";
    for (const auto &[tileId, setting] : tileSettings_) {
        // Tiles with the default settings are not written
        if (setting.glow == Glow::kNone && setting.light == LightMode::kAuto && setting.radius == 0 && !setting.hasColour) {
            continue;
        }
        out << "tile=" << tileId << " glow=" << glowName(setting.glow) << " light=" << lightModeName(setting.light)
            << " radius=" << setting.radius << " colour=";
        if (setting.hasColour) {
            out << static_cast<int>(setting.colour.r) << "," << static_cast<int>(setting.colour.g) << ","
                << static_cast<int>(setting.colour.b);
        } else {
            out << "auto";
        }
        out << "\n";
    }
    LOG(Log::k_FLG_GFX, "MapLighting", "saveTileSettings", ("Tile lighting settings saved in %s", tileSettingsPath_.c_str()))
    return static_cast<bool>(out);
}

const fs_eng::Tile *MapLighting::tileAt(const Point2D &point, Point2D *pTilePos) {
    if (!enabled_) {
        return nullptr;
    }
    fs_eng::TileManager *pTileManager = pMap_->getTileManager();
    // The last tile drawn is the one in front
    for (auto it = drawnTiles_.rbegin(); it != drawnTiles_.rend(); ++it) {
        const fs_eng::Tile *pTile = it->first;
        const Point2D &pos = it->second;
        int x = point.x - pos.x;
        int y = point.y - pos.y;
        if (x >= 0 && y >= 0 && x < fs_eng::Tile::kTileWidth && y < fs_eng::Tile::kTileHeight &&
                pTileManager->pixelIndex(pTile, x, y) != fs_eng::TileManager::kTileColorKeyIndex) {
            *pTilePos = pos;
            return pTile;
        }
    }
    return nullptr;
}

void MapLighting::addTileLights(const fs_eng::Tile *pTile, const Point2D &screenPos) {
    drawnTiles_.push_back({pTile, screenPos});
    const TileLight &light = tileLights_[pTile->id()];
    if (light.hasLight) {
        addLight(screenPos.add(light.offset.x, light.offset.y), light.radius, light.colour, light.radiusY);
    }
}

void MapLighting::addLight(const Point2D &pos, int radius, fs_eng::FSColor colour, int radiusY) {
    lights_.push_back({pos, radius, radiusY > 0 ? radiusY : radius, colour, false, 0.0});
}

void MapLighting::addConeLight(const Point2D &pos, double angle, int length, fs_eng::FSColor colour) {
    lights_.push_back({pos, length, length, colour, true, angle});
}

void MapLighting::addGlow(const Point2D &pos, int radius, fs_eng::FSColor colour) {
    glows_.push_back({pos, radius, radius, colour, false, 0.0});
}

void MapLighting::applyLights(fs_eng::FSTexture *pMapTexture, fs_eng::FSColor ambient, int viewWidth, int viewHeight,
                              const std::function<void()> &drawGlowingSprites) {
    // The light map starts with the ambient light
    pLightMap_->setAsRenderTarget();
    g_System.drawFillRect({0, 0}, viewWidth, viewHeight, ambient);

    // The bright pixels of the tiles shine : the tiles are drawn in the same order as
    // on the map so the ones in front hide the ones behind
    pTileSilhouettes_->setColorModulation(ambient);
    for (const auto &[pTile, pos] : drawnTiles_) {
        pTileSilhouettes_->render(pTile->textureLocation(), pos, fs_eng::Tile::kTileWidth, fs_eng::Tile::kTileHeight);
        pTileBrightness_->render(pTile->textureLocation(), pos, fs_eng::Tile::kTileWidth, fs_eng::Tile::kTileHeight);
        // In lit windows, the bright pixels of the tile shine
        for (const WindowGlow &window : windowGlows_) {
            int left = std::max(pos.x, window.topLeft.x);
            int top = std::max(pos.y, window.topLeft.y);
            int right = std::min(pos.x + fs_eng::Tile::kTileWidth, window.topLeft.x + window.width);
            int bottom = std::min(pos.y + fs_eng::Tile::kTileHeight, window.topLeft.y + window.height);
            if (left < right && top < bottom) {
                pTileBrightPixels_->render(pTile->textureLocation().add(left - pos.x, top - pos.y), {left, top},
                                           right - left, bottom - top);
            }
        }
    }

    // The colours of glowing sprites are added to the light
    if (drawGlowingSprites) {
        g_SpriteMgr.setBlendMode(fs_eng::FSTexture::BlendMode::kAdd);
        g_SpriteMgr.setColorModulation(kNoChange);
        drawGlowingSprites();
        g_SpriteMgr.setBlendMode(fs_eng::FSTexture::BlendMode::kBlend);
    }

    // Each light adds a circle of light
    for (const PointLight &light : lights_) {
        if (light.isCone) {
            // The cone is turned around its tip
            int height = light.radius * kConeTextureHeight / kConeTextureWidth;
            pConeTexture_->setColorModulation(light.colour);
            pConeTexture_->renderRotated({light.pos.x, light.pos.y - height / 2}, light.radius, height,
                light.angle, {0, height / 2});
        } else {
            pLightTexture_->setColorModulation(light.colour);
            pLightTexture_->renderScaled({0, 0}, kLightTextureSize, kLightTextureSize,
                {light.pos.x - light.radius, light.pos.y - light.radiusY}, light.radius * 2, light.radiusY * 2);
        }
    }

    // Then the map is multiplied by the light map
    pMapTexture->setAsRenderTarget();
    pLightMap_->setBlendMode(fs_eng::FSTexture::BlendMode::kMultiply);
    pLightMap_->renderScaled({0, 0}, viewWidth, viewHeight, {0, 0}, viewWidth, viewHeight);
    pLightMap_->setBlendMode(fs_eng::FSTexture::BlendMode::kNone);

    // Glows are added to the colours of the map
    for (const PointLight &glow : glows_) {
        pLightTexture_->setColorModulation(glow.colour);
        pLightTexture_->renderScaled({0, 0}, kLightTextureSize, kLightTextureSize,
            {glow.pos.x - glow.radius, glow.pos.y - glow.radius}, glow.radius * 2, glow.radius * 2);
    }
}

void MapLighting::setSceneBrightness(double brightness) {
    // In a darker scene (dusk, night), the lighting is weaker so it doesn't hide the scene
    strength_ = std::clamp(kDefaultStrength * brightness, 0.3, 1.0);
}

bool MapLighting::isSolid(int x, int y, int z) {
    if (x < 0 || y < 0 || z < 0 || x >= pMap_->maxX() || y >= pMap_->maxY() || z >= pMap_->maxZ()) {
        return false;
    }
    fs_eng::Tile::EType type = pMap_->getTileAt(x, y, z)->type();
    // Fences and handrails are made of mesh : they don't cast shadows
    return type != fs_eng::Tile::kNone && type != fs_eng::Tile::kUnknown && type != fs_eng::Tile::kHandrailLight;
}

/*!
 * Only the surfaces seen from above receive shadows : floors, roads, slopes and
 * roofs with nothing on them. The sides of buildings are not changed.
 */
bool MapLighting::receivesShadow(int x, int y, int z) {
    switch (pMap_->getTileAt(x, y, z)->type()) {
    case fs_eng::Tile::kNone:
    case fs_eng::Tile::kUnknown:
    case fs_eng::Tile::kHandrailLight:
    case fs_eng::Tile::kWall:
        return false;
    default:
        return !isSolid(x, y, z + 1);
    }
}

/*!
 * The sun comes from the top left of the screen (decreasing X) and rises one
 * level per tile : the top of a tile is in shadow if there's a solid tile on
 * that line.
 */
bool MapLighting::computeShadow(int x, int y, int z) {
    if (!receivesShadow(x, y, z)) {
        return false;
    }
    for (int k = 1; k <= kMaxShadowLength; k++) {
        if (isSolid(x - k, y, z + k)) {
            return true;
        }
    }
    return false;
}

fs_eng::FSColor MapLighting::colourForLight(double light) {
    // At night (low strength), the lighting is closer to no change
    double value = 1.0 - (1.0 - light) * strength_;
    uint8_t c = static_cast<uint8_t>(std::clamp(value * 255.0, 0.0, 255.0));
    return {c, c, c, 255};
}

fs_eng::FSColor MapLighting::tileColour(const fs_eng::Tile *pTile, int tileX, int tileY, int tileZ) {
    if (!enabled_) {
        return kNoChange;
    }
    double light = 1.0;
    if (pTile->type() == fs_eng::Tile::kSlopeWE) {
        // Slope going down from the sun
        light = kSlopeAwayLight;
    }
    if (shadows_[(static_cast<size_t>(tileZ) * pMap_->maxY() + tileY) * pMap_->maxX() + tileX]) {
        light = std::min(light, kShadowLight);
    }
    return colourForLight(light);
}

fs_eng::FSColor MapLighting::objectColour(int tileX, int tileY, int tileZ) {
    if (!enabled_ || tileX < 0 || tileY < 0 || tileX >= pMap_->maxX() || tileY >= pMap_->maxY()) {
        return kNoChange;
    }
    // Objects stand on the tile below them
    int floorZ = std::clamp(tileZ - 1, 0, pMap_->maxZ() - 1);
    bool inShadow = shadows_[(static_cast<size_t>(floorZ) * pMap_->maxY() + tileY) * pMap_->maxX() + tileX];
    return colourForLight(inShadow ? kShadowLight : 1.0);
}

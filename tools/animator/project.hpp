/**
 * @file project.hpp
 * @brief The .anim project file and its sidecar .msprite manifests. An .anim is
 *        a small TOML naming the blobman manifest the tiles come from and the
 *        folder holding the metasprites.
 */
#pragma once

#include "chr_table.hpp"
#include "palette.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace proj {

constexpr const char* kProjectExtension     = ".anim";
constexpr const char* kMetaspriteExtension  = ".msprite";
constexpr const char* kDefaultMetaspritesDir = "metasprites";

struct Sprite {
    int         col = 0;     ///< Slot column.
    int         row = 0;     ///< Slot row; a slot is one tile tall, 8 or 16 pixels by mode.
    int         id = 0;      ///< PPU tile id: an index within its pattern table.
    int         patternByte = 0;  ///< What a consumer puts in OAM for it, half bit and all.

    std::string frame;

    std::string entry;       ///< blobman entry the art came from.
    std::string include;     ///< Include (filename stem) within that entry.
    int         tile = 0;    ///< Tile index within that include.
    bool        flipH = false;  ///< Mirrored horizontally.
    bool        flipV = false;  ///< Mirrored vertically.
};

struct Frame {
    std::string         name;
    int                 timer = 0;  ///< NES frames to hold; 0 stops the animation here.

    bool                isPublic = false;

    std::vector<Sprite> sprites;    ///< In placement order, which is draw order.
};

struct Metasprite {
    std::string        name;

    std::string        palette;

    std::string        savedStem;

    int                tilesX = 1;  ///< Extent in tiles, indifferent to 8x8 vs 8x16.
    int                tilesY = 1;
    std::vector<Frame> frames;      ///< In play order.
};

struct Project {
    std::filesystem::path path;

    std::string chrManifest = "";                          ///< Path to the blobman manifest, as written.
    std::string metaspritesDir = kDefaultMetaspritesDir;   ///< Sidecar folder, as written.
    chr::TileMode mode = chr::TileMode::Size8x8;           ///< Tile grouping the project was authored in.

    std::vector<chr::Frame> frames;

    std::vector<pal::Palette> palettes;

    std::vector<Metasprite>  metasprites;  ///< In the order the .anim lists them.
    std::vector<std::string> warnings;     ///< Non-fatal problems found while loading.
};

bool save(Project& project, std::string& error);

std::optional<Project> load(const std::filesystem::path& path, std::string& error);

std::filesystem::path resolve(const Project& project, const std::string& relative);

std::string fileStem(const std::string& name);

const chr::Cell* findCell(const chr::Table& table, const std::string& frame,
                          const std::string& entry, const std::string& include, int tile, int id);

} // namespace proj

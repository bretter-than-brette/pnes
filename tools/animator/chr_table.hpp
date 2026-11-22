/**
 * @file chr_table.hpp
 * @brief Builds a tile table from a blobman manifest -- the read side of
 *        blobman: the same layout parsed the same way, but producing tiles with
 *        the PPU ids the hardware would see rather than packing a blob.
 */
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace chr {

constexpr int kTilePixels = 8;
constexpr int kTileBytes  = 16;

constexpr int kPatternTableBytes = kTileBytes * 256;

constexpr int kFrameTiles = 256;

enum class TileMode {
    Size8x8,   ///< One tile per cell.
    Size8x16,  ///< Two vertically adjacent tiles per cell (PPUCTRL sprite height).
};

struct Frame {
    std::string name;   ///< What a sprite names to draw from it.

    std::uint32_t patternTable = 0;

    std::vector<std::string> windows;
};

struct Entry {
    std::string   name;        ///< The toml key.
    std::uint32_t lma = 0;     ///< Offset inside the blob.
    std::uint32_t size = 0;    ///< Bytes the entry reserves.
    std::uint32_t filled = 0;  ///< Bytes its includes actually fill.
};

constexpr int kNoId = -1;

struct Cell {
    int         id = kNoId;

    int         patternByte = kNoId;
    std::size_t entry = 0;          ///< Index into Table::entries.

    std::string frame;

    int         window = -1;

    std::string include;            ///< Stem of the .chr file it came from.
    int         tileInInclude = 0;  ///< 0-based tile index within that include.

    int         tileInEntry = 0;
    int         heightTiles = 1;    ///< 1 (8x8) or 2 (8x16).

    std::vector<std::uint8_t> pixels;

    [[nodiscard]] bool numbered() const { return id != kNoId; }

    [[nodiscard]] int widthPx() const { return kTilePixels; }
    [[nodiscard]] int heightPx() const { return kTilePixels * heightTiles; }
};

struct Table {
    std::filesystem::path    path;         ///< The manifest that was read.
    std::uint32_t            blobSize = 0; ///< [manifest] size.
    TileMode                 mode = TileMode::Size8x8;
    std::vector<Frame>       frames;       ///< As given to ::loadManifest, in declaration order.
    std::vector<Entry>       entries;      ///< In blob order (sorted by LMA), as blobman emits them.
    std::vector<Cell>        cells;        ///< Entry order, then include pack order.
    std::vector<std::string> warnings;     ///< Non-fatal oddities worth surfacing.
};

std::optional<Table> loadManifest(const std::filesystem::path& path, TileMode mode,
                                  const std::vector<Frame>& frames, std::string& error);

std::optional<Table> loadManifest(const std::filesystem::path& path, TileMode mode,
                                  std::string& error);

} // namespace chr

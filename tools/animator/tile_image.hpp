/**
 * @file tile_image.hpp
 * @brief Bridges chr::Table palette-index pixels into Qt images, so chr_table
 *        and palette stay Qt-free.
 */
#pragma once

#include "chr_table.hpp"
#include "palette.hpp"

#include <QImage>
#include <QRgb>
#include <QVector>

[[nodiscard]] QVector<QRgb> paletteTable(const pal::Palette* palette);

[[nodiscard]] QImage cellImage(const chr::Cell& cell, const QVector<QRgb>& table);

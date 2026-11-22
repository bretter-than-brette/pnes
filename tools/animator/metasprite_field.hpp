/**
 * @file metasprite_field.hpp
 * @brief The field a metasprite is composed on: a grid of tile slots that
 *        stamped sprites are placed into.
 */
#pragma once

#include "chr_table.hpp"

#include "tile_image.hpp"

#include <QWidget>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include <optional>
#include <vector>

constexpr int kDefaultFieldTilesX = 2;
constexpr int kDefaultFieldTilesY = 2;

constexpr int kMinFieldTiles = 1;
constexpr int kMaxFieldTiles = 32;

enum class Mirror : int {
    None       = 0,
    Vertical   = 1,
    Horizontal = 2,
    Both       = 3,
};

constexpr int kMirrorModes = 4;

[[nodiscard]] QString mirrorName(Mirror mirror);

struct PlacedSprite {
    int     col = 0;          ///< Slot column; one slot is one tile wide.
    int     row = 0;          ///< Slot row; one slot is one tile TALL, which is
    int     id = 0;           ///< PPU tile id; the even id of the pair when 8x16.
    int     patternByte = 0;  ///< What OAM takes for it -- see chr::Cell::patternByte.
    int     heightTiles = 1;  ///< Height of the cell's art, in 8-pixel tiles.
    Mirror  mirror = Mirror::None;

    QString frame;            ///< chr::Frame the id is numbered in.
    QString entry;            ///< blobman entry.
    QString include;          ///< Include (filename stem) within it.
    int     tile = 0;         ///< Tile index within that include.

    QImage  image;            ///< The cell's own pixels, unmirrored.

    [[nodiscard]] QString source() const {
        return QString("%1::%2 tile %3").arg(entry, include).arg(tile);
    }
};

class MetaspriteField final : public QWidget {
    Q_OBJECT

public:
    explicit MetaspriteField(int zoom, QWidget* parent = nullptr);

    void setBrush(const chr::Cell* cell, const QString& entry);

    bool addSprite(const chr::Cell& cell, const QString& entry, int col, int row, Mirror mirror);

    void setTileMode(chr::TileMode mode);

    int renameFrame(const QString& from, const QString& to);

    void setFieldSize(int tilesX, int tilesY);

    [[nodiscard]] int tilesX() const { return tilesX_; }
    [[nodiscard]] int tilesY() const { return tilesY_; }

    [[nodiscard]] int slotHeightPx() const;

    void setPaletteTable(const QVector<QRgb>& table);

    void clearSprites();

    [[nodiscard]] const std::vector<PlacedSprite>& sprites() const { return sprites_; }
    [[nodiscard]] int spriteCount() const { return static_cast<int>(sprites_.size()); }

    [[nodiscard]] QImage render() const;

    [[nodiscard]] QSize sizeHint() const override;

signals:
    void statusChanged(const QString& text);

    void spritesChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Brush {
        int     id = 0;
        int     patternByte = 0;
        int     heightTiles = 1;
        QString frame;
        QString entry;
        QString include;
        int     tile = 0;
        QImage  image;

        [[nodiscard]] QString source() const {
            return QString("%1::%2 tile %3").arg(entry, include).arg(tile);
        }
    };

    [[nodiscard]] std::optional<QPoint> slotAt(const QPoint& pos) const;

    [[nodiscard]] int spriteAt(const QPoint& slot) const;

    [[nodiscard]] QRect slotRect(const QPoint& slot) const;

    void reportSlot(const std::optional<QPoint>& slot);

    int                       zoom_;
    QVector<QRgb>             table_ = paletteTable(nullptr);
    chr::TileMode             mode_ = chr::TileMode::Size8x8;
    int                       tilesX_ = kDefaultFieldTilesX;
    int                       tilesY_ = kDefaultFieldTilesY;
    std::optional<Brush>      brush_;
    std::vector<PlacedSprite> sprites_;
    std::optional<QPoint>     hovered_;
};

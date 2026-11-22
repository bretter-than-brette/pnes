/**
 * @file tile_canvas.hpp
 * @brief Widget that draws a chr::Table cells as a scrollable tile grid.
 */
#pragma once

#include "chr_table.hpp"
#include "tile_image.hpp"

#include <QWidget>
#include <QImage>
#include <QRect>
#include <QSize>

#include <vector>

constexpr int kCellsPerRow = 16;

int zoomForScreen(const QRect& screenGeometry);

class TileCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit TileCanvas(int zoom, QWidget* parent = nullptr);

    void setTable(const chr::Table& table);

    void clear();

    void setPaletteTable(const QVector<QRgb>& table);

    void setSelected(int cellIndex);

    [[nodiscard]] int selected() const { return selected_; }

    [[nodiscard]] int cellCount() const { return static_cast<int>(cells_.size()); }

    [[nodiscard]] QSize sizeHint() const override;

signals:
    void cellHovered(int cellIndex);

    void cellPicked(int cellIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Placement {
        QRect rect;
    };

    [[nodiscard]] int cellAt(const QPoint& pos) const;

    int                    zoom_;
    QVector<QRgb>          table_ = paletteTable(nullptr);
    QImage                 sheet_;         ///< 1:1 NES-pixel indexed image of the whole sheet.
    std::vector<Placement> cells_;         ///< Parallel to the table's cells.
    int                    hovered_ = -1;  ///< Last cell reported through cellHovered().
    int                    selected_ = -1; ///< Picked cell, outlined; -1 for none.
};

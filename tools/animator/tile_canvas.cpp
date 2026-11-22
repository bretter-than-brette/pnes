#include "tile_canvas.hpp"
#include "tile_image.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QColor>
#include <QPen>
#include <QPalette>

#include <algorithm>

namespace {

constexpr double kTilePxAt1080p   = 32.0;
constexpr double kReferenceHeight = 1080.0;

} // namespace

int zoomForScreen(const QRect& screenGeometry) {
    const double tilePx = kTilePxAt1080p * (screenGeometry.height() / kReferenceHeight);
    return std::max(1, static_cast<int>(tilePx / chr::kTilePixels));
}

TileCanvas::TileCanvas(int zoom, QWidget* parent)
    : QWidget(parent), zoom_(std::max(1, zoom)) {
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(table_[0]));
    setPalette(pal);

    setMouseTracking(true);
}

void TileCanvas::clear() {
    sheet_ = QImage();
    cells_.clear();
    hovered_ = -1;
    selected_ = -1;
    updateGeometry();
    update();
}

void TileCanvas::setTable(const chr::Table& table) {
    clear();
    if (table.cells.empty()) return;

    cells_.resize(table.cells.size());
    int y = 0;
    for (std::size_t first = 0; first < table.cells.size(); first += kCellsPerRow) {
        const std::size_t last = std::min(first + kCellsPerRow, table.cells.size());

        int rowHeight = 0;
        for (std::size_t i = first; i < last; ++i) {
            rowHeight = std::max(rowHeight, table.cells[i].heightPx());
        }

        for (std::size_t i = first; i < last; ++i) {
            const chr::Cell& cell = table.cells[i];
            const int x = static_cast<int>(i - first) * chr::kTilePixels;
            cells_[i].rect = QRect(x, y, cell.widthPx(), cell.heightPx());
        }

        y += rowHeight;
    }

    sheet_ = QImage(kCellsPerRow * chr::kTilePixels, y, QImage::Format_Indexed8);
    sheet_.setColorTable(table_);
    sheet_.fill(0);

    for (std::size_t i = 0; i < table.cells.size(); ++i) {
        const chr::Cell& cell = table.cells[i];
        const QRect&     rect = cells_[i].rect;

        for (int row = 0; row < rect.height(); ++row) {
            std::uint8_t* scanline = sheet_.scanLine(rect.top() + row) + rect.left();
            const std::uint8_t* source = cell.pixels.data() + static_cast<std::size_t>(row) * cell.widthPx();
            std::copy_n(source, rect.width(), scanline);
        }
    }

    updateGeometry();
    update();
}

QSize TileCanvas::sizeHint() const {
    const int width = kCellsPerRow * chr::kTilePixels * zoom_;
    const int height = sheet_.isNull() ? chr::kTilePixels * zoom_ : sheet_.height() * zoom_;
    return {width, height};
}

void TileCanvas::paintEvent(QPaintEvent* event) {
    if (sheet_.isNull()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const QRect target(0, 0, sheet_.width() * zoom_, sheet_.height() * zoom_);
    const QRect dirty = event->rect().intersected(target);
    if (dirty.isEmpty()) return;

    const QRect source(dirty.left() / zoom_,
                       dirty.top() / zoom_,
                       (dirty.right() / zoom_) - (dirty.left() / zoom_) + 1,
                       (dirty.bottom() / zoom_) - (dirty.top() / zoom_) + 1);
    const QRect snapped(source.left() * zoom_,
                        source.top() * zoom_,
                        source.width() * zoom_,
                        source.height() * zoom_);
    painter.drawImage(snapped, sheet_, source);

    if (selected_ >= 0 && static_cast<std::size_t>(selected_) < cells_.size()) {
        const QRect cell = cells_[static_cast<std::size_t>(selected_)].rect;
        const QRect outline(cell.left() * zoom_, cell.top() * zoom_,
                            cell.width() * zoom_, cell.height() * zoom_);
        painter.setPen(QPen(QColor(0xFF, 0xFF, 0xFF), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(outline.adjusted(0, 0, -1, -1));
    }
}

void TileCanvas::setPaletteTable(const QVector<QRgb>& table) {
    table_ = table;

    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(table_[0]));
    setPalette(pal);

    if (!sheet_.isNull()) sheet_.setColorTable(table_);
    update();
}

void TileCanvas::setSelected(int cellIndex) {
    if (cellIndex == selected_) return;
    const int previous = selected_;
    selected_ = (cellIndex >= 0 && static_cast<std::size_t>(cellIndex) < cells_.size()) ? cellIndex : -1;

    for (const int index : {previous, selected_}) {
        if (index < 0) continue;
        const QRect cell = cells_[static_cast<std::size_t>(index)].rect;
        update(QRect(cell.left() * zoom_, cell.top() * zoom_,
                     cell.width() * zoom_, cell.height() * zoom_));
    }
}

int TileCanvas::cellAt(const QPoint& pos) const {
    const QPoint nes(pos.x() / zoom_, pos.y() / zoom_);
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        if (cells_[i].rect.contains(nes)) return static_cast<int>(i);
    }
    return -1;
}

void TileCanvas::mouseMoveEvent(QMouseEvent* event) {
    const int cell = cellAt(event->pos());
    if (cell != hovered_) {
        hovered_ = cell;
        emit cellHovered(cell);
    }
    QWidget::mouseMoveEvent(event);
}

void TileCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (const int cell = cellAt(event->pos()); cell >= 0) {
            setSelected(cell);
            emit cellPicked(cell);
        }
    }
    QWidget::mousePressEvent(event);
}

void TileCanvas::leaveEvent(QEvent* event) {
    if (hovered_ != -1) {
        hovered_ = -1;
        emit cellHovered(-1);
    }
    QWidget::leaveEvent(event);
}

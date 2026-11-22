/**
 * @file metasprite_page.hpp
 * @brief One metasprite tab: its attributes, and a tab per animation frame.
 */
#pragma once

#include "animation_preview.hpp"
#include "chr_table.hpp"
#include "metasprite_frame.hpp"
#include "palette_store.hpp"

#include <QWidget>
#include <QString>

#include <vector>

class QComboBox;
class QLabel;
class QToolButton;
class QLineEdit;
class QSpinBox;
class QTabWidget;

class MetaspritePage final : public QWidget {
    Q_OBJECT

public:
    MetaspritePage(int zoom, PaletteStore* palettes, QWidget* parent = nullptr);

    [[nodiscard]] QString name() const;
    void setName(const QString& name);

    [[nodiscard]] QString savedStem() const { return savedStem_; }
    void setSavedStem(const QString& stem) { savedStem_ = stem; }

    [[nodiscard]] QString paletteName() const;
    void setPaletteName(const QString& name);

    void setSize(int tilesX, int tilesY);

    [[nodiscard]] int tilesX() const { return tilesX_; }
    [[nodiscard]] int tilesY() const { return tilesY_; }

    [[nodiscard]] const std::vector<MetaspriteFrame*>& frames() const { return frameList_; }

    MetaspriteFrame* appendFrame();

    void clearFrames();

    [[nodiscard]] MetaspriteFrame* currentFrame() const;

    [[nodiscard]] int frameCount() const;

    [[nodiscard]] bool hasContent() const;

    void newFrame();

    void closeFrame(int index = -1);

    [[nodiscard]] QString summary() const;

    void setTileMode(chr::TileMode mode);

    void setBrush(const chr::Cell* cell, const QString& source);

signals:
    void statusChanged(const QString& text);

    void nameChanged(const QString& name);

    void framesChanged();

    void dirtied();

private:
    void applySize();

    void refreshPalettes();

    void applyPalette();

    void refreshDerived();

    void refreshPreview();

    void retitle(MetaspriteFrame* frame);

    void adopt(MetaspriteFrame* frame);

    int                           zoom_;
    chr::TileMode                 mode_ = chr::TileMode::Size8x8;
    int                           tilesX_ = kDefaultFieldTilesX;
    int                           tilesY_ = kDefaultFieldTilesY;
    int                           frameIdCounter_ = 0;  ///< Hands out MetaspriteFrame ids.
    int                           frameNumber_ = 0;   ///< Numbers default frame names.
    QTabWidget*                   frames_ = nullptr;
    QLineEdit*                    name_ = nullptr;
    QComboBox*                    palette_ = nullptr;
    PaletteStore*                 palettes_ = nullptr;
    QString                       savedStem_;
    QSpinBox*                     width_ = nullptr;
    QSpinBox*                     height_ = nullptr;
    QLabel*                       derived_ = nullptr;
    AnimationPreview*             preview_ = nullptr;
    QToolButton*                  play_ = nullptr;
    QLabel*                       previewLabel_ = nullptr;
    std::vector<MetaspriteFrame*> frameList_;         ///< Every frame, tab order.
};

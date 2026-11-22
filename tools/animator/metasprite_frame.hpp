/**
 * @file metasprite_frame.hpp
 * @brief One animation frame: its name, timer, successor and the field holding
 *        its sprites.
 */
#pragma once

#include "chr_table.hpp"
#include "metasprite_field.hpp"
#include "tile_image.hpp"

#include <QWidget>
#include <QString>

class QRegularExpressionValidator;

class QCheckBox;
class QLineEdit;
class QScrollArea;
class QSpinBox;

constexpr int kMinTimerFrames     = 0;
constexpr int kMaxTimerFrames     = 255;
constexpr int kDefaultTimerFrames = 8;

constexpr int kStopTimer = 0;

[[nodiscard]] QString identifierSafe(const QString& name);

[[nodiscard]] QRegularExpressionValidator* identifierValidator(QObject* parent);

class MetaspriteFrame final : public QWidget {
    Q_OBJECT

public:
    MetaspriteFrame(int id, int zoom, const QString& name, QWidget* parent = nullptr);

    [[nodiscard]] int frameId() const { return id_; }
    [[nodiscard]] QString frameName() const;
    [[nodiscard]] int timerFrames() const;

    [[nodiscard]] bool isPublicFrame() const;

    [[nodiscard]] MetaspriteField* field() const { return field_; }

    void setFrameName(const QString& name);

    void setTimerFrames(int frames);

    void setPublicFrame(bool isPublic);

    void setTileMode(chr::TileMode mode);
    void setPaletteTable(const QVector<QRgb>& table);
    void setBrush(const chr::Cell* cell, const QString& source);

signals:
    void nameChanged(const QString& name);

    void attributesChanged();

    void statusChanged(const QString& text);

    void spritesChanged();

private:
    int              id_;
    QLineEdit*       name_ = nullptr;
    QSpinBox*        timer_ = nullptr;
    QCheckBox*       public_ = nullptr;
    QScrollArea*     scroll_ = nullptr;
    MetaspriteField* field_ = nullptr;
};

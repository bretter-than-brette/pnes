/**
 * @file animation_preview.hpp
 * @brief A small canvas that plays the metasprite being worked on.
 */
#pragma once

#include <QImage>
#include <QString>
#include <QWidget>

#include <vector>

class QTimer;

constexpr double kNesFrameMs = 1000.0 / 60.0988;

struct PreviewCel {
    QImage  image;     ///< The whole metasprite, as the PPU would show it.
    int     hold = 0;  ///< NES frames to hold; 0 stops the animation here.
    QString name;
};

class AnimationPreview final : public QWidget {
    Q_OBJECT

public:
    explicit AnimationPreview(int zoom, QWidget* parent = nullptr);

    void setCels(std::vector<PreviewCel> cels);

    void setPlaying(bool playing);
    [[nodiscard]] bool isPlaying() const;

    void restart();

    [[nodiscard]] QSize sizeHint() const override;

signals:
    void playingChanged(bool playing);

    void frameChanged(const QString& name, int position, int count);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void tick();

    void announce();

    int                     box_;        ///< Widget side, in device pixels.
    QTimer*                 clock_ = nullptr;
    std::vector<PreviewCel> cels_;
    std::size_t             current_ = 0;
    int                     remaining_ = 0;  ///< NES frames left on the current cel.
};

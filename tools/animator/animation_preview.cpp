#include "animation_preview.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QTimer>

#include <algorithm>

namespace {

constexpr int kBoxNesPx = 40;

} // namespace

AnimationPreview::AnimationPreview(const int zoom, QWidget* parent)
    : QWidget(parent), box_(kBoxNesPx * std::max(1, zoom)) {
    setFixedSize(box_, box_);

    clock_ = new QTimer(this);
    clock_->setInterval(static_cast<int>(kNesFrameMs + 0.5));
    connect(clock_, &QTimer::timeout, this, &AnimationPreview::tick);
}

QSize AnimationPreview::sizeHint() const {
    return {box_, box_};
}

void AnimationPreview::setCels(std::vector<PreviewCel> cels) {
    const bool wasPlaying = isPlaying();
    cels_ = std::move(cels);

    if (cels_.empty()) {
        setPlaying(false);
        current_ = 0;
        remaining_ = 0;
        update();
        announce();
        return;
    }

    current_ = std::min(current_, cels_.size() - 1);
    remaining_ = cels_[current_].hold;
    if (wasPlaying && remaining_ == 0) setPlaying(false);  // the edit made this a stop

    update();
    announce();
}

bool AnimationPreview::isPlaying() const {
    return clock_->isActive();
}

void AnimationPreview::setPlaying(const bool playing) {
    if (playing) {
        if (!cels_.empty() && cels_[current_].hold == 0) {
            current_ = 0;
            remaining_ = cels_[0].hold;
            update();
            announce();
        }
        if (!cels_.empty() && cels_[current_].hold != 0) clock_->start();
    } else {
        clock_->stop();
    }

    emit playingChanged(isPlaying());
}

void AnimationPreview::restart() {
    if (cels_.empty()) return;
    current_ = 0;
    remaining_ = cels_[0].hold;
    update();
    announce();
}

void AnimationPreview::tick() {
    if (cels_.empty()) {
        setPlaying(false);
        return;
    }

    if (--remaining_ > 0) return;

    current_ = (current_ + 1) % cels_.size();
    remaining_ = cels_[current_].hold;
    update();
    announce();

    if (remaining_ == 0) setPlaying(false);
}

void AnimationPreview::announce() {
    if (cels_.empty()) {
        emit frameChanged(QString(), 0, 0);
        return;
    }
    emit frameChanged(cels_[current_].name,
                      static_cast<int>(current_) + 1,
                      static_cast<int>(cels_.size()));
}

void AnimationPreview::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.fillRect(event->rect(), Qt::black);

    if (cels_.empty()) return;
    const QImage& image = cels_[current_].image;
    if (image.isNull()) return;

    const int longest = std::max(image.width(), image.height());
    const int zoom = box_ / std::max(1, longest);

    QRect target;
    if (zoom >= 1) {
        target = QRect(0, 0, image.width() * zoom, image.height() * zoom);
    } else {
        const QSize fitted = image.size().scaled(box_, box_, Qt::KeepAspectRatio);
        target = QRect(QPoint(0, 0), fitted);
    }
    target.moveCenter(rect().center());

    painter.drawImage(target, image);
}

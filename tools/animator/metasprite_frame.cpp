#include "metasprite_frame.hpp"

#include <QBoxLayout>
#include <QCheckBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QAbstractSpinBox>
#include <QSpinBox>
#include <QVariant>

QString identifierSafe(const QString& name) {
    QString out;
    out.reserve(name.size());
    for (const QChar ch : name) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                     || (ch >= '0' && ch <= '9') || ch == '_';
        out += ok ? ch : QChar('_');
    }
    if (!out.isEmpty() && out.front() >= '0' && out.front() <= '9') out.prepend('_');
    return out;
}

QRegularExpressionValidator* identifierValidator(QObject* parent) {
    return new QRegularExpressionValidator(QRegularExpression("[A-Za-z_][A-Za-z0-9_]*"), parent);
}

MetaspriteFrame::MetaspriteFrame(const int id, const int zoom, const QString& name, QWidget* parent)
    : QWidget(parent), id_(id) {
    field_ = new MetaspriteField(zoom, nullptr);
    connect(field_, &MetaspriteField::statusChanged, this, &MetaspriteFrame::statusChanged);
    connect(field_, &MetaspriteField::spritesChanged, this, &MetaspriteFrame::spritesChanged);

    auto* attributes = new QGroupBox("Frame", this);
    auto* form = new QFormLayout(attributes);

    name_ = new QLineEdit(attributes);
    name_->setValidator(identifierValidator(name_));
    name_->setText(identifierSafe(name));
    connect(name_, &QLineEdit::textChanged, this, &MetaspriteFrame::nameChanged);

    timer_ = new QSpinBox(attributes);
    timer_->setRange(kMinTimerFrames, kMaxTimerFrames);
    timer_->setValue(kDefaultTimerFrames);
    timer_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    timer_->setAlignment(Qt::AlignRight);
    connect(timer_, &QSpinBox::valueChanged, this, [this] { emit attributesChanged(); });

    public_ = new QCheckBox("Name this frame's bytes in the generated header", attributes);
    public_->setToolTip("The generated header's frame table is private. A public frame also\n"
                        "gets a kTiles_<name> of its own -- for animations a consumer reads a\n"
                        "frame of directly instead of playing, where there is no timer to count.");
    connect(public_, &QCheckBox::toggled, this, [this] { emit attributesChanged(); });

    form->addRow("&Name", name_);
    form->addRow("&Timer (NES frames, 0 stops)", timer_);
    form->addRow("P&ublic", public_);

    scroll_ = new QScrollArea(this);
    scroll_->setWidget(field_);
    scroll_->setWidgetResizable(false);
    scroll_->setAlignment(Qt::AlignCenter);
    scroll_->setFrameShape(QFrame::NoFrame);
    field_->resize(field_->sizeHint());

    auto* rows = new QVBoxLayout(this);
    rows->addWidget(attributes);
    rows->addWidget(scroll_, 1);
}

QString MetaspriteFrame::frameName() const {
    return name_->text();
}

int MetaspriteFrame::timerFrames() const {
    return timer_->value();
}

bool MetaspriteFrame::isPublicFrame() const {
    return public_->isChecked();
}

void MetaspriteFrame::setFrameName(const QString& name) {
    name_->setText(identifierSafe(name));  // textChanged carries it on to the tab label
}

void MetaspriteFrame::setTimerFrames(const int frames) {
    timer_->setValue(frames);  // QSpinBox clamps to its own range
}

void MetaspriteFrame::setPublicFrame(const bool isPublic) {
    public_->setChecked(isPublic);
}

void MetaspriteFrame::setTileMode(const chr::TileMode mode) {
    field_->setTileMode(mode);
    field_->resize(field_->sizeHint());
}

void MetaspriteFrame::setPaletteTable(const QVector<QRgb>& table) {
    field_->setPaletteTable(table);
}

void MetaspriteFrame::setBrush(const chr::Cell* cell, const QString& source) {
    field_->setBrush(cell, source);
}

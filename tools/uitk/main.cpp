// 'pnes' UI Toolkit: a scene editor that exports a target-specific .hpp/.cpp
// pair per scene. See the code-export banner further down for what is emitted.

#include <QApplication>
#include <QMainWindow>
#include <QDockWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScreen>
#include <QCursor>
#include <QIntValidator>
#include <QFontMetrics>
#include <QtGlobal>
#include <QPainter>
#include <QImage>
#include <QFont>
#include <QObject>
#include <QEvent>
#include <algorithm>
#include <functional>
#include <QTimer>
#include <QTreeWidget>
#include <QAbstractItemModel>
#include <QMenu>
#include <QPushButton>
#include <QGroupBox>
#include <QMouseEvent>
#include <QColor>
#include <QVector>
#include <QSplitter>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QFontDatabase>
#include <QMenuBar>
#include <QStatusBar>
#include <QAction>
#include <QKeySequence>
#include <QShortcut>
#include <QFileDialog>
#include <QInputDialog>
#include <QDir>
#include <QMessageBox>
#include <QCloseEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QtAlgorithms>
#include <QRegularExpression>
#include <QSet>
#include <QHash>
#include <QBrush>
#include <QTabWidget>
#include <QStackedWidget>
#include <array>
#include <map>
#include <optional>
#include <utility>
#include <vector>
#include <memory>
#include <cstdio>

namespace {

constexpr double kTilePxAt1080p = 8.0;
constexpr double kReferenceHeight = 1080.0;

constexpr int kMinGridDimensionPx = 100;

constexpr double kSidebarWidthPxAt1080p = 200.0;

constexpr int kWindowChromeMarginPx = 60;

QString g_sceneNameOverride;

double tilePxForScreen(const QRect& screenGeometry) {
    return kTilePxAt1080p * (screenGeometry.height() / kReferenceHeight);
}

int sidebarWidthForScreen(const QRect& screenGeometry) {
    return qRound(kSidebarWidthPxAt1080p * (screenGeometry.height() / kReferenceHeight));
}

void lockWindowToContents(QMainWindow* window) {
    window->setMinimumSize(0, 0);
    window->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    window->adjustSize();

    QSize size = window->size();
    if (const QScreen* screen = window->screen()) {
        size = size.boundedTo(screen->availableGeometry().size());
    }
    window->setFixedSize(size);
}

class LayoutChangeNotifier : public QObject {
public:
    LayoutChangeNotifier(std::function<void()> callback, QObject* parent)
        : QObject(parent), callback_(std::move(callback)) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::LayoutRequest) {
            callback_();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> callback_;
};

class UitkMainWindow : public QMainWindow {
public:
    using QMainWindow::QMainWindow;
    std::function<bool()> confirmClose;

protected:
    void closeEvent(QCloseEvent* event) override {
        if (confirmClose && !confirmClose()) {
            event->ignore();
            return;
        }
        QMainWindow::closeEvent(event);
    }
};

constexpr int kKindRole = Qt::UserRole;
constexpr int kTextContentRole = Qt::UserRole + 1;
constexpr int kPosXRole = Qt::UserRole + 2;
constexpr int kPosYRole = Qt::UserRole + 3;
constexpr int kSizeWRole = Qt::UserRole + 4;
constexpr int kSizeHRole = Qt::UserRole + 5;
constexpr int kAlignRole = Qt::UserRole + 6;
constexpr int kPosXExprRole = Qt::UserRole + 7;
constexpr int kPosYExprRole = Qt::UserRole + 8;
constexpr int kSizeWExprRole = Qt::UserRole + 9;
constexpr int kSizeHExprRole = Qt::UserRole + 10;
constexpr int kErrorRole = Qt::UserRole + 11;
constexpr int kLastValidNameRole = Qt::UserRole + 12;
constexpr int kErrorMaskRole = Qt::UserRole + 13;
constexpr int kNegSpaceViolationRole = Qt::UserRole + 14;
constexpr int kSplitterRole = Qt::UserRole + 15;
constexpr int kHideTargetsRole = Qt::UserRole + 16;
constexpr int kDefaultOptionRole = Qt::UserRole + 18;
constexpr int kCharmapOverrideRole = Qt::UserRole + 19;
constexpr int kProvideErasingRole = Qt::UserRole + 20;
constexpr int kButtonDefaultEnabledRole = Qt::UserRole + 21;
constexpr int kButtonToggleableRole = Qt::UserRole + 23;
constexpr int kButtonHasBoxRole = Qt::UserRole + 24;
constexpr int kCanvasVerticalRole = Qt::UserRole + 25;
constexpr int kSingleChoiceVerticalRole = Qt::UserRole + 26;
constexpr int kSingleChoiceHasRectRole = Qt::UserRole + 27;
constexpr int kSliderVerticalRole = Qt::UserRole + 28;
constexpr char kCtTextBoxKind[] = "ctTextBox";
constexpr char kRtTextBoxKind[] = "rtTextBox";
constexpr char kNegSpaceKind[] = "negspace";
constexpr char kSingleChoiceKind[] = "singlechoice";
constexpr char kButtonKind[] = "button";
constexpr char kSliderKind[] = "slider";
constexpr char kCanvasKind[] = "canvas";
constexpr char kRectKind[] = "rect";
constexpr char kCtTextBoxPrefix[] = "[CT] ";
constexpr char kRtTextBoxPrefix[] = "[RT] ";
constexpr char kNegSpacePrefix[] = "[N] ";
constexpr char kSingleChoicePrefix[] = "[SC] ";
constexpr char kButtonPrefix[] = "[B] ";
constexpr char kSliderPrefix[] = "[S] ";
constexpr char kCanvasPrefix[] = "[C] ";
constexpr char kRectPrefix[] = "[R] ";

const QStringList kTargetNames = {"NES", "GBA", "GCN", "DS",     "Wii",
                                   "Wii U", "3DS", "Switch", "PSP",  "PC"};


std::optional<std::pair<int, int>> viewportTilesForTarget(const QString& target) {
    static const QHash<QString, std::pair<int, int>> kSizes{
        {"NES", {32, 30}},    // hardware PPU, fixed 256x240
        {"GBA", {30, 20}},    // 240x160 panel
        {"GCN", {40, 30}},    // runtime TV width; 40 = a standard 640x480 NTSC mode
        {"DS", {32, 48}},     // both 256x192 panels stacked: 256x384
        {"Wii", {40, 30}},    // same runtime-TV path as GCN
        {"Wii U", {52, 30}},  // widescreen, scaled to fill the panel
        {"3DS", {50, 60}},    // 400x240 top over 320x240 bottom: 400x480
        {"Switch", {52, 30}}, // widescreen, scaled to fill the panel
        {"PSP", {60, 30}},    // 480x272 panel, letterboxed to 480x240
    };
    const auto it = kSizes.constFind(target);
    if (it == kSizes.constEnd()) return std::nullopt;
    return *it;
}

struct PanelTiles {
    int x, y, tx, ty;
};

std::optional<PanelTiles> panel1TilesForTarget(const QString& target) {
    static const QHash<QString, PanelTiles> kPanels{
        {"DS", {0, 24, 32, 24}},    // 256x192 under a 256x192 top, in a 32x48 viewport
        {"3DS", {5, 30, 40, 30}},   // 320x240 centred under a 400x240 top, in a 50x60 viewport
    };
    const auto it = kPanels.constFind(target);
    if (it == kPanels.constEnd()) return std::nullopt;
    return *it;
}

enum class TextAlign { Left = 0, Center = 1, Right = 2 };

QStringList wrapTextIntoRows(const QString& text, QChar splitter, int w) {
    const QStringList words = text.split(splitter);
    QStringList rows;
    QString current;
    for (const QString& word : words) {
        if (current.isEmpty()) {
            current = word;
        } else if (current.length() + 1 + word.length() <= w) {
            current += splitter;
            current += word;
        } else {
            rows << current;
            current = word;
        }
    }
    rows << current;
    return rows;
}

bool isCtTextBoxItem(const QTreeWidgetItem* item) {
    return item && item->data(0, kKindRole).toString() == QLatin1String(kCtTextBoxKind);
}

bool isRtTextBoxItem(const QTreeWidgetItem* item) {
    return item && item->data(0, kKindRole).toString() == QLatin1String(kRtTextBoxKind);
}

bool isComponentItem(const QTreeWidgetItem* item) {
    return isCtTextBoxItem(item) || isRtTextBoxItem(item);
}

bool isNegSpaceItem(const QTreeWidgetItem* item) {
    return item && item->data(0, kKindRole).toString() == QLatin1String(kNegSpaceKind);
}

bool isRectItem(const QTreeWidgetItem* item) {
    return item && item->data(0, kKindRole).toString() == QLatin1String(kRectKind);
}

bool isSingleChoiceItem(const QTreeWidgetItem* item) {
    return item && item->data(0, kKindRole).toString() == QLatin1String(kSingleChoiceKind);
}

bool isButtonItem(const QTreeWidgetItem* item) {
    return item && item->data(0, kKindRole).toString() == QLatin1String(kButtonKind);
}

bool isSliderItem(const QTreeWidgetItem* item) {
    return item && item->data(0, kKindRole).toString() == QLatin1String(kSliderKind);
}

bool isCanvasItem(const QTreeWidgetItem* item) {
    return item && item->data(0, kKindRole).toString() == QLatin1String(kCanvasKind);
}

bool isGeometryItem(const QTreeWidgetItem* item) {
    return isComponentItem(item) || isNegSpaceItem(item) || isButtonItem(item) || isSliderItem(item) ||
           isRectItem(item);
}

bool isPrefixedItem(const QTreeWidgetItem* item) {
    return isGeometryItem(item) || isSingleChoiceItem(item) || isCanvasItem(item);
}

QString requiredPrefixFor(const QTreeWidgetItem* item) {
    if (isCtTextBoxItem(item)) return QString(kCtTextBoxPrefix);
    if (isRtTextBoxItem(item)) return QString(kRtTextBoxPrefix);
    if (isNegSpaceItem(item)) return QString(kNegSpacePrefix);
    if (isSingleChoiceItem(item)) return QString(kSingleChoicePrefix);
    if (isButtonItem(item)) return QString(kButtonPrefix);
    if (isSliderItem(item)) return QString(kSliderPrefix);
    if (isCanvasItem(item)) return QString(kCanvasPrefix);
    if (isRectItem(item)) return QString(kRectPrefix);
    return QString();
}

QVector<QTreeWidgetItem*> collectComponentItems(QTreeWidgetItem* node) {
    QVector<QTreeWidgetItem*> result;
    std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* n) {
        for (int i = 0; i < n->childCount(); ++i) {
            QTreeWidgetItem* child = n->child(i);
            if (isGeometryItem(child)) {
                result.push_back(child);
            }
            visit(child);
        }
    };
    visit(node);
    return result;
}

QVector<QTreeWidgetItem*> collectPrefixedItems(QTreeWidgetItem* node) {
    QVector<QTreeWidgetItem*> result;
    std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* n) {
        for (int i = 0; i < n->childCount(); ++i) {
            QTreeWidgetItem* child = n->child(i);
            if (isPrefixedItem(child)) {
                result.push_back(child);
            }
            visit(child);
        }
    };
    visit(node);
    return result;
}

bool isEffectivelyHidden(const QTreeWidgetItem* item, const QString& target) {
    for (const QTreeWidgetItem* n = item; n && isPrefixedItem(n); n = n->parent()) {
        if (n->data(0, kHideTargetsRole).toStringList().contains(target)) return true;
    }
    return false;
}

QVector<QTreeWidgetItem*> singleChoiceMembers(const QTreeWidgetItem* scItem, const QString& target) {
    QVector<QTreeWidgetItem*> result;
    for (int i = 0; i < scItem->childCount(); ++i) {
        QTreeWidgetItem* child = scItem->child(i);
        if (isComponentItem(child) && !isEffectivelyHidden(child, target)) {
            result.push_back(child);
        }
    }
    return result;
}

bool hasGeneratedPass(const QTreeWidgetItem* item, const QString& target) {
    if (isSliderItem(item)) return true;
    if (isButtonItem(item)) return item->data(0, kButtonToggleableRole).toBool();
    return isSingleChoiceItem(item) && !singleChoiceMembers(item, target).isEmpty();
}

bool isCanvasComponent(const QTreeWidgetItem* item, const QString& target) {
    const QTreeWidgetItem* parent = item ? item->parent() : nullptr;
    while (parent && !isPrefixedItem(parent)) parent = parent->parent();
    return parent && isCanvasItem(parent) && hasGeneratedPass(item, target);
}

QVector<QTreeWidgetItem*> canvasComponents(const QTreeWidgetItem* canvasItem, const QString& target) {
    QVector<QTreeWidgetItem*> result;
    std::function<void(const QTreeWidgetItem*)> visit = [&](const QTreeWidgetItem* n) {
        for (int i = 0; i < n->childCount(); ++i) {
            QTreeWidgetItem* child = n->child(i);
            if (!isPrefixedItem(child)) {
                visit(child);  // transparent branch
                continue;
            }
            if (isEffectivelyHidden(child, target)) continue;
            if (hasGeneratedPass(child, target)) result.push_back(child);
        }
    };
    visit(canvasItem);
    return result;
}

struct ExprNode {
    enum class Kind { Number, Ident, Add, Sub, Mul, Div, Shl, Shr, Neg };
    Kind kind = Kind::Number;
    long long number = 0;
    QString identName;  // component base name, "this", or "VIEWPORT_{T,P}{X,Y}"
    int identProp = -1;
    std::shared_ptr<ExprNode> a;
    std::shared_ptr<ExprNode> b;
};
using ExprPtr = std::shared_ptr<ExprNode>;

class ExprParser {
public:
    explicit ExprParser(const QString& src) : s_(src) {}

    ExprPtr parse(bool& ok) {
        ok = true;
        skipSpace();
        ExprPtr e = parseShift(ok);
        skipSpace();
        if (!ok || pos_ != s_.length()) {
            ok = false;
            return nullptr;
        }
        return e;
    }

private:
    void skipSpace() {
        while (pos_ < s_.length() && s_.at(pos_).isSpace()) ++pos_;
    }
    QChar peek() const { return pos_ < s_.length() ? s_.at(pos_) : QChar(); }
    QChar peek2() const { return pos_ + 1 < s_.length() ? s_.at(pos_ + 1) : QChar(); }

    ExprPtr parseShift(bool& ok) {
        ExprPtr left = parseExpr(ok);
        while (ok) {
            skipSpace();
            const QChar c = peek();
            if ((c != '<' && c != '>') || peek2() != c) break;
            pos_ += 2;
            ExprPtr right = parseExpr(ok);
            if (!ok) return nullptr;
            auto node = std::make_shared<ExprNode>();
            node->kind = (c == '<') ? ExprNode::Kind::Shl : ExprNode::Kind::Shr;
            node->a = left;
            node->b = right;
            left = node;
        }
        return ok ? left : nullptr;
    }

    ExprPtr parseExpr(bool& ok) {
        ExprPtr left = parseTerm(ok);
        while (ok) {
            skipSpace();
            const QChar c = peek();
            if (c != '+' && c != '-') break;
            ++pos_;
            ExprPtr right = parseTerm(ok);
            if (!ok) return nullptr;
            auto node = std::make_shared<ExprNode>();
            node->kind = (c == '+') ? ExprNode::Kind::Add : ExprNode::Kind::Sub;
            node->a = left;
            node->b = right;
            left = node;
        }
        return ok ? left : nullptr;
    }

    ExprPtr parseTerm(bool& ok) {
        ExprPtr left = parseFactor(ok);
        while (ok) {
            skipSpace();
            const QChar c = peek();
            if (c != '*' && c != '/') break;
            ++pos_;
            ExprPtr right = parseFactor(ok);
            if (!ok) return nullptr;
            auto node = std::make_shared<ExprNode>();
            node->kind = (c == '*') ? ExprNode::Kind::Mul : ExprNode::Kind::Div;
            node->a = left;
            node->b = right;
            left = node;
        }
        return ok ? left : nullptr;
    }

    ExprPtr parseFactor(bool& ok) {
        skipSpace();
        const QChar c = peek();
        if (c == '-') {
            ++pos_;
            ExprPtr inner = parseFactor(ok);
            if (!ok) return nullptr;
            auto node = std::make_shared<ExprNode>();
            node->kind = ExprNode::Kind::Neg;
            node->a = inner;
            return node;
        }
        if (c == '(') {
            ++pos_;
            ExprPtr inner = parseShift(ok);
            skipSpace();
            if (!ok || peek() != ')') {
                ok = false;
                return nullptr;
            }
            ++pos_;
            return inner;
        }
        if (c.isDigit()) {
            const int start = pos_;
            while (pos_ < s_.length() && s_.at(pos_).isDigit()) ++pos_;
            auto node = std::make_shared<ExprNode>();
            node->kind = ExprNode::Kind::Number;
            node->number = s_.mid(start, pos_ - start).toLongLong();
            return node;
        }
        if (c.isLetter() || c == '_') {
            const int start = pos_;
            while (pos_ < s_.length() &&
                   (s_.at(pos_).isLetterOrNumber() || s_.at(pos_) == '_' || s_.at(pos_) == '.')) {
                ++pos_;
            }
            const QString token = s_.mid(start, pos_ - start);
            const QStringList parts = token.split('.');
            auto node = std::make_shared<ExprNode>();
            node->kind = ExprNode::Kind::Ident;
            if (parts.size() == 1) {
                static const QSet<QString> kViewportIdents{"VIEWPORT_TX", "VIEWPORT_TY", "VIEWPORT_PX",
                                                             "VIEWPORT_PY", "PANEL1_X",  "PANEL1_Y",
                                                             "PANEL1_TX",   "PANEL1_TY"};
                if (!kViewportIdents.contains(token)) {
                    ok = false;
                    return nullptr;
                }
                node->identName = token;
                node->identProp = -1;
                return node;
            }
            if (parts.size() == 3) {
                static const QHash<QString, int> kPropMap{
                    {"pos.x", 0}, {"pos.y", 1}, {"size.x", 2}, {"size.y", 3}};
                const QString propKey = parts.at(1) + "." + parts.at(2);
                if (!kPropMap.contains(propKey)) {
                    ok = false;
                    return nullptr;
                }
                node->identName = parts.at(0);
                node->identProp = kPropMap.value(propKey);
                return node;
            }
            if (parts.size() == 2 && parts.at(1) == QLatin1String("textSize")) {
                node->identName = parts.at(0);
                node->identProp = 4;
                return node;
            }
            if (parts.size() == 2 && parts.at(1) == QLatin1String("enabled")) {
                node->identName = parts.at(0);
                node->identProp = 5;
                return node;
            }
            ok = false;
            return nullptr;
        }
        ok = false;
        return nullptr;
    }

    QString s_;
    int pos_ = 0;
};

std::optional<long long> evalExpr(
    const ExprPtr& node, const std::function<std::optional<long long>(const QString&, int)>& resolveIdent) {
    if (!node) return std::nullopt;
    switch (node->kind) {
        case ExprNode::Kind::Number:
            return node->number;
        case ExprNode::Kind::Ident:
            return resolveIdent(node->identName, node->identProp);
        case ExprNode::Kind::Neg: {
            const auto v = evalExpr(node->a, resolveIdent);
            return v ? std::optional<long long>(-*v) : std::nullopt;
        }
        default: {
            const auto a = evalExpr(node->a, resolveIdent);
            const auto b = evalExpr(node->b, resolveIdent);
            if (!a || !b) return std::nullopt;
            switch (node->kind) {
                case ExprNode::Kind::Add: return *a + *b;
                case ExprNode::Kind::Sub: return *a - *b;
                case ExprNode::Kind::Mul: return *a * *b;
                case ExprNode::Kind::Div: return (*b != 0) ? std::optional<long long>(*a / *b) : std::nullopt;
                case ExprNode::Kind::Shl:
                    return (*b >= 0 && *b < 63) ? std::optional<long long>(*a << *b) : std::nullopt;
                case ExprNode::Kind::Shr:
                    return (*b >= 0 && *b < 63) ? std::optional<long long>(*a >> *b) : std::nullopt;
                default: return std::nullopt;
            }
        }
    }
}

QString emitExprCpp(const ExprPtr& node, const std::function<long long(const QString&, int)>& resolveIdent,
                     const std::function<std::optional<QString>(const QString&, int)>& substituteIdent = nullptr) {
    if (!node) return QStringLiteral("0");
    switch (node->kind) {
        case ExprNode::Kind::Number:
            return QString::number(node->number);
        case ExprNode::Kind::Ident:
            if (node->identProp < 0) {
                if (node->identName == QLatin1String("VIEWPORT_TX")) return QStringLiteral("video::viewport_tx()");
                if (node->identName == QLatin1String("VIEWPORT_TY")) return QStringLiteral("video::viewport_ty()");
                if (node->identName == QLatin1String("VIEWPORT_PX")) return QStringLiteral("video::viewport_px()");
                if (node->identName == QLatin1String("VIEWPORT_PY")) return QStringLiteral("video::viewport_py()");
                if (node->identName == QLatin1String("PANEL1_X"))
                    return QStringLiteral("static_cast<u16>(video::panel_rect(1).x >> 3)");
                if (node->identName == QLatin1String("PANEL1_Y"))
                    return QStringLiteral("static_cast<u16>(video::panel_rect(1).y >> 3)");
                if (node->identName == QLatin1String("PANEL1_TX")) return QStringLiteral("video::panel_tx(1)");
                if (node->identName == QLatin1String("PANEL1_TY")) return QStringLiteral("video::panel_ty(1)");
            }
            if (substituteIdent) {
                if (const auto sub = substituteIdent(node->identName, node->identProp)) return *sub;
            }
            return QString::number(resolveIdent(node->identName, node->identProp));
        case ExprNode::Kind::Neg:
            return QString("-(%1)").arg(emitExprCpp(node->a, resolveIdent, substituteIdent));
        case ExprNode::Kind::Add:
            return QString("(%1 + %2)").arg(emitExprCpp(node->a, resolveIdent, substituteIdent),
                                            emitExprCpp(node->b, resolveIdent, substituteIdent));
        case ExprNode::Kind::Sub:
            return QString("(%1 - %2)").arg(emitExprCpp(node->a, resolveIdent, substituteIdent),
                                            emitExprCpp(node->b, resolveIdent, substituteIdent));
        case ExprNode::Kind::Mul:
            return QString("(%1 * %2)").arg(emitExprCpp(node->a, resolveIdent, substituteIdent),
                                            emitExprCpp(node->b, resolveIdent, substituteIdent));
        case ExprNode::Kind::Div:
            return QString("(%1 / %2)").arg(emitExprCpp(node->a, resolveIdent, substituteIdent),
                                            emitExprCpp(node->b, resolveIdent, substituteIdent));
        case ExprNode::Kind::Shl:
            return QString("(%1 << %2)").arg(emitExprCpp(node->a, resolveIdent, substituteIdent),
                                             emitExprCpp(node->b, resolveIdent, substituteIdent));
        case ExprNode::Kind::Shr:
            return QString("(%1 >> %2)").arg(emitExprCpp(node->a, resolveIdent, substituteIdent),
                                             emitExprCpp(node->b, resolveIdent, substituteIdent));
    }
    return QStringLiteral("0");
}

QJsonObject serializeNode(const QTreeWidgetItem* item) {
    QJsonObject obj;
    obj["name"] = item->text(0);
    if (isGeometryItem(item)) {
        obj["kind"] = item->data(0, kKindRole).toString();
        obj["posXExpr"] = item->data(0, kPosXExprRole).toString();
        obj["posYExpr"] = item->data(0, kPosYExprRole).toString();
        obj["sizeWExpr"] = item->data(0, kSizeWExprRole).toString();
        obj["sizeHExpr"] = item->data(0, kSizeHExprRole).toString();
        if (isComponentItem(item)) {
            obj["align"] = item->data(0, kAlignRole).toInt();
            obj["text"] = item->data(0, kTextContentRole).toString();
            obj["splitter"] = item->data(0, kSplitterRole).toString();
            obj["charmapOverride"] = item->data(0, kCharmapOverrideRole).toString();
            if (isCtTextBoxItem(item)) {
                obj["provideErasing"] = item->data(0, kProvideErasingRole).toBool();
            }
        }
        if (isButtonItem(item)) {
            obj["defaultEnabled"] = item->data(0, kButtonDefaultEnabledRole).toBool();
            obj["toggleable"] = item->data(0, kButtonToggleableRole).toBool();
            obj["hasBox"] = item->data(0, kButtonHasBoxRole).toBool();
        }
        if (isSliderItem(item)) {
            obj["vertical"] = item->data(0, kSliderVerticalRole).toBool();
        }
    } else if (isSingleChoiceItem(item)) {
        obj["kind"] = QStringLiteral("singlechoice");
        obj["defaultOption"] = item->data(0, kDefaultOptionRole).toInt();
        obj["vertical"] = item->data(0, kSingleChoiceVerticalRole).toBool();
        obj["provideBounds"] = item->data(0, kSingleChoiceHasRectRole).toBool();
    } else if (isCanvasItem(item)) {
        obj["kind"] = QStringLiteral("canvas");
        obj["vertical"] = item->data(0, kCanvasVerticalRole).toBool();
    } else {
        obj["kind"] = QStringLiteral("branch");
    }
    if (isPrefixedItem(item)) {
        obj["hideTargets"] = QJsonArray::fromStringList(item->data(0, kHideTargetsRole).toStringList());
    }
    QJsonArray children;
    for (int i = 0; i < item->childCount(); ++i) {
        children.append(serializeNode(item->child(i)));
    }
    obj["children"] = children;
    return obj;
}

void deserializeNode(QTreeWidgetItem* parent, const QJsonObject& obj) {
    auto* item = new QTreeWidgetItem(parent, QStringList{obj["name"].toString()});
    const QString kind = obj["kind"].toString();
    const bool isCtTextBox = (kind == QLatin1String("ctTextBox"));
    const bool isRtTextBox = (kind == QLatin1String("rtTextBox"));
    const bool isComponent = isCtTextBox || isRtTextBox;
    const bool isNegSpace = (kind == QLatin1String("negspace"));
    const bool isButton = (kind == QLatin1String("button"));
    const bool isSlider = (kind == QLatin1String("slider"));
    const bool isRect = (kind == QLatin1String("rect"));
    if (isComponent || isNegSpace || isButton || isSlider || isRect) {
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setData(0, kKindRole,
                       QString(isCtTextBox   ? kCtTextBoxKind
                               : isRtTextBox ? kRtTextBoxKind
                               : isNegSpace  ? kNegSpaceKind
                               : isButton    ? kButtonKind
                               : isRect      ? kRectKind
                                             : kSliderKind));
        const QString posXExpr = obj["posXExpr"].toString(QStringLiteral("0"));
        const QString posYExpr = obj["posYExpr"].toString(QStringLiteral("0"));
        const QString sizeWExpr = obj["sizeWExpr"].toString(QStringLiteral("1"));
        const QString sizeHExpr = obj["sizeHExpr"].toString(QStringLiteral("1"));
        item->setData(0, kPosXExprRole, posXExpr);
        item->setData(0, kPosYExprRole, posYExpr);
        item->setData(0, kSizeWExprRole, sizeWExpr);
        item->setData(0, kSizeHExprRole, sizeHExpr);
        bool ok = false;
        item->setData(0, kPosXRole, posXExpr.toInt(&ok));
        if (!ok) item->setData(0, kPosXRole, 0);
        ok = false;
        item->setData(0, kPosYRole, posYExpr.toInt(&ok));
        if (!ok) item->setData(0, kPosYRole, 0);
        ok = false;
        item->setData(0, kSizeWRole, sizeWExpr.toInt(&ok));
        if (!ok) item->setData(0, kSizeWRole, 1);
        ok = false;
        item->setData(0, kSizeHRole, sizeHExpr.toInt(&ok));
        if (!ok) item->setData(0, kSizeHRole, 1);
        if (isComponent) {
            item->setData(0, kAlignRole, obj["align"].toInt());
            item->setData(0, kTextContentRole, obj["text"].toString());
            item->setData(0, kSplitterRole, obj["splitter"].toString(QStringLiteral(" ")));
            item->setData(0, kCharmapOverrideRole, obj["charmapOverride"].toString());
            if (isCtTextBox) {
                item->setData(0, kProvideErasingRole, obj["provideErasing"].toBool(false));
            }
        }
        if (isButton) {
            item->setData(0, kButtonDefaultEnabledRole, obj["defaultEnabled"].toBool(true));
            item->setData(0, kButtonToggleableRole, obj["toggleable"].toBool(false));
            item->setData(0, kButtonHasBoxRole, obj["hasBox"].toBool(true));
        }
        if (isSlider) {
            item->setData(0, kSliderVerticalRole, obj["vertical"].toBool(false));
        }
        item->setData(0, kLastValidNameRole, item->text(0));
    } else if (kind == QLatin1String("singlechoice")) {
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setData(0, kKindRole, QString(kSingleChoiceKind));
        item->setData(0, kDefaultOptionRole, obj["defaultOption"].toInt(0));
        item->setData(0, kSingleChoiceVerticalRole, obj["vertical"].toBool(true));
        item->setData(0, kSingleChoiceHasRectRole, obj["provideBounds"].toBool(false));
        item->setData(0, kLastValidNameRole, item->text(0));
    } else if (kind == QLatin1String("canvas")) {
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setData(0, kKindRole, QString(kCanvasKind));
        item->setData(0, kCanvasVerticalRole, obj["vertical"].toBool(true));
        item->setData(0, kLastValidNameRole, item->text(0));
    }
    if (isPrefixedItem(item)) {
        item->setData(0, kHideTargetsRole, obj["hideTargets"].toArray().toVariantList());
    }
    for (const QJsonValue& child : obj["children"].toArray()) {
        deserializeNode(item, child.toObject());
    }
}

bool writeUisFile(const QString& path, const QTreeWidgetItem* rootItem, int quadrant, const QString& target,
                   const QString& linkerPrefix, const QString& bssPrefix,
                   const QString& dataPrefix, const QString& charmap, const QString& specialInclude,
                   bool bssSeparate, const QString& bssSymbolPrefix, bool showTileGrid, int tileGridW,
                   int tileGridH) {
    QJsonArray nodes;
    for (int i = 0; i < rootItem->childCount(); ++i) {
        nodes.append(serializeNode(rootItem->child(i)));
    }
    QJsonObject doc;
    doc["version"] = 1;
    doc["quadrant"] = quadrant;
    doc["target"] = target;
    doc["linker_prefix"] = linkerPrefix;
    doc["bss_prefix"] = bssPrefix;
    doc["data_prefix"] = dataPrefix;
    doc["charmap"] = charmap;
    doc["special_include"] = specialInclude;
    doc["bss_separate"] = bssSeparate;
    doc["bss_symbol_prefix"] = bssSymbolPrefix;
    doc["showTileGrid"] = showTileGrid;
    doc["tileGridW"] = tileGridW;
    doc["tileGridH"] = tileGridH;
    doc["nodes"] = nodes;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(doc).toJson());
    return true;
}

bool parseUisFile(const QString& path, QJsonArray& outNodes, int& outQuadrant, QString& outTarget,
                   QString& outLinkerPrefix, QString& outBssPrefix, QString& outDataPrefix,
                   QString& outCharmap, QString& outSpecialInclude, bool& outBssSeparate,
                   QString& outBssSymbolPrefix, bool& outShowTileGrid, int& outTileGridW, int& outTileGridH) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    outNodes = doc.object()["nodes"].toArray();
    const QJsonObject docObj = doc.object();
    const QJsonValue quadrantVal =
        docObj.contains("quadrant") ? docObj["quadrant"] : docObj["nametable"];
    outQuadrant = std::clamp(quadrantVal.toInt(0), 0, 3);
    outTarget = doc.object()["target"].toString();
    outLinkerPrefix = doc.object()["linker_prefix"].toString();
    outBssPrefix = doc.object()["bss_prefix"].toString();
    outDataPrefix = doc.object()["data_prefix"].toString();
    outCharmap = doc.object()["charmap"].toString();
    outSpecialInclude = doc.object()["special_include"].toString();
    outBssSeparate = doc.object()["bss_separate"].toBool(false);
    outBssSymbolPrefix = doc.object()["bss_symbol_prefix"].toString();
    outShowTileGrid = doc.object()["showTileGrid"].toBool(false);
    outTileGridW = std::max(1, doc.object()["tileGridW"].toInt(1));
    outTileGridH = std::max(1, doc.object()["tileGridH"].toInt(1));
    return true;
}

// --- Code export -----------------------------------------------------------
//
// Turns a resolved scene into a target-specific .hpp/.cpp pair. Two textbox
// kinds, never mixed: a ctTextBox's text is known at design time, so wrapping
// and alignment are baked here into a self-contained placement function tagged
// with the scene's linker_prefix; an rtTextBox's is not, so it becomes a thin
// always-inline wrapper around ui::text::Make, defined in the header (an
// always_inline free function needs vague linkage across TUs) and taking only
// the buffer, since Make finds its own end.
//
// Button and Slider nodes emit no bodies at all -- only geometry, one piece of
// state each, and the hooks hand-written code must define. A button's state is a
// bit in the scene-wide `button_state` mask; a slider's is its own `amount`
// byte, which uitk allocates and then leaves alone, because a slider's bounds
// belong to whatever it controls rather than to the scene. Negative-space nodes
// emit nothing. Nothing here decides call order, and nothing draws a
// SingleChoice's selection -- both are the caller's.
//
// Everything lands in `namespace gen::<sceneName>`, so two scenes can reuse
// component names freely.
//
// Positions are baked as constants on a fixed-panel target and re-derived at
// runtime through video::viewport_*() on a variadic one, where VIEWPORT_* cannot
// collapse to a number (see kVariadicTargets).

constexpr int kNametableQuadW = 32;
constexpr int kNametableQuadH = 30;

// Targets whose viewport size is not fixed at compile time -- "gamecube
// onwards", except PSP, which keeps a fixed letterboxed panel. Everything NOT in
// this set bakes a SingleChoice's member positions to constants.
const QSet<QString> kVariadicTargets{"GCN", "DS", "Wii", "Wii U", "3DS", "Switch", "PC"};

// The subset whose viewport is genuinely unknown until the game runs:
// video::viewport_tx() resolves to an extern or a live SDL mode pointer there,
// so an expression over VIEWPORT_* is not a constant expression and a
// `constexpr Rect` would not compile. Deliberately NOT the same split as
// kVariadicTargets -- that one decides whether placement is EMITTED as a runtime
// expression, this one whether the resulting Rect can be constexpr STORAGE.
const QSet<QString> kRuntimePosTargets{"GCN", "Wii", "Wii U", "PC"};

QString bareName(const QTreeWidgetItem* item) {
    return item->text(0).mid(requiredPrefixFor(item).length());
}

QString cStringEscape(const QString& s) {
    QString out;
    out.reserve(s.size());
    for (const QChar ch : s) {
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('"')) out += QLatin1Char('\\');
        out += ch;
    }
    return out;
}

QString cCharEscape(QChar ch) {
    if (ch == QLatin1Char('\\') || ch == QLatin1Char('\'')) return QString("\\") + ch;
    return QString(ch);
}

struct GeneratedFiles {
    QString hpp;
    QString cpp;
    bool cppHasContent = false;
    QString bss;
    bool bssHasContent = false;
};

// --- Separate BSS ----------------------------------------------------------
//
// With Generate BSS Separately set, none of a scene's mutable storage is
// declared in the header: it moves to <scene>/<target>/bss.hpp as plain
// unqualified `type name;` declarations -- no namespace, no guard, nothing a
// compiler could object to being pasted into the middle of a struct the project
// already has. That is the point: a scene's state becomes a member of something
// the game owns rather than an object uitk placed itself.
//
// State only -- a Slider's amount, a SingleChoice's option, the scene-wide
// button_state mask. Geometry, a SingleChoice's anchor table and a Canvas's focus
// index are mutable too and all stay put: they are derived from the layout and
// re-derived by Make() on every boot, so persisting them would keep something
// the next build may contradict.
//
// Emitted bodies still name the storage unqualified, so the header declares a
// reference bound to the real object -- constant-initialized, so it costs
// nothing. The flattened name is the node's path from the scene root, which is
// what guarantees two of them cannot collide.
struct BssSplit {
    bool enabled = false;
    QString symbolPrefix;
    QStringList* decls = nullptr;
    QString path;

    BssSplit at(const QString& nodeName) const {
        BssSplit next = *this;
        next.path = path.isEmpty() ? nodeName : (path + "_" + nodeName);
        return next;
    }

    QString flat(const QString& name) const { return path.isEmpty() ? name : (path + "_" + name); }

    QString decl(const QString& placementTok, const QString& type, const QString& name,
                 const QString& init = QString(), int arrayLen = -1) const {
        const QString subscript = (arrayLen >= 0) ? QString("[%1]").arg(arrayLen) : QString();
        if (!enabled) {
            const QString initTok = init.isEmpty() ? QString() : (" = " + init);
            return QString("%1inline %2 %3%4%5;").arg(placementTok, type, name, subscript, initTok);
        }
        const QString storedType = type.startsWith(QLatin1String("atomic "))
                                       ? type
                                       : ("atomic " + type);
        *decls << QString("%1 %2%3;").arg(storedType, flat(name), subscript);
        const QString owner = symbolPrefix + flat(name);
        if (arrayLen >= 0) {
            return QString("inline %1 (&%2)[%3] = %4;").arg(storedType, name).arg(arrayLen).arg(owner);
        }
        return QString("inline %1& %2 = %3;").arg(storedType, name, owner);
    }
};

// Mirrors src/nes/video.cpp's xy_to_nt_addr exactly, so NES export can bake the
// same VRAM address ppu::CartesianToAddress would compute. Only valid for the
// fixed single-page wrap NES itself uses; never called for another target.
quint16 nesNtAddr(int x, int y) {
    constexpr quint16 base = 0x2000;
    const quint16 ux = static_cast<quint16>(x);
    const quint16 uy = static_cast<quint16>(y);
    const quint16 nt_h = static_cast<quint16>((ux >> 5 & 1) << 10);
    const quint16 col = ux & 0x1F;
    if (uy < 30) {
        return static_cast<quint16>(base + nt_h + (uy << 5) + col);
    }
    const quint16 nt_v = static_cast<quint16>((uy / 30) << 11);
    const quint16 row = uy % 30;
    return static_cast<quint16>(base + nt_h + nt_v + row * 32 + col);
}

struct CtTextBoxGen {
    QString rootDecls;
    QString body;
};

constexpr int kGeomRoles[4] = {kPosXRole, kPosYRole, kSizeWRole, kSizeHRole};
constexpr int kGeomExprRoles[4] = {kPosXExprRole, kPosYExprRole, kSizeWExprRole, kSizeHExprRole};
constexpr long long kGeomDefaults[4] = {0, 0, 1, 1};

// Every node's four geometry properties, resolved for ONE export target.
//
// The editor caches exactly these per node, but resolves them against whatever
// the Target combo is on -- the target the scene was AUTHORED on, which has
// nothing to do with the one being exported. Reading those roles at export time
// baked one target's numbers into every target's header.
//
// So export resolves its own: the same fixed-point loop and vocabulary as the
// editor's, with two differences -- the viewport comes from the target's own
// entry rather than the sidebar, and VIEWPORT_PX/PY are tiles * 8 rather than
// tiles * the canvas zoom, which has no business reaching generated code.
//
// Anything that cannot be resolved keeps the editor's cached value, so a broken
// expression generates what it generated before rather than collapsing to zero.
// Nothing here writes to the tree, so exporting eleven targets in a row does not
// leave the canvas drawing the last one's layout.
struct TargetGeometry {
    QHash<const QTreeWidgetItem*, std::array<long long, 4>> values;

    long long at(const QTreeWidgetItem* item, int prop) const {
        if (!item || prop < 0 || prop > 3) return 0;
        const auto it = values.constFind(item);
        if (it != values.constEnd()) return (*it)[static_cast<size_t>(prop)];
        const QVariant cached = item->data(0, kGeomRoles[prop]);
        return cached.isValid() ? cached.toLongLong() : kGeomDefaults[prop];
    }

    int x(const QTreeWidgetItem* item) const { return static_cast<int>(at(item, 0)); }
    int y(const QTreeWidgetItem* item) const { return static_cast<int>(at(item, 1)); }
    int w(const QTreeWidgetItem* item) const { return std::clamp<int>(static_cast<int>(at(item, 2)), 1, 255); }
    int h(const QTreeWidgetItem* item) const { return std::clamp<int>(static_cast<int>(at(item, 3)), 1, 255); }
};

TargetGeometry resolveGeometryForTarget(QTreeWidgetItem* rootItem, const QString& target) {
    TargetGeometry out;
    const QVector<QTreeWidgetItem*> components = collectComponentItems(rootItem);

    const auto vp = viewportTilesForTarget(target);
    const long long vpTx = vp ? vp->first : kNametableQuadW;
    const long long vpTy = vp ? vp->second : kNametableQuadH;
    const long long vpPx = vpTx * 8;
    const long long vpPy = vpTy * 8;
    const auto panel = panel1TilesForTarget(target);

    QHash<QString, QTreeWidgetItem*> byName;
    QSet<QString> duplicateNames;
    for (QTreeWidgetItem* item : components) {
        const QString base = bareName(item);
        if (byName.contains(base)) duplicateNames.insert(base);
        byName.insert(base, item);
    }

    struct Pending {
        QTreeWidgetItem* item;
        int prop;
        ExprPtr ast;
    };
    QVector<Pending> pending;
    std::map<std::pair<const QTreeWidgetItem*, int>, long long> resolved;

    for (QTreeWidgetItem* item : components) {
        for (int prop = 0; prop < 4; ++prop) {
            QString src = item->data(0, kGeomExprRoles[prop]).toString().trimmed();
            if (src.isEmpty()) src = QString::number(kGeomDefaults[prop]);
            bool ok = false;
            ExprPtr ast = ExprParser(src).parse(ok);
            if (!ok) continue;  // keeps the cached value, see TargetGeometry::at
            pending.push_back({item, prop, ast});
        }
    }

    std::function<std::optional<long long>(const QString&, int)> resolveIdent =
        [&](const QString& name, int prop) -> std::optional<long long> {
        if (prop < 0) {
            if (name == QLatin1String("VIEWPORT_TX")) return vpTx;
            if (name == QLatin1String("VIEWPORT_TY")) return vpTy;
            if (name == QLatin1String("VIEWPORT_PX")) return vpPx;
            if (name == QLatin1String("VIEWPORT_PY")) return vpPy;
            if (name.startsWith(QLatin1String("PANEL1_"))) {
                if (name == QLatin1String("PANEL1_X")) return panel ? panel->x : 0;
                if (name == QLatin1String("PANEL1_Y")) return panel ? panel->y : 0;
                if (name == QLatin1String("PANEL1_TX")) return panel ? panel->tx : vpTx;
                if (name == QLatin1String("PANEL1_TY")) return panel ? panel->ty : vpTy;
            }
            return std::nullopt;
        }
        if (duplicateNames.contains(name)) return std::nullopt;
        const auto it = byName.constFind(name);
        if (it == byName.constEnd()) return std::nullopt;
        if (prop == 4) {
            return isComponentItem(*it)
                       ? std::optional<long long>((*it)->data(0, kTextContentRole).toString().length())
                       : std::nullopt;
        }
        if (prop == 5) return isEffectivelyHidden(*it, target) ? 0 : 1;
        const auto found = resolved.find({*it, prop});
        return found != resolved.end() ? std::optional<long long>(found->second) : std::nullopt;
    };

    auto resolveIdentFor = [&resolveIdent](const QTreeWidgetItem* self) {
        const QString selfName = bareName(self);
        return [&resolveIdent, selfName](const QString& name, int prop) {
            return resolveIdent(name == QLatin1String("this") ? selfName : name, prop);
        };
    };

    bool progress = true;
    while (progress && !pending.isEmpty()) {
        progress = false;
        for (int i = pending.size() - 1; i >= 0; --i) {
            const auto v = evalExpr(pending[i].ast, resolveIdentFor(pending[i].item));
            if (!v) continue;
            resolved[{pending[i].item, pending[i].prop}] = *v;
            pending.removeAt(i);
            progress = true;
        }
    }

    for (QTreeWidgetItem* item : components) {
        std::array<long long, 4> vals{};
        for (int prop = 0; prop < 4; ++prop) {
            const auto it = resolved.find({item, prop});
            long long value = (it != resolved.end()) ? it->second
                                                     : item->data(0, kGeomRoles[prop]).toLongLong();
            value = (prop >= 2) ? std::max<long long>(1, value) : std::max<long long>(0, value);
            vals[static_cast<size_t>(prop)] = value;
        }
        out.values.insert(item, vals);
    }
    return out;
}

long long resolvePositionRef(QTreeWidgetItem* rootItem, const QString& target,
                              const QTreeWidgetItem* self, const QString& rawName, int prop,
                              const TargetGeometry& geom) {
    const QString wanted = (rawName == QLatin1String("this")) ? bareName(self) : rawName;
    for (QTreeWidgetItem* other : collectComponentItems(rootItem)) {
        if (bareName(other) != wanted) continue;
        switch (prop) {
            case 0:
            case 1:
            case 2:
            case 3: return geom.at(other, prop);
            case 4: return other->data(0, kTextContentRole).toString().length();
            case 5: return isEffectivelyHidden(other, target) ? 0 : 1;
            default: return 0;
        }
    }
    return 0;
}

QString emitGeomComponent(const QTreeWidgetItem* item, int prop, bool variadic, QTreeWidgetItem* rootItem,
                           const QString& target, const TargetGeometry& geom,
                           int depth = 0) {
    if (variadic) {
        QString src = item->data(0, kGeomExprRoles[prop]).toString().trimmed();
        if (src.isEmpty()) src = QStringLiteral("0");
        bool ok = false;
        const ExprPtr ast = ExprParser(src).parse(ok);
        if (ok) {
            auto resolveFor = [rootItem, target, item, &geom](const QString& n, int p) {
                return resolvePositionRef(rootItem, target, item, n, p, geom);
            };
            auto substitute = [rootItem, target, item, depth, &geom](const QString& n,
                                                                             int p) -> std::optional<QString> {
                if (p < 0 || p > 3 || depth >= 16) return std::nullopt;
                const QString wanted = (n == QLatin1String("this")) ? bareName(item) : n;
                for (QTreeWidgetItem* other : collectComponentItems(rootItem)) {
                    if (bareName(other) != wanted) continue;
                    return "(" + emitGeomComponent(other, p, true, rootItem, target, geom, depth + 1) + ")";
                }
                return std::nullopt;
            };
            return emitExprCpp(ast, resolveFor, substitute);
        }
    }
    return QString::number(geom.at(item, prop));
}

CtTextBoxGen genCtTextBoxBody(const QTreeWidgetItem* item, int ntOffX, int ntOffY, bool isNes,
                               bool variadic, QTreeWidgetItem* rootItem, const QString& target,
                               const QString& dataPrefixTok, const QString& charmapFn,
                               const QString& ntOffXExpr, const QString& ntOffYExpr,
                               const TargetGeometry& geom) {
    const int x = geom.x(item) + ntOffX;
    const int y = geom.y(item) + ntOffY;
    const int w = geom.w(item);
    const int h = geom.h(item);
    const auto align = static_cast<TextAlign>(item->data(0, kAlignRole).toInt());
    const QString splitterStr = item->data(0, kSplitterRole).toString();
    const QChar splitter = splitterStr.isEmpty() ? QLatin1Char(' ') : splitterStr.at(0);
    const QString text = item->data(0, kTextContentRole).toString();

    QStringList rootLines;
    QStringList bodyLines;
    if (!text.isEmpty()) {
        const QStringList rows = wrapTextIntoRows(text, splitter, w);
        for (int row = 0; row < h && row < rows.size(); ++row) {
            const QString& rowText = rows.at(row);
            if (rowText.isEmpty()) break;
            const int slack = w - rowText.length();
            const int startCol = (align == TextAlign::Left)    ? 0
                                  : (align == TextAlign::Right) ? slack
                                                                 : slack / 2;

            QString sourceArgs;  // the (source, count) args to WriteFromBufferToNameTable
            if (charmapFn.isEmpty()) {
                const QString rowVar = QString("row%1").arg(row);
                bodyLines << QString("    static const char %1[] = \"%2\";")
                                 .arg(rowVar, cStringEscape(rowText));
                sourceArgs = QString("reinterpret_cast<const u8*>(%1), sizeof(%1) - 1").arg(rowVar);
            } else {
                const QString dataName = QString("row%1").arg(row);
                rootLines << QString("%1inline constexpr auto %2 = %3.encode(\"%4\");")
                                 .arg(dataPrefixTok, dataName, charmapFn, cStringEscape(rowText));
                sourceArgs = QString("SIZED_STR(%1)").arg(dataName);
            }

            if (isNes) {
                bodyLines << QString("    ppu::WriteFromBufferToNameTable(0x%1, %2, 0);")
                                 .arg(static_cast<uint>(nesNtAddr(x + startCol, y + row)), 4, 16, QLatin1Char('0'))
                                 .arg(sourceArgs);
            } else if (variadic) {
                const QString xExpr = emitGeomComponent(item, 0, true, rootItem, target, geom);
                const QString yExpr = emitGeomComponent(item, 1, true, rootItem, target, geom);
                QString xFinal = startCol ? QString("(%1 + %2)").arg(xExpr).arg(startCol) : xExpr;
                QString yFinal = row ? QString("(%1 + %2)").arg(yExpr).arg(row) : yExpr;
                if (ntOffXExpr != QLatin1String("0")) xFinal = QString("(%1 + %2)").arg(xFinal, ntOffXExpr);
                if (ntOffYExpr != QLatin1String("0")) yFinal = QString("(%1 + %2)").arg(yFinal, ntOffYExpr);
                bodyLines << QString("    ppu::WriteFromBufferToNameTable("
                                      "vec2<u16>{static_cast<u16>(%1), static_cast<u16>(%2)}, %3, 0);")
                                 .arg(xFinal, yFinal, sourceArgs);
            } else {
                bodyLines << QString("    ppu::WriteFromBufferToNameTable(vec2<u16>{%1, %2}, %3, 0);")
                                 .arg(x + startCol)
                                 .arg(y + row)
                                 .arg(sourceArgs);
            }
        }
    }
    return {rootLines.join("\n"), bodyLines.join("\n")};
}

QString genCtTextBoxEraseBody(const QTreeWidgetItem* item, int ntOffX, int ntOffY, bool isNes, bool variadic,
                               QTreeWidgetItem* rootItem, const QString& target,
                               const QString& charmapFn, const QString& ntOffXExpr, const QString& ntOffYExpr,
                               const TargetGeometry& geom) {
    const int x = geom.x(item) + ntOffX;
    const int y = geom.y(item) + ntOffY;
    const int w = geom.w(item);
    const int h = geom.h(item);
    const QString tileExpr = charmapFn.isEmpty() ? QStringLiteral("' '") : QStringLiteral("blank");

    QStringList bodyLines;
    if (!charmapFn.isEmpty()) {
        bodyLines << QString("    constexpr auto blank = %1(' ');").arg(charmapFn);
    }
    for (int row = 0; row < h; ++row) {
        if (isNes) {
            bodyLines << QString("    ppu::WriteRepeatedToNameTable(0x%1, %2, %3, 0);")
                             .arg(static_cast<uint>(nesNtAddr(x, y + row)), 4, 16, QLatin1Char('0'))
                             .arg(tileExpr)
                             .arg(w);
        } else if (variadic) {
            const QString xExpr = emitGeomComponent(item, 0, true, rootItem, target, geom);
            const QString yExpr = emitGeomComponent(item, 1, true, rootItem, target, geom);
            QString xFinal = xExpr;
            QString yFinal = row ? QString("(%1 + %2)").arg(yExpr).arg(row) : yExpr;
            if (ntOffXExpr != QLatin1String("0")) xFinal = QString("(%1 + %2)").arg(xFinal, ntOffXExpr);
            if (ntOffYExpr != QLatin1String("0")) yFinal = QString("(%1 + %2)").arg(yFinal, ntOffYExpr);
            bodyLines << QString("    ppu::WriteRepeatedToNameTable("
                                  "vec2<u16>{static_cast<u16>(%1), static_cast<u16>(%2)}, %3, %4, 0);")
                             .arg(xFinal, yFinal, tileExpr)
                             .arg(w);
        } else {
            bodyLines << QString("    ppu::WriteRepeatedToNameTable(vec2<u16>{%1, %2}, %3, %4, 0);")
                             .arg(x)
                             .arg(y + row)
                             .arg(tileExpr)
                             .arg(w);
        }
    }
    return bodyLines.join("\n");
}

QStringList childNamespaceNames(const QTreeWidgetItem* item, const QString& target) {
    QStringList names;
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* child = item->child(i);
        if (!isPrefixedItem(child)) {
            names += childNamespaceNames(child, target);  // transparent branch
            continue;
        }
        if (isEffectivelyHidden(child, target)) continue;
        if (isSingleChoiceItem(child) && singleChoiceMembers(child, target).isEmpty()) continue;
        names << bareName(child);
    }
    return names;
}

QStringList genGeometryStorage(const QTreeWidgetItem* item, int ntOffX, int ntOffY, bool isNes, bool variadic,
                                bool runtimePos, QTreeWidgetItem* rootItem, const QString& target,
                                const QString& ntOffXExpr, const QString& ntOffYExpr,
                                const TargetGeometry& geom,
                                const QStringList& extraMakeSeeds = QStringList()) {
    const int x = geom.x(item) + ntOffX;
    const int y = geom.y(item) + ntOffY;
    const int w = geom.w(item);
    const int h = geom.h(item);

    QString xCpp = QString::number(x);
    QString yCpp = QString::number(y);
    QString wCpp = QString::number(w);
    QString hCpp = QString::number(h);
    if (variadic) {
        xCpp = emitGeomComponent(item, 0, true, rootItem, target, geom);
        yCpp = emitGeomComponent(item, 1, true, rootItem, target, geom);
        if (ntOffXExpr != QLatin1String("0")) xCpp = QString("(%1 + %2)").arg(xCpp, ntOffXExpr);
        if (ntOffYExpr != QLatin1String("0")) yCpp = QString("(%1 + %2)").arg(yCpp, ntOffYExpr);
        xCpp = QString("static_cast<u16>(%1)").arg(xCpp);
        yCpp = QString("static_cast<u16>(%1)").arg(yCpp);
        const QString wExpr = emitGeomComponent(item, 2, true, rootItem, target, geom);
        const QString hExpr = emitGeomComponent(item, 3, true, rootItem, target, geom);
        if (wExpr.contains(QLatin1String("video::"))) wCpp = QString("static_cast<u8>(%1)").arg(wExpr);
        if (hExpr.contains(QLatin1String("video::"))) hCpp = QString("static_cast<u8>(%1)").arg(hExpr);
    }
    const QString rectLiteral = QString("Rect{{%1, %2}, {%3, %4}}").arg(xCpp, yCpp, wCpp, hCpp);

    QStringList childMakes;
    for (const QString& kid : childNamespaceNames(item, target)) {
        childMakes << QString("    %1::Make();").arg(kid);
    }
    QStringList makeTail = childMakes;
    makeTail += extraMakeSeeds;
    const QString childTail = makeTail.isEmpty() ? QString() : ("\n" + makeTail.join("\n"));

    QStringList lines;
    if (runtimePos) {
        lines << QString("inline atomic Rect rect;");
        lines << QString("inline atomic u16 pos;");
        lines << QString("inline AI void Make() {\n"
                          "    const Rect r = %1;\n"
                          "    rect = r;\n"
                          "    pos = ppu::CartesianToAddress(r.pos);%2\n"
                          "}")
                     .arg(rectLiteral, childTail);
        return lines;
    }

    lines << QString("inline constexpr Rect rect = %1;").arg(rectLiteral);
    if (isNes) {
        lines << QString("inline constexpr u16 pos = 0x%1;")
                     .arg(static_cast<uint>(nesNtAddr(x, y)), 4, 16, QLatin1Char('0'));
        lines << (makeTail.isEmpty() ? QString("inline AI void Make() {}")
                                      : QString("inline AI void Make() {\n%1\n}").arg(makeTail.join("\n")));
    } else {
        lines << QString("inline atomic u16 pos;");
        lines << (makeTail.isEmpty()
                      ? QString("inline AI void Make() { pos = ppu::CartesianToAddress(rect.pos); }")
                      : QString("inline AI void Make() {\n"
                                 "    pos = ppu::CartesianToAddress(rect.pos);%1\n}")
                            .arg(childTail));
    }
    return lines;
}

QString genSingleChoiceDecl(QTreeWidgetItem* scItem, QTreeWidgetItem* rootItem, const QString& target,
                             const QString& bssPrefixTok, const QString& dataPrefixTok, const BssSplit& bss,
                             bool runtimePos) {
    const QVector<QTreeWidgetItem*> members = singleChoiceMembers(scItem, target);
    if (members.isEmpty()) {
        return QString();
    }
    const int nOptions = members.size();
    const int defaultOption = std::clamp(scItem->data(0, kDefaultOptionRole).toInt(), 0, nOptions - 1);
    const bool scVertical = scItem->data(0, kSingleChoiceVerticalRole).toBool();

    QStringList lines;

    if (runtimePos) {
        lines << QString("%1inline atomic Rect options[%2];").arg(bssPrefixTok).arg(nOptions);
    } else {
        QStringList entries;
        for (QTreeWidgetItem* member : members) {
            entries << QString("%1::rect").arg(bareName(member));
        }
        lines << QString("%1inline constexpr Rect options[%2] = {%3};")
                     .arg(dataPrefixTok)
                     .arg(nOptions)
                     .arg(entries.join(", "));
    }

    lines << bss.decl(bssPrefixTok, QStringLiteral("atomic u8"), QStringLiteral("option"),
                       QString::number(defaultOption));
    lines << QString("inline constexpr u8 nOptions = %1;").arg(nOptions);

    lines << QString("inline constexpr bool vertical = %1;")
                 .arg(scVertical ? QLatin1String("true") : QLatin1String("false"));

    const bool wantRect =
        scItem->data(0, kSingleChoiceHasRectRole).toBool() || isCanvasComponent(scItem, target);
    const QString boundsFold =
        QString("    u16 x0 = options[0].pos.x;\n"
                 "    u16 y0 = options[0].pos.y;\n"
                 "    u16 x1 = static_cast<u16>(options[0].pos.x + options[0].size.x);\n"
                 "    u16 y1 = static_cast<u16>(options[0].pos.y + options[0].size.y);\n"
                 "    for (u8 i = 1; i < nOptions; ++i) {\n"
                 "        const Rect r = options[i];\n"
                 "        if (r.pos.x < x0) x0 = r.pos.x;\n"
                 "        if (r.pos.y < y0) y0 = r.pos.y;\n"
                 "        if (static_cast<u16>(r.pos.x + r.size.x) > x1) x1 = static_cast<u16>(r.pos.x + r.size.x);\n"
                 "        if (static_cast<u16>(r.pos.y + r.size.y) > y1) y1 = static_cast<u16>(r.pos.y + r.size.y);\n"
                 "    }\n");
    const QString boundsValue = QString("Rect{{x0, y0}, {static_cast<u8>(x1 - x0), static_cast<u8>(y1 - y0)}}");
    if (wantRect) {
        if (runtimePos) {
            lines << QString("%1inline atomic Rect rect;").arg(bssPrefixTok);
        } else {
            lines << QString("%1inline constexpr Rect rect = [] {\n%2    return %3;\n}();")
                         .arg(dataPrefixTok, boundsFold, boundsValue);
        }
    }

    QStringList makeBody;
    for (const QString& kid : childNamespaceNames(scItem, target)) {
        makeBody << QString("    %1::Make();").arg(kid);
    }
    if (runtimePos) {
        for (int i = 0; i < members.size(); ++i) {
            makeBody << QString("    { const Rect r = %1::rect; options[%2] = r; }")
                            .arg(bareName(members[i]))
                            .arg(i);
        }
        if (wantRect) {
            QString nestedFold = boundsFold;
            nestedFold.replace(QLatin1String("\n    "), QLatin1String("\n        "));
            makeBody << QString("    {\n    %1        rect = %2;\n    }").arg(nestedFold, boundsValue);
        }
    }
    makeBody << QString("    option = %1;").arg(defaultOption);

    lines << QString("inline AI void Make() {\n%1\n}").arg(makeBody.join("\n"));

    lines << QString("void DrawChange(u8 from, u8 to);");

    const QString scBack = scVertical ? QStringLiteral("input::UP") : QStringLiteral("input::LEFT");
    const QString scFwd = scVertical ? QStringLiteral("input::DOWN") : QStringLiteral("input::RIGHT");
    lines << QString("inline AI void Pass(const u8 inputs) {\n"
                      "    const u8 from = option;\n"
                      "\n"
                      "    if (inputs & %1) {\n"
                      "        if (from == 0) return;\n"
                      "        const u8 to = static_cast<u8>(from - 1);\n"
                      "        option = to;\n"
                      "        DrawChange(from, to);\n"
                      "        return;\n"
                      "    }\n"
                      "\n"
                      "    if (inputs & %2) {\n"
                      "        if (from == nOptions - 1) return;\n"
                      "        const u8 to = static_cast<u8>(from + 1);\n"
                      "        option = to;\n"
                      "        DrawChange(from, to);\n"
                      "        return;\n"
                      "    }\n"
                      "}")
                 .arg(scBack, scFwd);

    lines << (runtimePos
                  ? QString("inline AI bool Click(const vec2<u16> at) {\n"
                             "    const u8 from = option;\n"
                             "\n"
                             "    for (u8 i = 0; i < nOptions; i++) {\n"
                             "        const Rect r = options[i];\n"
                             "        if (!r.Contains(at)) continue;\n"
                             "        if (i == from) return true;\n"
                             "        option = i;\n"
                             "        DrawChange(from, i);\n"
                             "        return true;\n"
                             "    }\n"
                             "\n"
                             "    return false;\n"
                             "}")
                  : QString("inline AI bool Click(const vec2<u16> at) {\n"
                             "    const u8 from = option;\n"
                             "\n"
                             "    for (u8 i = 0; i < nOptions; i++) {\n"
                             "        if (!options[i].Contains(at)) continue;\n"
                             "        if (i == from) return true;\n"
                             "        option = i;\n"
                             "        DrawChange(from, i);\n"
                             "        return true;\n"
                             "    }\n"
                             "\n"
                             "    return false;\n"
                             "}"));

    bool allDrawable = true;
    bool allErasable = true;
    for (QTreeWidgetItem* member : members) {
        if (!isCtTextBoxItem(member)) {
            allDrawable = false;
            allErasable = false;
            break;
        }
        if (!member->data(0, kProvideErasingRole).toBool()) allErasable = false;
    }
    if (allDrawable) {
        QStringList drawBody;
        QStringList eraseBody;
        for (QTreeWidgetItem* member : members) {
            drawBody << QString("    %1::Draw();").arg(bareName(member));
            eraseBody << QString("    %1::Erase();").arg(bareName(member));
        }
        lines << QString("inline AI void Draw() {\n%1\n}").arg(drawBody.join("\n"));
        if (allErasable) {
            lines << QString("inline AI void Erase() {\n%1\n}").arg(eraseBody.join("\n"));
        }
    }

    return lines.join("\n") + "\n";
}


// Emits one Button node's declarations: geometry, the Enable/Disable (and opt-in
// Toggle) that write its enabled bit, the Enabled() that reads it, and Pass().
// uitk provides no appearance -- a button could be a 1x1 tickbox or a sprite --
// so Draw() is a hook hand-written code must supply, and enabled/disabled is all
// the memory this toolkit offers.
//
// ONE Draw(), not the DrawEnabled/DrawDisabled pair this used to emit with a
// generated dispatcher: a button's two states are two appearances of one widget,
// and nearly every implementation of the pair shared a body differing by one
// tile. The branch belongs inside the hook, where it is written once and stays
// out of every call site.
//
// A missing Draw() is caught at link time, and only once something calls it --
// the closest to "forced by compile" this codegen can offer.
//
// The enabled bit is never its own variable: every button shares one
// `button_state` array, sized to fit the scene, so eight buttons cost one byte
// between them where eight `atomic bool`s cost eight.
QString genButtonDecl(const QTreeWidgetItem* item, int ntOffX, int ntOffY, bool isNes, bool variadic,
                       bool runtimePos, QTreeWidgetItem* rootItem, const QString& target,
                       const QString& ntOffXExpr, const QString& ntOffYExpr, int bitIndex,
                       const QString& stateArrayName, const TargetGeometry& geom, const BssSplit& bss) {
    const bool toggleable = item->data(0, kButtonToggleableRole).toBool();
    const bool defaultEnabled = item->data(0, kButtonDefaultEnabledRole).toBool();
    const int byteIndex = bitIndex / 8;
    const int bitInByte = bitIndex % 8;
    const QString byteExpr = QString("%1[%2]").arg(stateArrayName).arg(byteIndex);
    const QString maskExpr = QString("(1u << %1)").arg(bitInByte);

    QStringList stateSeed;
    if (bss.enabled) {
        stateSeed << (defaultEnabled ? QString("    %1 |= %2;").arg(byteExpr, maskExpr)
                                      : QString("    %1 &= static_cast<u8>(~%2);").arg(byteExpr, maskExpr));
    }

    QStringList lines =
        genGeometryStorage(item, ntOffX, ntOffY, isNes, variadic, runtimePos, rootItem, target,
                            ntOffXExpr, ntOffYExpr, geom, stateSeed);

    lines << QString("inline AI void Enable() { %1 |= %2; }").arg(byteExpr, maskExpr);
    lines << QString("inline AI void Disable() { %1 &= static_cast<u8>(~%2); }").arg(byteExpr, maskExpr);
    if (toggleable) {
        lines << QString("inline AI bool Toggle() {\n"
                          "    const u8 next = static_cast<u8>(%1 ^ %2);\n"
                          "    %1 = next;\n"
                          "    return next & %2;\n"
                          "}")
                     .arg(byteExpr, maskExpr);
    }
    lines << QString("inline AI bool Enabled() { return %1 & %2; }").arg(byteExpr, maskExpr);

    lines << QString("void Draw();");

    if (toggleable) {
        lines << QString("inline AI void Pass(const u8 payload) {\n"
                          "    const u8 wanted = payload & static_cast<u8>(~(input::UP | input::DOWN |\n"
                          "        input::LEFT | input::RIGHT\n"
                          "    ));\n"
                          "\n"
                          "    if (!wanted) return;\n"
                          "    Toggle();\n"
                          "    Draw();\n"
                          "}");

        lines << (runtimePos
                      ? QString("inline AI bool Click(const vec2<u16> at) {\n"
                                 "    const Rect r = rect;\n"
                                 "    if (!r.Contains(at)) return false;\n"
                                 "    Toggle();\n"
                                 "    Draw();\n"
                                 "    return true;\n"
                                 "}")
                      : QString("inline AI bool Click(const vec2<u16> at) {\n"
                                 "    if (!rect.Contains(at)) return false;\n"
                                 "    Toggle();\n"
                                 "    Draw();\n"
                                 "    return true;\n"
                                 "}"));
    }

    return lines.join("\n") + "\n";
}


// Emits one Slider node's declarations: geometry, the `amount` byte, the two
// forced-to-define hooks (Draw() for initial placement, DrawChange(u8) for
// moving one already on screen) and the generated Pass() that turns a frame's
// input into a DrawChange.
//
// No Enable/Disable/Toggle and no enabled pair: a slider is not a two-state
// thing. `amount` is storage and nothing else -- uitk allocates it and never
// generates code that reads or writes it, since a slider's range, step and
// clamping belong to what it controls. Its footprint is never optional: the
// track length IS the range.
QString genSliderDecl(const QTreeWidgetItem* item, int ntOffX, int ntOffY, bool isNes, bool variadic,
                       bool runtimePos, QTreeWidgetItem* rootItem, const QString& target,
                       const QString& bssPrefixTok, const QString& ntOffXExpr, const QString& ntOffYExpr,
                       const TargetGeometry& geom, const BssSplit& bss) {
    const bool vertical = item->data(0, kSliderVerticalRole).toBool();

    const QString amountDecl =
        bss.decl(bssPrefixTok, QStringLiteral("atomic u8"), QStringLiteral("amount"), QStringLiteral("0"));
    QStringList lines;
    if (bss.enabled) lines << amountDecl;
    lines += genGeometryStorage(item, ntOffX, ntOffY, isNes, variadic, runtimePos, rootItem, target,
                                 ntOffXExpr, ntOffYExpr, geom);

    if (!bss.enabled) lines << amountDecl;

    lines << QString("inline constexpr bool vertical = %1;")
                 .arg(vertical ? QLatin1String("true") : QLatin1String("false"));

    lines << QString("void Draw();");
    lines << QString("void DrawChange(u8 amt);");

    lines << QString("void DrawClick(u8 cell);");

    const QString slBack = vertical ? QStringLiteral("input::UP") : QStringLiteral("input::LEFT");
    const QString slFwd = vertical ? QStringLiteral("input::DOWN") : QStringLiteral("input::RIGHT");
    lines << QString("inline AI void Pass(const u8 payload) {\n"
                      "    const u8 cur = amount;\n"
                      "\n"
                      "    if      (payload & %1) DrawChange(static_cast<u8>(cur + 1));\n"
                      "    else if (payload & %2) DrawChange(static_cast<u8>(cur - 1));\n"
                      "}")
                 .arg(slFwd, slBack);

    const QString slAxis = vertical ? QStringLiteral("y") : QStringLiteral("x");
    lines << (runtimePos
                  ? QString("inline AI bool Click(const vec2<u16> at) {\n"
                             "    const Rect r = rect;\n"
                             "    if (!r.Contains(at)) return false;\n"
                             "    DrawClick(static_cast<u8>(at.%1 - r.pos.%1));\n"
                             "    return true;\n"
                             "}").arg(slAxis)
                  : QString("inline AI bool Click(const vec2<u16> at) {\n"
                             "    if (!rect.Contains(at)) return false;\n"
                             "    DrawClick(static_cast<u8>(at.%1 - rect.pos.%1));\n"
                             "    return true;\n"
                             "}").arg(slAxis));

    return lines.join("\n") + "\n";
}


bool componentHasDraw(QTreeWidgetItem* item, const QString& target) {
    if (isSingleChoiceItem(item)) {
        const QVector<QTreeWidgetItem*> members = singleChoiceMembers(item, target);
        if (members.isEmpty()) return false;
        for (QTreeWidgetItem* member : members) {
            if (!isCtTextBoxItem(member)) return false;
        }
        return true;
    }
    return isSliderItem(item) || isButtonItem(item);
}

QString genCanvasDecl(QTreeWidgetItem* canvasItem, const QString& target,
                       const QString& bssPrefixTok) {
    const QVector<QTreeWidgetItem*> comps = canvasComponents(canvasItem, target);
    const int size = comps.size();
    const bool vertical = canvasItem->data(0, kCanvasVerticalRole).toBool();

    QStringList lines;

    if (size > 0) {
        lines << QString("inline constexpr u8 size = %1;").arg(size);
        QStringList entries;
        for (QTreeWidgetItem* comp : comps) {
            entries << QString("    {&%1::rect, &%1::Pass},").arg(bareName(comp));
        }
        lines << QString("inline constexpr InvokeableComponant components[size] {\n%1\n};")
                     .arg(entries.join("\n"));

        lines << QString("%1inline atomic u8 index = 0;").arg(bssPrefixTok);

        lines << QString("inline constexpr bool vertical = %1;")
                     .arg(vertical ? QLatin1String("true") : QLatin1String("false"));

        lines << QString("void DrawUpdate(const Rect& last, const Rect& current);");

        lines << QString("void DrawComponentSelection(const Rect& current);");
    }

    QStringList makeBody;
    for (const QString& kid : childNamespaceNames(canvasItem, target)) {
        makeBody << QString("    %1::Make();").arg(kid);
    }
    if (size > 0) makeBody << QString("    index = 0;");
    lines << (makeBody.isEmpty() ? QString("inline AI void Make() {}")
                                  : QString("inline AI void Make() {\n%1\n}").arg(makeBody.join("\n")));

    if (size == 0) {
        return lines.join("\n") + "\n";
    }

    bool allComponentsDrawable = true;
    for (QTreeWidgetItem* comp : comps) {
        if (!componentHasDraw(comp, target)) {
            allComponentsDrawable = false;
            break;
        }
    }
    if (allComponentsDrawable) {
        QStringList drawBody;
        for (QTreeWidgetItem* comp : comps) {
            drawBody << QString("    %1::Draw();").arg(bareName(comp));
        }
        lines << QString("inline AI void DrawComponents() {\n%1\n}").arg(drawBody.join("\n"));
    }

    if (size == 1) {
        lines << QString("inline AI void Pass(const u8 payload) {\n"
                          "    components[0].func(payload);\n"
                          "}");

        lines << QString("inline AI bool PassClick(const vec2<u16> at) {\n"
                          "    return %1::Click(at);\n"
                          "}")
                     .arg(bareName(comps[0]));
        return lines.join("\n") + "\n";
    }

    const QString back = vertical ? QStringLiteral("input::UP") : QStringLiteral("input::LEFT");
    const QString fwd = vertical ? QStringLiteral("input::DOWN") : QStringLiteral("input::RIGHT");
    lines << QString("inline AI void Pass(const u8 payload) {\n"
                      "    const u8 from = index;\n"
                      "\n"
                      "    if (payload & %1) {\n"
                      "        if (from == 0) return;\n"
                      "        index = static_cast<u8>(from - 1);\n"
                      "        const Rect last = *components[from].rect;\n"
                      "        const Rect current = *components[from - 1].rect;\n"
                      "        DrawUpdate(last, current);\n"
                      "        return;\n"
                      "    }\n"
                      "\n"
                      "    if (payload & %2) {\n"
                      "        if (from == size - 1) return;\n"
                      "        index = static_cast<u8>(from + 1);\n"
                      "        const Rect last = *components[from].rect;\n"
                      "        const Rect current = *components[from + 1].rect;\n"
                      "        DrawUpdate(last, current);\n"
                      "        return;\n"
                      "    }\n"
                      "\n"
                      "    components[from].func(payload);\n"
                      "}")
                 .arg(back, fwd);

    lines << QString("inline AI void Focus(const u8 to) {\n"
                      "    const u8 from = index;\n"
                      "    if (from == to) return;\n"
                      "\n"
                      "    index = to;\n"
                      "    const Rect last = *components[from].rect;\n"
                      "    const Rect current = *components[to].rect;\n"
                      "    DrawUpdate(last, current);\n"
                      "}");

    {
        QStringList clickBody;
        for (int i = 0; i < comps.size(); ++i) {
            clickBody << QString("    if (%1::Click(at)) { Focus(%2); return true; }")
                             .arg(bareName(comps[i]))
                             .arg(i);
        }
        lines << QString("inline AI bool PassClick(const vec2<u16> at) {\n%1\n\n    return false;\n}")
                     .arg(clickBody.join("\n"));
    }

    return lines.join("\n") + "\n";
}

GeneratedFiles generateCode(QTreeWidgetItem* rootItem, const QString& target, int quadrant,
                             const QString& linkerPrefix, const QString& bssPrefix, const QString& dataPrefix,
                             const QString& charmap, const QString& specialInclude, bool bssSeparate,
                             const QString& bssSymbolPrefix, const QString& sceneName) {
    const TargetGeometry geom = resolveGeometryForTarget(rootItem, target);

    const int ntOffX = (quadrant & 1) * kNametableQuadW;
    const int ntOffY = ((quadrant >> 1) & 1) * kNametableQuadH;
    const bool isNes = (target == QLatin1String("NES"));
    const bool variadic = kVariadicTargets.contains(target);
    const bool runtimePos = kRuntimePosTargets.contains(target);
    const QString ntOffXExpr = !(quadrant & 1)             ? QStringLiteral("0")
                                : variadic                    ? QStringLiteral("(video::viewport_tx() < 32 ? 32 : video::viewport_tx())")
                                                               : QString::number(kNametableQuadW);
    const QString ntOffYExpr = !((quadrant >> 1) & 1)      ? QStringLiteral("0")
                                : variadic                    ? QStringLiteral("(video::viewport_ty() < 30 ? 30 : video::viewport_ty())")
                                                               : QString::number(kNametableQuadH);
    const QString bssPrefixTok = (isNes && !bssPrefix.trimmed().isEmpty()) ? (bssPrefix.trimmed() + " ") : QString();
    const QString dataPrefixTok =
        (isNes && !dataPrefix.trimmed().isEmpty()) ? (dataPrefix.trimmed() + " ") : QString();
    const QString charmapTrimmed = charmap.trimmed();
    const QString defaultCharmapFn = charmapTrimmed.isEmpty() ? QString() : ("charmap_" + charmapTrimmed);
    auto nodeCharmapFn = [&defaultCharmapFn](const QTreeWidgetItem* item) -> QString {
        const QString override_ = item->data(0, kCharmapOverrideRole).toString().trimmed();
        return override_.isEmpty() ? defaultCharmapFn : ("charmap_" + override_);
    };

    QStringList bssDecls;
    BssSplit bss;
    bss.enabled = bssSeparate;
    bss.symbolPrefix = bssSymbolPrefix.trimmed();
    bss.decls = &bssDecls;
    bss.path = sceneName;

    QStringList hppDecls;
    QStringList cppDefs;
    bool usesInput = false;
    QSet<QString> usedCharmapFns;

    static const QString kButtonStateArrayName = QStringLiteral("button_state");
    QVector<QTreeWidgetItem*> visibleButtons;
    for (QTreeWidgetItem* item : collectComponentItems(rootItem)) {
        if (isButtonItem(item) && !isEffectivelyHidden(item, target)) {
            visibleButtons.push_back(item);
        }
    }
    QHash<const QTreeWidgetItem*, int> buttonBitIndex;
    for (int i = 0; i < visibleButtons.size(); ++i) {
        buttonBitIndex[visibleButtons[i]] = i;
    }
    if (!visibleButtons.isEmpty()) {
        const int byteCount = (visibleButtons.size() + 7) / 8;
        QVector<unsigned> byteDefaults(byteCount, 0u);
        for (int i = 0; i < visibleButtons.size(); ++i) {
            if (visibleButtons[i]->data(0, kButtonDefaultEnabledRole).toBool()) {
                byteDefaults[i / 8] |= (1u << (i % 8));
            }
        }
        QStringList byteLiterals;
        for (unsigned b : byteDefaults) {
            byteLiterals << QString("0x%1").arg(b, 2, 16, QLatin1Char('0'));
        }
        hppDecls << QString("// Every Button's enabled/disabled bit, packed into the fewest bytes that fit\n"
                             "// them all -- one bit per button, in the order they appear in the scene.\n"
                             "%1\n")
                        .arg(bss.decl(QString(), QStringLiteral("atomic u8"), kButtonStateArrayName,
                                       "{" + byteLiterals.join(", ") + "}", byteCount));
    }

    QStringList makeAll;
    std::function<QStringList(QTreeWidgetItem*, const QString&, const BssSplit&)> emitNodeNamespace =
        [&](QTreeWidgetItem* node, const QString& qualPrefix, const BssSplit& nodeBss) -> QStringList {
        QStringList emitted;
        for (int i = 0; i < node->childCount(); ++i) {
            QTreeWidgetItem* child = node->child(i);
            if (!isPrefixedItem(child)) {
                emitted += emitNodeNamespace(child, qualPrefix, nodeBss);  // transparent branch
                continue;
            }
            if (isEffectivelyHidden(child, target)) continue;

            const QString name = bareName(child);
            const QString qual = qualPrefix.isEmpty() ? name : (qualPrefix + "::" + name);
            const BssSplit childBss = nodeBss.at(name);

            QStringList body = emitNodeNamespace(child, qual, childBss);

            if (isButtonItem(child)) {
                body << genButtonDecl(child, ntOffX, ntOffY, isNes, variadic, runtimePos, rootItem, target,
                                       ntOffXExpr, ntOffYExpr, buttonBitIndex.value(child), kButtonStateArrayName,
                                       geom, childBss);
                if (child->data(0, kButtonToggleableRole).toBool()) usesInput = true;
                makeAll << qual;
            } else if (isSliderItem(child)) {
                body << genSliderDecl(child, ntOffX, ntOffY, isNes, variadic, runtimePos, rootItem, target,
                                       bssPrefixTok, ntOffXExpr, ntOffYExpr, geom, childBss);
                usesInput = true;
                makeAll << qual;
            } else if (isSingleChoiceItem(child)) {
                const QString decl =
                    genSingleChoiceDecl(child, rootItem, target, bssPrefixTok, dataPrefixTok, childBss,
                                         runtimePos);
                if (decl.isEmpty()) continue;  // no visible options -- nothing to construct
                body << decl;
                usesInput = true;
                makeAll << qual;
            } else if (isCanvasItem(child)) {
                body << genCanvasDecl(child, target, bssPrefixTok);
                if (canvasComponents(child, target).size() > 1) usesInput = true;
                makeAll << qual;
            } else {
                QStringList own = genGeometryStorage(child, ntOffX, ntOffY, isNes, variadic, runtimePos, rootItem,
                                                      target, ntOffXExpr, ntOffYExpr, geom);
                makeAll << qual;

                const QString charmapFn = nodeCharmapFn(child);
                if (!charmapFn.isEmpty() && isComponentItem(child)) usedCharmapFns.insert(charmapFn);

                if (isCtTextBoxItem(child)) {
                    const CtTextBoxGen gen = genCtTextBoxBody(child, ntOffX, ntOffY, isNes, variadic, rootItem,
                                                                target, dataPrefixTok, charmapFn,
                                                                ntOffXExpr, ntOffYExpr, geom);
                    if (!gen.rootDecls.isEmpty()) own << gen.rootDecls;
                    own << QString("inline AI void Draw() {\n%1\n}").arg(gen.body);

                    if (child->data(0, kProvideErasingRole).toBool()) {
                        const QString eraseBody = genCtTextBoxEraseBody(child, ntOffX, ntOffY, isNes, variadic,
                                                                          rootItem, target, charmapFn,
                                                                          ntOffXExpr, ntOffYExpr, geom);
                        own << QString("inline AI void Erase() {\n%1\n}").arg(eraseBody);
                    }
                } else if (isRtTextBoxItem(child)) {
                    const int w = geom.w(child);
                    const int h = geom.h(child);
                    const QString splitterStr = child->data(0, kSplitterRole).toString();
                    const QChar splitter = splitterStr.isEmpty() ? QLatin1Char(' ') : splitterStr.at(0);
                    const QString mapExpr = charmapFn.isEmpty() ? QStringLiteral("ui::text::ascii") : charmapFn;
                    const QString callExpr = QString("ui::text::Make<%1, '%2'>(buff, vec2<u8>{%3, %4})")
                                                 .arg(mapExpr, cCharEscape(splitter)).arg(w).arg(h);
                    own << QString("inline AI ui::text::textBuffer* MakeText(const u8* buff) {\n"
                                    "    return %1;\n"
                                    "}")
                                 .arg(callExpr);
                }
                body << own.join("\n\n");
            }

            emitted << QString("namespace %1 {\n\n%2\n}  // namespace %1\n").arg(name, body.join("\n\n"));
        }
        return emitted;
    };

    hppDecls += emitNodeNamespace(rootItem, QString(), bss);

    if (!makeAll.isEmpty()) {
        QStringList callLines;
        for (const QString& qual : makeAll) callLines << QString("    %1::Make();").arg(qual);
        hppDecls << QString("// Resolves every node's geometry and seeds every menu's running state.\n"
                             "// On a runtime-viewport target nothing may read a node's rect/pos until\n"
                             "// this (or that node's own Make()) has run; on a fixed one it is still\n"
                             "// emitted, so no call site needs a #if to decide whether it exists.\n"
                             "inline AI void Make() {\n%1\n}")
                        .arg(callLines.join("\n"));
    }

    QStringList hppIncludes{"#include <intsh>", "#include <pnes/types.hpp>", "#include <pnes/video.hpp>",
                             "#include <pnes/extras/ui/text.hpp>", "#include <pnes/extras/ui/types.hpp>"};
    if (usesInput) {
        hppIncludes << "#include <pnes/input.hpp>";
    }
    if (!specialInclude.trimmed().isEmpty()) {
        hppIncludes << specialInclude.trimmed();
    }

    QString charmapNote;
    if (!usedCharmapFns.isEmpty()) {
        QStringList sorted(usedCharmapFns.begin(), usedCharmapFns.end());
        sorted.sort();
        charmapNote = QString("\n// NOTE: %1 must already be in scope here.\n").arg(sorted.join(", "));
    }

    QSet<QString> usedSegmentMacros;
    if (isNes) {
        for (const QString& p : {linkerPrefix.trimmed(), bssPrefix.trimmed(), dataPrefix.trimmed()}) {
            if (!p.isEmpty()) usedSegmentMacros.insert(p);
        }
    }
    QString segmentGuards;
    if (!usedSegmentMacros.isEmpty()) {
        QStringList sorted(usedSegmentMacros.begin(), usedSegmentMacros.end());
        sorted.sort();
        QStringList guardLines;
        for (const QString& macroName : sorted) {
            guardLines << QString("#ifndef %1\n"
                                   "#error \"%1 is not set -- add it to local.cmake.\"\n"
                                   "#endif")
                            .arg(macroName);
        }
        segmentGuards = "\n" + guardLines.join("\n") + "\n";
    }
    const QString requirementsNote = charmapNote;

    GeneratedFiles out;
    out.hpp = QString("#pragma once\n\n"
                       "// Generated by uitk from \"%1\" -- do not edit by hand.\n"
                       "%4"
                       "\n%2\n"
                       "%5"
                       "\nusing namespace br0::intsh;\n\n"
                       "namespace gen::%1 {\n\n"
                       "%3"
                       "\n}  // namespace gen::%1\n")
                   .arg(sceneName, hppIncludes.join("\n"), hppDecls.join("\n\n"), requirementsNote, segmentGuards);
    out.cpp = QString("// Generated by uitk from \"%1\" -- do not edit by hand.\n\n"
                       "#include \"%2.hpp\"\n\n"
                       "namespace gen::%1 {\n\n"
                       "%3"
                       "\n}  // namespace gen::%1\n")
                  .arg(sceneName, sceneName, cppDefs.join("\n"));
    out.cppHasContent = !cppDefs.isEmpty();
    if (bssSeparate && !bssDecls.isEmpty()) {
        out.bss = QString("// Generated by uitk from \"%1\" -- do not edit by hand.\n"
                           "// %2's mutable state, as plain declarations: no guards and no namespace,\n"
                           "// to be included wherever the project wants this storage to live. The\n"
                           "// generated header reaches it back through \"%3\".\n"
                           "//\n"
                           "// DO NOT STRIP THE `atomic`. Where this gets pasted into a save struct in\n"
                           "// memory the startup code does not clear (a battery-backed NOLOAD section\n"
                           "// on the NES), C++'s zero-initialization promise is one LLVM still makes\n"
                           "// and acts on: at LTO -- llvm-mos's default -- GlobalOpt field-splits such\n"
                           "// a struct and folds every member nothing stores to down to that zero,\n"
                           "// deleting the storage. One volatile access anywhere in the object stops\n"
                           "// that for the whole object, so these declarations are also what protects\n"
                           "// the plain members the project put beside them. The access has to\n"
                           "// survive, not just the qualifier -- see BssSplit::decl.\n\n"
                           "%4\n")
                       .arg(sceneName, target, bss.symbolPrefix, bssDecls.join("\n"));
        out.bssHasContent = true;
    }
    return out;
}

class TileGridWidget : public QWidget {
    static constexpr double kGlyphSupersample = 4.0;

public:
    TileGridWidget(double tilePx, QTreeWidget* tree, QComboBox* targetCombo,
                   QCheckBox* showTileGridCheck, QSpinBox* tileGridWSpin, QSpinBox* tileGridHSpin,
                   QWidget* parent = nullptr)
        : QWidget(parent), tilePx_(tilePx), tree_(tree), targetCombo_(targetCombo),
          showTileGridCheck_(showTileGridCheck), tileGridWSpin_(tileGridWSpin), tileGridHSpin_(tileGridHSpin) {
        glyphFont_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        glyphFont_.setPixelSize(std::max(1, qRound(tilePx_ * 0.75 * kGlyphSupersample)));
        naturalGlyphWidthPx_ = std::max(1, QFontMetrics(glyphFont_).horizontalAdvance(QLatin1Char('M')));
        naturalGlyphHeightPx_ = std::max(1, QFontMetrics(glyphFont_).height());
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.fillRect(rect(), Qt::black);

        for (QTreeWidgetItem* item : collectComponents()) {
            if (!isNegSpaceItem(item)) {
                continue;
            }
            const int x = item->data(0, kPosXRole).toInt();
            const int y = item->data(0, kPosYRole).toInt();
            const int w = std::max(1, item->data(0, kSizeWRole).toInt());
            const int h = std::max(1, item->data(0, kSizeHRole).toInt());
            for (int row = 0; row < h; ++row) {
                for (int col = 0; col < w; ++col) {
                    painter.fillRect(tileRect(x + col, y + row), QColor(200, 0, 0));
                }
            }
        }

        for (QTreeWidgetItem* item : collectComponents()) {
            if (!isComponentItem(item)) {
                continue;
            }
            const int x = item->data(0, kPosXRole).toInt();
            const int y = item->data(0, kPosYRole).toInt();
            const int w = std::max(1, item->data(0, kSizeWRole).toInt());
            const int h = std::max(1, item->data(0, kSizeHRole).toInt());
            const QRect region = componentRect(x, y, w, h);

            painter.fillRect(region, QColor(70, 130, 180));

            const QString text = item->data(0, kTextContentRole).toString();
            if (!text.isEmpty()) {
                painter.setFont(glyphFont_);
                painter.setPen(Qt::white);

                const auto align = static_cast<TextAlign>(item->data(0, kAlignRole).toInt());
                const QString splitterStr = item->data(0, kSplitterRole).toString();
                const QChar splitter = splitterStr.isEmpty() ? QLatin1Char(' ') : splitterStr.at(0);
                const QStringList rows = wrapTextIntoRows(text, splitter, w);
                for (int row = 0; row < h && row < rows.size(); ++row) {
                    const QString& rowText = rows.at(row);
                    if (rowText.isEmpty()) {
                        break;
                    }
                    const int slack = w - rowText.length();
                    const int startCol = (align == TextAlign::Left)    ? 0
                                          : (align == TextAlign::Right) ? slack
                                                                         : slack / 2;
                    for (int i = 0; i < rowText.length(); ++i) {
                        drawCellGlyph(painter, tileRect(x + startCol + i, y + row), rowText.at(i));
                    }
                }
            }
        }

        for (QTreeWidgetItem* item : collectComponents()) {
            if (!isButtonItem(item)) {
                continue;
            }
            const int x = item->data(0, kPosXRole).toInt();
            const int y = item->data(0, kPosYRole).toInt();
            const int w = std::max(1, item->data(0, kSizeWRole).toInt());
            const int h = std::max(1, item->data(0, kSizeHRole).toInt());

            for (int row = 0; row < h; ++row) {
                const bool edgeRow = (row == 0 || row == h - 1);
                for (int col = 0; col < w; ++col) {
                    const bool edgeCol = (col == 0 || col == w - 1);
                    if (!edgeRow && !edgeCol) {
                        continue;  // interior -- left hollow
                    }
                    painter.fillRect(tileRect(x + col, y + row), QColor(120, 90, 20));
                }
            }

            painter.setFont(glyphFont_);
            painter.setPen(Qt::white);
            const QString name = bareName(item);
            for (int i = 0; i < name.length() && i < w; ++i) {
                drawCellGlyph(painter, tileRect(x + i, y), name.at(i));
            }
        }

        for (QTreeWidgetItem* item : collectComponents()) {
            if (!isSliderItem(item)) {
                continue;
            }
            const int x = item->data(0, kPosXRole).toInt();
            const int y = item->data(0, kPosYRole).toInt();
            const int w = std::max(1, item->data(0, kSizeWRole).toInt());
            const int h = std::max(1, item->data(0, kSizeHRole).toInt());

            for (int row = 0; row < h; ++row) {
                for (int col = 0; col < w; ++col) {
                    painter.fillRect(tileRect(x + col, y + row), QColor(60, 100, 60));
                }
            }

            painter.setFont(glyphFont_);
            painter.setPen(Qt::white);
            const QString name = bareName(item);
            for (int i = 0; i < name.length() && i < w; ++i) {
                drawCellGlyph(painter, tileRect(x + i, y), name.at(i));
            }
        }

        for (QTreeWidgetItem* item : collectComponents()) {
            if (!isRectItem(item)) {
                continue;
            }
            const int x = item->data(0, kPosXRole).toInt();
            const int y = item->data(0, kPosYRole).toInt();
            const int w = std::max(1, item->data(0, kSizeWRole).toInt());
            const int h = std::max(1, item->data(0, kSizeHRole).toInt());

            const QRect region = tileRect(x, y).united(tileRect(x + w - 1, y + h - 1));
            painter.setPen(QPen(QColor(150, 110, 200), 1, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(region.adjusted(0, 0, -1, -1));

            painter.setFont(glyphFont_);
            painter.setPen(QColor(190, 160, 230));
            const QString name = bareName(item);
            for (int i = 0; i < name.length() && i < w; ++i) {
                drawCellGlyph(painter, tileRect(x + i, y), name.at(i));
            }
        }

        if (showTileGridCheck_ && showTileGridCheck_->isChecked()) {
            const int cols = std::max(1, qRound(width() / tilePx_));
            const int rows = std::max(1, qRound(height() / tilePx_));
            const int stepX = std::max(1, tileGridWSpin_->value());
            const int stepY = std::max(1, tileGridHSpin_->value());
            painter.setPen(QPen(QColor(255, 255, 0, 160), 1));
            for (int col = stepX; col < cols; col += stepX) {
                const int lineX = tileRect(col, 0).left();
                painter.drawLine(lineX, 0, lineX, height());
            }
            for (int row = stepY; row < rows; row += stepY) {
                const int lineY = tileRect(0, row).top();
                painter.drawLine(0, lineY, width(), lineY);
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            QTreeWidgetItem* hit = hitTest(event->pos());
            dragItem_ = hit;
            if (hit) {
                tree_->setCurrentItem(hit);
                dragAnchorCell_ = cellAt(event->pos());
                dragOriginX_ = hit->data(0, kPosXRole).toInt();
                dragOriginY_ = hit->data(0, kPosYRole).toInt();
            }
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragItem_ && (event->buttons() & Qt::LeftButton)) {
            const QPoint cell = cellAt(event->pos());
            const int newX = std::max(0, dragOriginX_ + (cell.x() - dragAnchorCell_.x()));
            const int newY = std::max(0, dragOriginY_ + (cell.y() - dragAnchorCell_.y()));
            dragItem_->setData(0, kPosXExprRole, QString::number(newX));
            dragItem_->setData(0, kPosYExprRole, QString::number(newY));
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        dragItem_ = nullptr;
        QWidget::mouseReleaseEvent(event);
    }


private:
    const QImage& glyphImage(QChar ch) const {
        auto it = glyphCache_.find(ch);
        if (it != glyphCache_.end()) {
            return it.value();
        }
        QImage image(naturalGlyphWidthPx_, naturalGlyphHeightPx_, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setFont(glyphFont_);
        p.setPen(Qt::white);
        p.drawText(image.rect(), Qt::AlignCenter, QString(ch));
        p.end();
        return glyphCache_.insert(ch, image).value();
    }

    void drawCellGlyph(QPainter& painter, const QRect& cell, QChar ch) const {
        painter.drawImage(cell, glyphImage(ch));
    }

    QRect tileRect(int col, int row) const {
        const int cols = std::max(1, qRound(width() / tilePx_));
        const int rows = std::max(1, qRound(height() / tilePx_));
        const int left = qRound(col * tilePx_);
        const int top = qRound(row * tilePx_);
        const int right = (col >= cols - 1) ? width() : qRound((col + 1) * tilePx_);
        const int bottom = (row >= rows - 1) ? height() : qRound((row + 1) * tilePx_);
        return QRect(left, top, right - left, bottom - top);
    }

    QRect componentRect(int x, int y, int w, int h) const {
        return tileRect(x, y).united(tileRect(x + w - 1, y + h - 1));
    }

    QPoint cellAt(const QPoint& pos) const {
        const int cols = std::max(1, qRound(width() / tilePx_));
        const int rows = std::max(1, qRound(height() / tilePx_));
        const int col = std::clamp(static_cast<int>(pos.x() / tilePx_), 0, cols - 1);
        const int row = std::clamp(static_cast<int>(pos.y() / tilePx_), 0, rows - 1);
        return {col, row};
    }

    QVector<QTreeWidgetItem*> collectComponents() const {
        const QString target = targetCombo_->currentText();
        QVector<QTreeWidgetItem*> result;
        for (QTreeWidgetItem* item : collectComponentItems(tree_->invisibleRootItem())) {
            if (!isEffectivelyHidden(item, target)) {
                result.push_back(item);
            }
        }
        return result;
    }

    QTreeWidgetItem* hitTest(const QPoint& pos) const {
        const QPoint cell = cellAt(pos);
        auto contains = [&cell](QTreeWidgetItem* item) {
            const int x = item->data(0, kPosXRole).toInt();
            const int y = item->data(0, kPosYRole).toInt();
            const int w = std::max(1, item->data(0, kSizeWRole).toInt());
            const int h = std::max(1, item->data(0, kSizeHRole).toInt());
            return cell.x() >= x && cell.x() < x + w && cell.y() >= y && cell.y() < y + h;
        };
        QTreeWidgetItem* componentMatch = nullptr;
        QTreeWidgetItem* negSpaceMatch = nullptr;
        QTreeWidgetItem* rectMatch = nullptr;
        for (QTreeWidgetItem* item : collectComponents()) {
            if (!contains(item)) {
                continue;
            }
            if (isComponentItem(item)) {
                componentMatch = item;
            } else if (isRectItem(item)) {
                rectMatch = item;
            } else {
                negSpaceMatch = item;
            }
        }
        if (componentMatch) return componentMatch;
        return negSpaceMatch ? negSpaceMatch : rectMatch;
    }

    double tilePx_;
    QTreeWidget* tree_;
    QComboBox* targetCombo_;
    QCheckBox* showTileGridCheck_;
    QSpinBox* tileGridWSpin_;
    QSpinBox* tileGridHSpin_;
    QFont glyphFont_;
    int naturalGlyphWidthPx_ = 1;
    int naturalGlyphHeightPx_ = 1;
    mutable QHash<QChar, QImage> glyphCache_;
    QTreeWidgetItem* dragItem_ = nullptr;
    QPoint dragAnchorCell_;
    int dragOriginX_ = 0;
    int dragOriginY_ = 0;
};

struct Sidebar {
    QWidget* content;
    QTreeWidget* tree;
    QLineEdit* xEdit;
    QLineEdit* yEdit;
    QComboBox* targetCombo;

    QCheckBox* showTileGridCheck;
    QSpinBox* tileGridWSpin;
    QSpinBox* tileGridHSpin;

    std::function<QString()> sceneName;
    std::function<bool(const QString&)> loadScene;
    std::function<bool()> hasUnresolvedErrors;
    std::function<bool(const QString&, const QString&)> exportOneTarget;

    std::function<bool()> save;
    std::function<bool()> saveAs;
    std::function<bool()> confirmDiscard;

    std::function<QString()> displayName;
};

Sidebar createSidebar(UitkMainWindow* window, int maxTilesX, int maxTilesY, double tilePx,
                       std::function<void()> onTitleChanged) {
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);

    auto* tree = new QTreeWidget(content);
    tree->setHeaderHidden(true);

    auto* rootItem = new QTreeWidgetItem(tree, QStringList{"scene"});
    tree->addTopLevelItem(rootItem);
    tree->expandItem(rootItem);
    tree->setCurrentItem(rootItem);

    QObject::connect(tree, &QTreeWidget::itemChanged, [](QTreeWidgetItem* item, int column) {
        if (column != 0 || !isPrefixedItem(item)) {
            return;
        }
        const QString prefix = requiredPrefixFor(item);
        QString text = item->text(0);
        if (!text.startsWith(prefix)) {
            text = prefix + text;
        }
        const QString base = text.mid(prefix.length());
        static const QRegularExpression kIdentRe(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
        bool valid = kIdentRe.match(base).hasMatch();
        if (valid) {
            for (QTreeWidgetItem* other : collectPrefixedItems(item->treeWidget()->invisibleRootItem())) {
                if (other != item && other->text(0) == text) {
                    valid = false;
                    break;
                }
            }
        }
        if (!valid) {
            const QString last = item->data(0, kLastValidNameRole).toString();
            if (item->text(0) != last) {
                item->setText(0, last);
            }
            return;
        }
        if (item->text(0) != text) {
            item->setText(0, text);
        }
        item->setData(0, kLastValidNameRole, text);
    });

    auto* quadrantCombo = new QComboBox(content);
    quadrantCombo->addItems({"UL", "UR", "BL", "BR"});
    quadrantCombo->setToolTip("Which quadrant of the 2x2 world grid the whole scene's tile positions "
                                "resolve into in-game: UR shifts every position one viewport width "
                                "right, BL one viewport height down, BR both. Ensuring the game's "
                                "VRAM and mirroring state matches is the caller's job.");

    auto* targetCombo = new QComboBox(content);
    targetCombo->addItems(kTargetNames);
    targetCombo->setToolTip("Which pnes backend this scene targets.");

    auto* linkerPrefixEdit = new QLineEdit(content);
    linkerPrefixEdit->setToolTip("Emitted immediately before every generated function definition that "
                                  "isn't marked AI -- e.g. a linker-section attribute -- so codegen places "
                                  "it wherever the linker expects it. Left empty, nothing is emitted. "
                                  "Only ever emitted when exporting for NES -- every other target's "
                                  "generated code never references it.");

    auto* bssPrefixEdit = new QLineEdit(content);
    bssPrefixEdit->setToolTip("Emitted immediately before a SingleChoice's own instance storage (and, on a "
                               "variadic-viewport target, its mutable options array), and before every "
                               "Slider's <Name>_amount byte -- e.g. a RAM-section "
                               "placement attribute. Left empty, nothing is emitted. Only ever emitted when "
                               "exporting for NES.");

    auto* dataPrefixEdit = new QLineEdit(content);
    dataPrefixEdit->setToolTip("Emitted immediately before a ctTextBox's own charmap-encoded row data -- e.g. "
                                "a ROM-section placement attribute (see demo/src/graphics/strings.hpp's "
                                "TITLE_DATA for the hand-written equivalent). Left empty, nothing is emitted. "
                                "Only ever emitted when exporting for NES.");

    auto* charmapEdit = new QLineEdit(content);
    charmapEdit->setToolTip("Default mapname of the charmap (tech::nes_str::charmap, technology.hpp) every "
                             "ctTextBox's row text -- and every rtTextBox's splitter byte -- is encoded "
                             "through, e.g. \"generic\" for "
                             "charmap_generic. charmap_<name> must already be defined and in scope wherever "
                             "the generated header is included. Left empty, text is emitted as a plain, "
                             "unencoded C string instead (unless a node's own Charmap Override sets one). "
                             "A textbox's Charmap Override property replaces this default for that node.");

    auto* specialIncludeEdit = new QLineEdit(content);
    specialIncludeEdit->setPlaceholderText("#include <game/saveData.hpp>");
    specialIncludeEdit->setToolTip("One line of C++ emitted verbatim as the last include in the generated "
                                    "header -- e.g. the header declaring whatever a split-out BSS block's "
                                    "Symbol Prefix names. Written as-is, never parsed. Left empty, nothing "
                                    "is emitted.");

    auto* bssSeparateCheck = new QCheckBox(content);
    bssSeparateCheck->setToolTip("Moves every Slider amount, SingleChoice option and the scene's button_state "
                                  "mask into a separate bss.hpp (no namespace, no include guards) that the "
                                  "project includes wherever that storage should live. The generated header "
                                  "declares a reference to each in its place, reached through Symbol Prefix.\n\n"
                                  "Every declaration in that file is `atomic`, and keeping it that way is what "
                                  "stops LTO folding a save struct in uncleared memory down to its zero "
                                  "initializer -- see the note bss.hpp carries.");

    auto* bssSymbolPrefixEdit = new QLineEdit(content);
    bssSymbolPrefixEdit->setPlaceholderText("saveData.config.");
    bssSymbolPrefixEdit->setEnabled(false);
    bssSymbolPrefixEdit->setToolTip("Pasted verbatim ahead of every split-out symbol's flattened name -- e.g. "
                                     "\"saveData.config.\" puts this scene's state at "
                                     "saveData.config.<scene>_<node>_<name>. Trailing dot (or ::, or nothing "
                                     "at all for plain globals) included as written.");
    QObject::connect(bssSeparateCheck, &QCheckBox::toggled, bssSymbolPrefixEdit, &QLineEdit::setEnabled);

    auto* showTileGridCheck = new QCheckBox(content);
    showTileGridCheck->setToolTip("Overlays grid lines on the canvas every N tiles (set below) -- an editor-only "
                                   "aid with no effect on exported code.");
    auto* tileGridWSpin = new QSpinBox(content);
    tileGridWSpin->setRange(1, std::max(1, maxTilesX));
    tileGridWSpin->setValue(1);
    tileGridWSpin->setSuffix(" tiles");
    tileGridWSpin->setToolTip("Grid overlay spacing, in tiles, along X.");
    auto* tileGridHSpin = new QSpinBox(content);
    tileGridHSpin->setRange(1, std::max(1, maxTilesY));
    tileGridHSpin->setValue(1);
    tileGridHSpin->setSuffix(" tiles");
    tileGridHSpin->setToolTip("Grid overlay spacing, in tiles, along Y.");

    auto currentPath = std::make_shared<QString>();
    auto dirty = std::make_shared<bool>(false);
    auto loading = std::make_shared<bool>(false);

    auto resolveAllPtr = std::make_shared<std::function<void()>>([] {});

    auto sceneName = [currentPath] {
        if (!g_sceneNameOverride.isEmpty()) return g_sceneNameOverride;
        return currentPath->isEmpty() ? QStringLiteral("scene") : QFileInfo(*currentPath).completeBaseName();
    };

    auto displayName = [currentPath, dirty] {
        const QString name =
            currentPath->isEmpty() ? QStringLiteral("Untitled") : QFileInfo(*currentPath).fileName();
        return name + (*dirty ? QStringLiteral("*") : QString());
    };

    auto updateTitle = [rootItem, tree, sceneName, onTitleChanged] {
        {
            const QSignalBlocker blocker(tree);
            rootItem->setText(0, sceneName());
        }
        onTitleChanged();
    };
    updateTitle();

    QObject::connect(tree, &QTreeWidget::itemChanged, [dirty, loading, updateTitle](QTreeWidgetItem*, int) {
        if (!*loading) {
            *dirty = true;
            updateTitle();
        }
    });

    QObject::connect(tree, &QTreeWidget::itemChanged, [resolveAllPtr, loading](QTreeWidgetItem*, int) {
        if (!*loading) {
            (*resolveAllPtr)();
        }
    });

    auto doSaveAs = [window, rootItem, currentPath, dirty, updateTitle, quadrantCombo, targetCombo,
                     linkerPrefixEdit, bssPrefixEdit, dataPrefixEdit, charmapEdit, specialIncludeEdit,
                     bssSeparateCheck, bssSymbolPrefixEdit, showTileGridCheck,
                     tileGridWSpin, tileGridHSpin]() {
        QString path = QFileDialog::getSaveFileName(window, "Save Scene", QString(), "UI Scene (*.uis)");
        if (path.isEmpty()) {
            return false;
        }
        if (!path.endsWith(".uis", Qt::CaseInsensitive)) {
            path += ".uis";
        }
        if (!writeUisFile(path, rootItem, quadrantCombo->currentIndex(), targetCombo->currentText(),
                           linkerPrefixEdit->text(), bssPrefixEdit->text(),
                           dataPrefixEdit->text(), charmapEdit->text(), specialIncludeEdit->text(),
                           bssSeparateCheck->isChecked(), bssSymbolPrefixEdit->text(),
                           showTileGridCheck->isChecked(),
                           tileGridWSpin->value(), tileGridHSpin->value())) {
            QMessageBox::warning(window, "Save Failed", "Could not write file:\n" + path);
            return false;
        }
        *currentPath = path;
        *dirty = false;
        updateTitle();
        return true;
    };

    auto doSave = [rootItem, currentPath, dirty, updateTitle, doSaveAs, window, quadrantCombo, targetCombo,
                   linkerPrefixEdit, bssPrefixEdit, dataPrefixEdit, charmapEdit, specialIncludeEdit,
                   bssSeparateCheck, bssSymbolPrefixEdit, showTileGridCheck,
                   tileGridWSpin, tileGridHSpin]() {
        if (currentPath->isEmpty()) {
            return doSaveAs();
        }
        if (!writeUisFile(*currentPath, rootItem, quadrantCombo->currentIndex(), targetCombo->currentText(),
                           linkerPrefixEdit->text(), bssPrefixEdit->text(),
                           dataPrefixEdit->text(), charmapEdit->text(), specialIncludeEdit->text(),
                           bssSeparateCheck->isChecked(), bssSymbolPrefixEdit->text(),
                           showTileGridCheck->isChecked(),
                           tileGridWSpin->value(), tileGridHSpin->value())) {
            QMessageBox::warning(window, "Save Failed", "Could not write file:\n" + *currentPath);
            return false;
        }
        *dirty = false;
        updateTitle();
        return true;
    };

    auto confirmDiscard = [window, currentPath, dirty, doSave]() {
        if (!*dirty) {
            return true;
        }
        const QString name =
            currentPath->isEmpty() ? QStringLiteral("Untitled") : QFileInfo(*currentPath).fileName();
        const auto choice = QMessageBox::question(
            window, "Unsaved Changes", QString("Save changes to \"%1\" before continuing?").arg(name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (choice == QMessageBox::Cancel) {
            return false;
        }
        return choice == QMessageBox::Discard || doSave();
    };

    auto loadSceneFromFile = [tree, rootItem, currentPath, dirty, loading, updateTitle, resolveAllPtr,
                               quadrantCombo, targetCombo, linkerPrefixEdit, bssPrefixEdit,
                               dataPrefixEdit, charmapEdit, specialIncludeEdit, bssSeparateCheck,
                               bssSymbolPrefixEdit, showTileGridCheck, tileGridWSpin,
                               tileGridHSpin](const QString& path) -> bool {
        QJsonArray nodes;
        int quadrant = 0;
        QString target;
        QString linkerPrefix;
        QString bssPrefix;
        QString dataPrefix;
        QString charmap;
        QString specialInclude;
        bool bssSeparate = false;
        QString bssSymbolPrefix;
        bool showTileGrid = false;
        int tileGridW = 8;
        int tileGridH = 8;
        if (!parseUisFile(path, nodes, quadrant, target, linkerPrefix, bssPrefix, dataPrefix, charmap,
                           specialInclude, bssSeparate, bssSymbolPrefix, showTileGrid, tileGridW,
                           tileGridH)) {
            return false;
        }
        const int targetIndex = std::max(0, static_cast<int>(kTargetNames.indexOf(target)));
        *loading = true;
        qDeleteAll(rootItem->takeChildren());
        for (const QJsonValue& node : nodes) {
            deserializeNode(rootItem, node.toObject());
        }
        quadrantCombo->setCurrentIndex(quadrant);
        targetCombo->setCurrentIndex(targetIndex);
        linkerPrefixEdit->setText(linkerPrefix);
        bssPrefixEdit->setText(bssPrefix);
        dataPrefixEdit->setText(dataPrefix);
        charmapEdit->setText(charmap);
        specialIncludeEdit->setText(specialInclude);
        bssSeparateCheck->setChecked(bssSeparate);
        bssSymbolPrefixEdit->setText(bssSymbolPrefix);
        showTileGridCheck->setChecked(showTileGrid);
        tileGridWSpin->setValue(std::min(tileGridW, tileGridWSpin->maximum()));
        tileGridHSpin->setValue(std::min(tileGridH, tileGridHSpin->maximum()));
        *loading = false;
        (*resolveAllPtr)();
        tree->expandItem(rootItem);
        *currentPath = path;
        *dirty = false;
        updateTitle();
        return true;
    };

    auto hasUnresolvedErrors = [rootItem]() {
        for (const QTreeWidgetItem* item : collectComponentItems(rootItem)) {
            if (item->data(0, kErrorRole).toBool()) return true;
        }
        return false;
    };

    auto writeIfChanged = [](const QString& path, const QString& content) -> bool {
        QFile file(path);
        const bool existedBefore = file.exists();
        if (existedBefore && file.open(QIODevice::ReadOnly)) {
            const bool unchanged = (QString::fromUtf8(file.readAll()) == content);
            file.close();
            if (unchanged) {
                std::fprintf(stdout, "  unchanged  %s\n", qPrintable(path));
                return true;
            }
        }
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::fprintf(stderr, "  FAILED     %s\n", qPrintable(path));
            return false;
        }
        file.write(content.toUtf8());
        std::fprintf(stdout, "  %s    %s\n", existedBefore ? "updated" : "created", qPrintable(path));
        return true;
    };

    auto exportOneTarget = [window, rootItem, quadrantCombo, linkerPrefixEdit, bssPrefixEdit,
                             dataPrefixEdit, charmapEdit, specialIncludeEdit, bssSeparateCheck,
                             bssSymbolPrefixEdit, sceneName,
                             writeIfChanged](const QString& dir, const QString& target) -> bool {
        const QString targetDir = dir + "/" + target.toLower();
        if (!QDir().mkpath(targetDir)) return false;
        const GeneratedFiles files = generateCode(rootItem, target,
                                                    quadrantCombo->currentIndex(), linkerPrefixEdit->text(),
                                                    bssPrefixEdit->text(), dataPrefixEdit->text(),
                                                    charmapEdit->text(), specialIncludeEdit->text(),
                                                    bssSeparateCheck->isChecked(),
                                                    bssSymbolPrefixEdit->text(), sceneName());
        const bool hppOk = writeIfChanged(targetDir + "/" + sceneName() + ".hpp", files.hpp);

        const QString cppPath = targetDir + "/" + sceneName() + ".cpp";
        bool cppOk = true;
        if (files.cppHasContent) {
            cppOk = writeIfChanged(cppPath, files.cpp);
        } else if (QFile::exists(cppPath)) {
            cppOk = QFile::remove(cppPath);
            std::fprintf(stdout, cppOk ? "  removed    %s\n" : "  FAILED     %s\n", qPrintable(cppPath));
        }
        const QString bssPath = targetDir + "/bss.hpp";
        bool bssOk = true;
        if (files.bssHasContent) {
            bssOk = writeIfChanged(bssPath, files.bss);
        } else if (QFile::exists(bssPath)) {
            bssOk = QFile::remove(bssPath);
            std::fprintf(stdout, bssOk ? "  removed    %s\n" : "  FAILED     %s\n", qPrintable(bssPath));
        }
        return hppOk && cppOk && bssOk;
    };


    auto markDirtyFromCombo = [dirty, loading, updateTitle] {
        if (!*loading) {
            *dirty = true;
            updateTitle();
        }
    };
    QObject::connect(quadrantCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                      [markDirtyFromCombo](int) { markDirtyFromCombo(); });
    QObject::connect(targetCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                      [markDirtyFromCombo](int) { markDirtyFromCombo(); });
    QObject::connect(linkerPrefixEdit, &QLineEdit::textChanged,
                      [markDirtyFromCombo](const QString&) { markDirtyFromCombo(); });
    QObject::connect(bssPrefixEdit, &QLineEdit::textChanged,
                      [markDirtyFromCombo](const QString&) { markDirtyFromCombo(); });
    QObject::connect(dataPrefixEdit, &QLineEdit::textChanged,
                      [markDirtyFromCombo](const QString&) { markDirtyFromCombo(); });
    QObject::connect(charmapEdit, &QLineEdit::textChanged,
                      [markDirtyFromCombo](const QString&) { markDirtyFromCombo(); });
    QObject::connect(specialIncludeEdit, &QLineEdit::textChanged,
                      [markDirtyFromCombo](const QString&) { markDirtyFromCombo(); });
    QObject::connect(bssSeparateCheck, &QCheckBox::toggled, [markDirtyFromCombo](bool) { markDirtyFromCombo(); });
    QObject::connect(bssSymbolPrefixEdit, &QLineEdit::textChanged,
                      [markDirtyFromCombo](const QString&) { markDirtyFromCombo(); });
    QObject::connect(showTileGridCheck, &QCheckBox::toggled, [markDirtyFromCombo](bool) { markDirtyFromCombo(); });
    QObject::connect(tileGridWSpin, qOverload<int>(&QSpinBox::valueChanged),
                      [markDirtyFromCombo](int) { markDirtyFromCombo(); });
    QObject::connect(tileGridHSpin, qOverload<int>(&QSpinBox::valueChanged),
                      [markDirtyFromCombo](int) { markDirtyFromCombo(); });

    auto* properties = new QWidget(content);
    auto* posXEdit = new QLineEdit(properties);
    auto* posYEdit = new QLineEdit(properties);
    auto* sizeWEdit = new QLineEdit(properties);
    auto* sizeHEdit = new QLineEdit(properties);
    auto* alignCombo = new QComboBox(properties);
    auto* textEdit = new QLineEdit(properties);
    auto* splitterEdit = new QLineEdit(properties);
    auto* charmapOverrideEdit = new QLineEdit(properties);
    auto* provideErasingCheck = new QCheckBox(properties);
    auto* defaultOptionSpin = new QSpinBox(properties);
    defaultOptionSpin->setRange(0, 255);
    defaultOptionSpin->setToolTip("Which option (0-based, in tree order) this SingleChoice starts on.\n"
                                    "Clamped to the number of option children it actually has on export.");

    auto* buttonDefaultEnabledCheck = new QCheckBox(properties);
    auto* buttonToggleableCheck = new QCheckBox(properties);
    auto* buttonHasBoxCheck = new QCheckBox(properties);
    buttonDefaultEnabledCheck->setToolTip("Whether the scene starts with this button Enabled or Disabled.");
    buttonToggleableCheck->setToolTip("Also generate a Toggle_<Name>() that flips and returns the enabled flag.");
    buttonHasBoxCheck->setToolTip("Also generate a <Name>_box (vec2<u8>) constant with this button's Size X/Y.\n"
                                   "Leave unchecked if your Draw()/etc. implementation has "
                                   "no use for its own footprint (e.g. a fixed-size sprite).");

    auto* choiceVerticalCheck = new QCheckBox(properties);
    choiceVerticalCheck->setToolTip("Vertical: the options are a stacked list, stepped with UP/DOWN.\n"
                                     "Unchecked: a row, stepped with LEFT/RIGHT. Inside a Canvas, set this\n"
                                     "to the opposite of the Canvas's own axis so both can be moved.");

    auto* choiceHasRectCheck = new QCheckBox(properties);
    choiceHasRectCheck->setToolTip("Also generate a <Name>::rect covering every option this target shows.\n"
                                    "Leave unchecked unless something measures the menu as a whole -- on a\n"
                                    "runtime-viewport target it costs six bytes and a fold in Make(). Implied,\n"
                                    "whatever this says, when the menu sits inside a Canvas.");

    auto* canvasVerticalCheck = new QCheckBox(properties);
    canvasVerticalCheck->setToolTip("Vertical: focus steps with UP/DOWN and everything else (including\n"
                                     "LEFT/RIGHT) reaches the focused component. Unchecked: focus steps\n"
                                     "with LEFT/RIGHT instead, and UP/DOWN fall through. Either way, a\n"
                                     "component nested here wants the opposite axis to this one.");

    auto* sliderVerticalCheck = new QCheckBox(properties);
    sliderVerticalCheck->setToolTip("Vertical: the handle moves with UP/DOWN, a track down a column --\n"
                                     "your Draw()/DrawChange() measure the range off Size Y. Unchecked:\n"
                                     "LEFT/RIGHT along a row, measured off Size X. Inside a Canvas, set\n"
                                     "this to the opposite of the Canvas's own axis so both can be moved.");

    auto* hideTargetsButton = new QPushButton(properties);
    auto* hideTargetsMenu = new QMenu(hideTargetsButton);
    QVector<QAction*> hideTargetActions;
    for (const QString& name : kTargetNames) {
        QAction* action = hideTargetsMenu->addAction(name);
        action->setCheckable(true);
        hideTargetActions.push_back(action);
    }
    hideTargetsButton->setMenu(hideTargetsMenu);
    hideTargetsButton->setToolTip("Hide this node (and its children) whenever the scene's current "
                                   "Target is one of the checked platforms.");

    auto updateHideButtonSummary = [](QPushButton* button, const QVector<QAction*>& actions) {
        QStringList checked;
        for (QAction* action : actions) {
            if (action->isChecked()) checked << action->text();
        }
        button->setText(checked.isEmpty() ? QStringLiteral("(none)") : checked.join(QStringLiteral(", ")));
    };
    auto suppressHideWrite = std::make_shared<bool>(false);

    const QString exprHint = "A number, or a formula like Other.pos.x + 1.\n"
                              "Operators: + - * / << >> and parentheses.\n"
                              "VIEWPORT_TX/TY = viewport size in tiles, VIEWPORT_PX/PY = in pixels.\n"
                              "PANEL1_X/Y/TX/TY = the lower panel's rect in tiles (dual-screen targets).\n"
                              "Other.textSize / this.textSize = that textbox's text length in tiles.\n"
                              "'this' refers to the node the formula is on, e.g. this.textSize.";
    posXEdit->setToolTip(exprHint);
    posYEdit->setToolTip(exprHint);
    sizeWEdit->setToolTip(exprHint);
    sizeHEdit->setToolTip(exprHint);
    alignCombo->addItems({"Left", "Center", "Right"});
    splitterEdit->setMaxLength(1);
    splitterEdit->setToolTip("The single character that marks a word boundary when wrapping text "
                              "onto the next row (default: space).");
    charmapOverrideEdit->setToolTip("Charmap mapname (tech::nes_str::charmap, technology.hpp) to encode this "
                                     "node's row text/splitter through, in place of the scene's Default "
                                     "Charmap.\n"
                                     "Leave blank to use the scene's Default Charmap.");
    provideErasingCheck->setToolTip("Also generate an Erase_<Name>() function that blanks this box's whole "
                                     "footprint with its charmap's ' ' tile, via ppu::WriteRepeatedToNameTable.");

    auto* propertiesLayout = new QVBoxLayout(properties);
    propertiesLayout->setContentsMargins(0, 0, 0, 0);

    auto* geometryGroup = new QGroupBox("Geometry", properties);
    auto* geometryForm = new QFormLayout(geometryGroup);
    geometryForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    geometryForm->addRow("Position X", posXEdit);
    geometryForm->addRow("Position Y", posYEdit);
    geometryForm->addRow("Size X", sizeWEdit);
    geometryForm->addRow("Size Y", sizeHEdit);

    auto* componentGroup = new QGroupBox("Properties", properties);
    auto* componentForm = new QFormLayout(componentGroup);
    componentForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    componentForm->addRow("Alignment", alignCombo);
    componentForm->addRow("Text", textEdit);
    componentForm->addRow("Splitter", splitterEdit);
    componentForm->addRow("Charmap Override", charmapOverrideEdit);

    auto* choiceGroup = new QGroupBox("Choice", properties);
    auto* choiceForm = new QFormLayout(choiceGroup);
    choiceForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    choiceForm->addRow("Default Option", defaultOptionSpin);
    choiceForm->addRow("Vertical", choiceVerticalCheck);
    choiceForm->addRow("Provide Bounds", choiceHasRectCheck);

    auto* canvasGroup = new QGroupBox("Canvas", properties);
    auto* canvasForm = new QFormLayout(canvasGroup);
    canvasForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    canvasForm->addRow("Vertical", canvasVerticalCheck);

    auto* sliderGroup = new QGroupBox("Slider", properties);
    auto* sliderForm = new QFormLayout(sliderGroup);
    sliderForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    sliderForm->addRow("Vertical", sliderVerticalCheck);

    auto* buttonGroup = new QGroupBox("Button", properties);
    auto* buttonForm = new QFormLayout(buttonGroup);
    buttonForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    buttonForm->addRow("Default Enabled", buttonDefaultEnabledCheck);
    buttonForm->addRow("Toggleable", buttonToggleableCheck);
    buttonForm->addRow("Has Box", buttonHasBoxCheck);

    auto* erasingGroup = new QGroupBox("Erasing", properties);
    auto* erasingForm = new QFormLayout(erasingGroup);
    erasingForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    erasingForm->addRow("Provide Erasing", provideErasingCheck);

    auto* visibilityGroup = new QGroupBox("Visibility", properties);
    auto* visibilityForm = new QFormLayout(visibilityGroup);
    visibilityForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    visibilityForm->addRow("Hide on Target", hideTargetsButton);

    propertiesLayout->addWidget(geometryGroup);
    propertiesLayout->addWidget(componentGroup);
    propertiesLayout->addWidget(erasingGroup);
    propertiesLayout->addWidget(choiceGroup);
    propertiesLayout->addWidget(canvasGroup);
    propertiesLayout->addWidget(sliderGroup);
    propertiesLayout->addWidget(buttonGroup);
    propertiesLayout->addWidget(visibilityGroup);
    properties->setVisible(false);

    auto populateFrom = [posXEdit, posYEdit, sizeWEdit, sizeHEdit, alignCombo, textEdit, splitterEdit,
                         charmapOverrideEdit, provideErasingCheck, defaultOptionSpin, choiceVerticalCheck,
                         choiceHasRectCheck, canvasVerticalCheck, sliderVerticalCheck,
                         buttonDefaultEnabledCheck,
                         buttonToggleableCheck, buttonHasBoxCheck, hideTargetsButton,
                         hideTargetActions, updateHideButtonSummary,
                         suppressHideWrite](QTreeWidgetItem* item) {
        const QSignalBlocker bx(posXEdit);
        const QSignalBlocker by(posYEdit);
        const QSignalBlocker bw(sizeWEdit);
        const QSignalBlocker bh(sizeHEdit);
        const QSignalBlocker ba(alignCombo);
        const QSignalBlocker bt(textEdit);
        const QSignalBlocker bs(splitterEdit);
        const QSignalBlocker bc(charmapOverrideEdit);
        const QSignalBlocker be(provideErasingCheck);
        const QSignalBlocker bd(defaultOptionSpin);
        const QSignalBlocker bscv(choiceVerticalCheck);
        const QSignalBlocker bscr(choiceHasRectCheck);
        const QSignalBlocker bcv(canvasVerticalCheck);
        const QSignalBlocker bsv(sliderVerticalCheck);
        const QSignalBlocker bbe(buttonDefaultEnabledCheck);
        const QSignalBlocker bbt(buttonToggleableCheck);
        const QSignalBlocker bbb(buttonHasBoxCheck);
        auto exprOr = [item](int exprRole, int fallback) {
            const QString s = item->data(0, exprRole).toString();
            return s.isEmpty() ? QString::number(fallback) : s;
        };
        posXEdit->setText(exprOr(kPosXExprRole, 0));
        posYEdit->setText(exprOr(kPosYExprRole, 0));
        sizeWEdit->setText(exprOr(kSizeWExprRole, 1));
        sizeHEdit->setText(exprOr(kSizeHExprRole, 1));
        alignCombo->setCurrentIndex(item->data(0, kAlignRole).toInt());
        textEdit->setText(item->data(0, kTextContentRole).toString());
        const QString splitter = item->data(0, kSplitterRole).toString();
        splitterEdit->setText(splitter.isEmpty() ? QStringLiteral(" ") : splitter);
        charmapOverrideEdit->setText(item->data(0, kCharmapOverrideRole).toString());
        provideErasingCheck->setChecked(item->data(0, kProvideErasingRole).toBool());
        defaultOptionSpin->setValue(item->data(0, kDefaultOptionRole).toInt());
        choiceVerticalCheck->setChecked(item->data(0, kSingleChoiceVerticalRole).toBool());
        choiceHasRectCheck->setChecked(item->data(0, kSingleChoiceHasRectRole).toBool());
        canvasVerticalCheck->setChecked(item->data(0, kCanvasVerticalRole).toBool());
        sliderVerticalCheck->setChecked(item->data(0, kSliderVerticalRole).toBool());
        buttonDefaultEnabledCheck->setChecked(item->data(0, kButtonDefaultEnabledRole).toBool());
        buttonToggleableCheck->setChecked(item->data(0, kButtonToggleableRole).toBool());
        buttonHasBoxCheck->setChecked(item->data(0, kButtonHasBoxRole).toBool());

        *suppressHideWrite = true;
        const QStringList hideTargets = item->data(0, kHideTargetsRole).toStringList();
        for (QAction* action : hideTargetActions) {
            action->setChecked(hideTargets.contains(action->text()));
        }
        *suppressHideWrite = false;
        updateHideButtonSummary(hideTargetsButton, hideTargetActions);
    };

    auto updateErrorHighlight = [posXEdit, posYEdit, sizeWEdit, sizeHEdit](QTreeWidgetItem* item) {
        const int mask = item ? item->data(0, kErrorMaskRole).toInt() : 0;
        static const QString kErrorStyle = QStringLiteral("border: 1px solid red;");
        posXEdit->setStyleSheet((mask & 1) ? kErrorStyle : QString());
        posYEdit->setStyleSheet((mask & 2) ? kErrorStyle : QString());
        sizeWEdit->setStyleSheet((mask & 4) ? kErrorStyle : QString());
        sizeHEdit->setStyleSheet((mask & 8) ? kErrorStyle : QString());
    };

    QObject::connect(
        tree, &QTreeWidget::currentItemChanged,
        [properties, geometryGroup, componentGroup, erasingGroup, choiceGroup, canvasGroup, sliderGroup,
         buttonGroup, visibilityGroup, populateFrom,
         updateErrorHighlight](QTreeWidgetItem* current, QTreeWidgetItem*) {
            const bool selected = isPrefixedItem(current);
            properties->setVisible(selected);
            geometryGroup->setVisible(isGeometryItem(current));
            componentGroup->setVisible(isComponentItem(current));
            erasingGroup->setVisible(isCtTextBoxItem(current));
            choiceGroup->setVisible(isSingleChoiceItem(current));
            canvasGroup->setVisible(isCanvasItem(current));
            sliderGroup->setVisible(isSliderItem(current));
            buttonGroup->setVisible(isButtonItem(current));
            visibilityGroup->setVisible(selected);
            if (selected) {
                populateFrom(current);
                updateErrorHighlight(current);
            }
        });

    QObject::connect(
        tree, &QTreeWidget::itemChanged,
        [tree, populateFrom, updateErrorHighlight, posXEdit, posYEdit, sizeWEdit, sizeHEdit, textEdit,
         splitterEdit, charmapOverrideEdit, provideErasingCheck, defaultOptionSpin, choiceVerticalCheck,
         choiceHasRectCheck, canvasVerticalCheck, sliderVerticalCheck, buttonDefaultEnabledCheck,
         buttonToggleableCheck, buttonHasBoxCheck](QTreeWidgetItem* item, int column) {
            if (column != 0 || item != tree->currentItem() || !isPrefixedItem(item)) {
                return;
            }
            updateErrorHighlight(item);
            if (posXEdit->hasFocus() || posYEdit->hasFocus() || sizeWEdit->hasFocus() || sizeHEdit->hasFocus() ||
                textEdit->hasFocus() || splitterEdit->hasFocus() || charmapOverrideEdit->hasFocus() ||
                provideErasingCheck->hasFocus() || defaultOptionSpin->hasFocus() ||
                choiceVerticalCheck->hasFocus() || choiceHasRectCheck->hasFocus() ||
                canvasVerticalCheck->hasFocus() || sliderVerticalCheck->hasFocus() ||
                buttonDefaultEnabledCheck->hasFocus() ||
                buttonToggleableCheck->hasFocus() || buttonHasBoxCheck->hasFocus()) {
                return;
            }
            populateFrom(item);
        });

    auto writeToSelection = [tree](int exprRole, const QString& value) {
        QTreeWidgetItem* item = tree->currentItem();
        if (isGeometryItem(item)) {
            item->setData(0, exprRole, value);
        }
    };
    QObject::connect(posXEdit, &QLineEdit::editingFinished,
                      [writeToSelection, posXEdit] { writeToSelection(kPosXExprRole, posXEdit->text()); });
    QObject::connect(posYEdit, &QLineEdit::editingFinished,
                      [writeToSelection, posYEdit] { writeToSelection(kPosYExprRole, posYEdit->text()); });
    QObject::connect(sizeWEdit, &QLineEdit::editingFinished,
                      [writeToSelection, sizeWEdit] { writeToSelection(kSizeWExprRole, sizeWEdit->text()); });
    QObject::connect(sizeHEdit, &QLineEdit::editingFinished,
                      [writeToSelection, sizeHEdit] { writeToSelection(kSizeHExprRole, sizeHEdit->text()); });
    QObject::connect(alignCombo, qOverload<int>(&QComboBox::currentIndexChanged), [tree](int v) {
        QTreeWidgetItem* item = tree->currentItem();
        if (isComponentItem(item)) {
            item->setData(0, kAlignRole, v);
        }
    });
    QObject::connect(textEdit, &QLineEdit::editingFinished, [tree, textEdit] {
        QTreeWidgetItem* item = tree->currentItem();
        if (isComponentItem(item)) {
            item->setData(0, kTextContentRole, textEdit->text());
        }
    });
    QObject::connect(splitterEdit, &QLineEdit::editingFinished, [tree, splitterEdit] {
        QTreeWidgetItem* item = tree->currentItem();
        if (isComponentItem(item)) {
            const QString text = splitterEdit->text();
            item->setData(0, kSplitterRole, text.isEmpty() ? QStringLiteral(" ") : text);
        }
    });
    QObject::connect(charmapOverrideEdit, &QLineEdit::editingFinished, [tree, charmapOverrideEdit] {
        QTreeWidgetItem* item = tree->currentItem();
        if (isComponentItem(item)) {
            item->setData(0, kCharmapOverrideRole, charmapOverrideEdit->text().trimmed());
        }
    });
    QObject::connect(provideErasingCheck, &QCheckBox::toggled, [tree](bool checked) {
        QTreeWidgetItem* item = tree->currentItem();
        if (isCtTextBoxItem(item)) {
            item->setData(0, kProvideErasingRole, checked);
        }
    });
    QObject::connect(defaultOptionSpin, qOverload<int>(&QSpinBox::valueChanged), [tree](int v) {
        QTreeWidgetItem* item = tree->currentItem();
        if (isSingleChoiceItem(item)) {
            item->setData(0, kDefaultOptionRole, v);
        }
    });
    QObject::connect(choiceVerticalCheck, &QCheckBox::toggled, [tree](bool checked) {
        QTreeWidgetItem* item = tree->currentItem();
        if (isSingleChoiceItem(item)) {
            item->setData(0, kSingleChoiceVerticalRole, checked);
        }
    });
    QObject::connect(choiceHasRectCheck, &QCheckBox::toggled, [tree](bool checked) {
        QTreeWidgetItem* item = tree->currentItem();
        if (isSingleChoiceItem(item)) {
            item->setData(0, kSingleChoiceHasRectRole, checked);
        }
    });
    QObject::connect(canvasVerticalCheck, &QCheckBox::toggled, [tree](bool checked) {
        QTreeWidgetItem* item = tree->currentItem();
        if (isCanvasItem(item)) {
            item->setData(0, kCanvasVerticalRole, checked);
        }
    });
    QObject::connect(sliderVerticalCheck, &QCheckBox::toggled, [tree](bool checked) {
        QTreeWidgetItem* item = tree->currentItem();
        if (isSliderItem(item)) {
            item->setData(0, kSliderVerticalRole, checked);
        }
    });
    QObject::connect(buttonDefaultEnabledCheck, &QCheckBox::toggled, [tree](bool checked) {
        QTreeWidgetItem* item = tree->currentItem();
        if (isButtonItem(item)) {
            item->setData(0, kButtonDefaultEnabledRole, checked);
        }
    });
    QObject::connect(buttonToggleableCheck, &QCheckBox::toggled, [tree](bool checked) {
        QTreeWidgetItem* item = tree->currentItem();
        if (isButtonItem(item)) {
            item->setData(0, kButtonToggleableRole, checked);
        }
    });
    QObject::connect(buttonHasBoxCheck, &QCheckBox::toggled, [tree](bool checked) {
        QTreeWidgetItem* item = tree->currentItem();
        if (isButtonItem(item)) {
            item->setData(0, kButtonHasBoxRole, checked);
        }
    });
    auto writeHideSelection = [tree, suppressHideWrite](int role, QPushButton* button,
                                                          const QVector<QAction*>& actions,
                                                          const std::function<void(QPushButton*, const QVector<QAction*>&)>& updateSummary) {
        if (*suppressHideWrite) {
            return;
        }
        QTreeWidgetItem* item = tree->currentItem();
        if (!isPrefixedItem(item)) {
            return;
        }
        QStringList checked;
        for (QAction* action : actions) {
            if (action->isChecked()) checked << action->text();
        }
        item->setData(0, role, checked);
        updateSummary(button, actions);
    };
    for (QAction* action : hideTargetActions) {
        QObject::connect(action, &QAction::toggled, [writeHideSelection, hideTargetsButton, hideTargetActions,
                                                       updateHideButtonSummary](bool) {
            writeHideSelection(kHideTargetsRole, hideTargetsButton, hideTargetActions, updateHideButtonSummary);
        });
    }
    auto textBoxCounter = std::make_shared<int>(1);
    auto addTextBoxNode = [textBoxCounter](QTreeWidgetItem* parent, const char* kind, const QString& prefix) {
        const QString name = prefix + "Textbox" + QString::number((*textBoxCounter)++);
        auto* child = new QTreeWidgetItem(parent, QStringList{name});
        child->setFlags(child->flags() | Qt::ItemIsEditable);
        child->setData(0, kKindRole, QString(kind));
        child->setData(0, kTextContentRole, QString());
        child->setData(0, kPosXExprRole, QStringLiteral("0"));
        child->setData(0, kPosYExprRole, QStringLiteral("0"));
        child->setData(0, kSizeWExprRole, QStringLiteral("1"));
        child->setData(0, kSizeHExprRole, QStringLiteral("1"));
        child->setData(0, kPosXRole, 0);
        child->setData(0, kPosYRole, 0);
        child->setData(0, kSizeWRole, 1);
        child->setData(0, kSizeHRole, 1);
        child->setData(0, kAlignRole, static_cast<int>(TextAlign::Left));
        child->setData(0, kSplitterRole, QStringLiteral(" "));
        child->setData(0, kLastValidNameRole, name);
        parent->setExpanded(true);
        return child;
    };
    auto addCtTextBoxNode = [addTextBoxNode](QTreeWidgetItem* parent) {
        return addTextBoxNode(parent, kCtTextBoxKind, QString(kCtTextBoxPrefix));
    };
    auto addRtTextBoxNode = [addTextBoxNode](QTreeWidgetItem* parent) {
        return addTextBoxNode(parent, kRtTextBoxKind, QString(kRtTextBoxPrefix));
    };

    auto negSpaceCounter = std::make_shared<int>(1);
    auto addNegativeSpaceNode = [negSpaceCounter](QTreeWidgetItem* parent) {
        const QString name = QString(kNegSpacePrefix) + "NegativeSpace" + QString::number((*negSpaceCounter)++);
        auto* child = new QTreeWidgetItem(parent, QStringList{name});
        child->setFlags(child->flags() | Qt::ItemIsEditable);
        child->setData(0, kKindRole, QString(kNegSpaceKind));
        child->setData(0, kPosXExprRole, QStringLiteral("0"));
        child->setData(0, kPosYExprRole, QStringLiteral("0"));
        child->setData(0, kSizeWExprRole, QStringLiteral("1"));
        child->setData(0, kSizeHExprRole, QStringLiteral("1"));
        child->setData(0, kPosXRole, 0);
        child->setData(0, kPosYRole, 0);
        child->setData(0, kSizeWRole, 1);
        child->setData(0, kSizeHRole, 1);
        child->setData(0, kLastValidNameRole, name);
        parent->setExpanded(true);
        return child;
    };

    auto rectCounter = std::make_shared<int>(1);
    auto addRectNode = [rectCounter](QTreeWidgetItem* parent) {
        const QString name = QString(kRectPrefix) + "Rect" + QString::number((*rectCounter)++);
        auto* child = new QTreeWidgetItem(parent, QStringList{name});
        child->setFlags(child->flags() | Qt::ItemIsEditable);
        child->setData(0, kKindRole, QString(kRectKind));
        child->setData(0, kPosXExprRole, QStringLiteral("0"));
        child->setData(0, kPosYExprRole, QStringLiteral("0"));
        child->setData(0, kSizeWExprRole, QStringLiteral("1"));
        child->setData(0, kSizeHExprRole, QStringLiteral("1"));
        child->setData(0, kPosXRole, 0);
        child->setData(0, kPosYRole, 0);
        child->setData(0, kSizeWRole, 1);
        child->setData(0, kSizeHRole, 1);
        child->setData(0, kLastValidNameRole, name);
        parent->setExpanded(true);
        return child;
    };

    auto singleChoiceCounter = std::make_shared<int>(1);
    auto addSingleChoiceNode = [singleChoiceCounter](QTreeWidgetItem* parent) {
        const QString name = QString(kSingleChoicePrefix) + "SingleChoice" + QString::number((*singleChoiceCounter)++);
        auto* child = new QTreeWidgetItem(parent, QStringList{name});
        child->setFlags(child->flags() | Qt::ItemIsEditable);
        child->setData(0, kKindRole, QString(kSingleChoiceKind));
        child->setData(0, kSingleChoiceVerticalRole, true);
        child->setData(0, kSingleChoiceHasRectRole, false);
        child->setData(0, kLastValidNameRole, name);
        parent->setExpanded(true);
        return child;
    };

    auto canvasCounter = std::make_shared<int>(1);
    auto addCanvasNode = [canvasCounter](QTreeWidgetItem* parent) {
        const QString name = QString(kCanvasPrefix) + "Canvas" + QString::number((*canvasCounter)++);
        auto* child = new QTreeWidgetItem(parent, QStringList{name});
        child->setFlags(child->flags() | Qt::ItemIsEditable);
        child->setData(0, kKindRole, QString(kCanvasKind));
        child->setData(0, kCanvasVerticalRole, true);
        child->setData(0, kLastValidNameRole, name);
        parent->setExpanded(true);
        return child;
    };

    auto buttonCounter = std::make_shared<int>(1);
    auto addButtonNode = [buttonCounter](QTreeWidgetItem* parent) {
        const QString name = QString(kButtonPrefix) + "Button" + QString::number((*buttonCounter)++);
        auto* child = new QTreeWidgetItem(parent, QStringList{name});
        child->setFlags(child->flags() | Qt::ItemIsEditable);
        child->setData(0, kKindRole, QString(kButtonKind));
        child->setData(0, kPosXExprRole, QStringLiteral("0"));
        child->setData(0, kPosYExprRole, QStringLiteral("0"));
        child->setData(0, kSizeWExprRole, QStringLiteral("1"));
        child->setData(0, kSizeHExprRole, QStringLiteral("1"));
        child->setData(0, kPosXRole, 0);
        child->setData(0, kPosYRole, 0);
        child->setData(0, kSizeWRole, 1);
        child->setData(0, kSizeHRole, 1);
        child->setData(0, kButtonDefaultEnabledRole, true);
        child->setData(0, kButtonToggleableRole, false);
        child->setData(0, kButtonHasBoxRole, true);
        child->setData(0, kLastValidNameRole, name);
        parent->setExpanded(true);
        return child;
    };

    auto sliderCounter = std::make_shared<int>(1);
    auto addSliderNode = [sliderCounter](QTreeWidgetItem* parent) {
        const QString name = QString(kSliderPrefix) + "Slider" + QString::number((*sliderCounter)++);
        auto* child = new QTreeWidgetItem(parent, QStringList{name});
        child->setFlags(child->flags() | Qt::ItemIsEditable);
        child->setData(0, kKindRole, QString(kSliderKind));
        child->setData(0, kSliderVerticalRole, false);
        child->setData(0, kPosXExprRole, QStringLiteral("0"));
        child->setData(0, kPosYExprRole, QStringLiteral("0"));
        child->setData(0, kSizeWExprRole, QStringLiteral("1"));
        child->setData(0, kSizeHExprRole, QStringLiteral("1"));
        child->setData(0, kPosXRole, 0);
        child->setData(0, kPosYRole, 0);
        child->setData(0, kSizeWRole, 1);
        child->setData(0, kSizeHRole, 1);
        child->setData(0, kLastValidNameRole, name);
        parent->setExpanded(true);
        return child;
    };

    auto deleteNode = [rootItem, dirty, updateTitle, resolveAllPtr](QTreeWidgetItem* item) {
        if (!item || item == rootItem) {
            return;
        }
        delete item;
        *dirty = true;
        updateTitle();
        (*resolveAllPtr)();
    };

    auto* deleteShortcut = new QShortcut(QKeySequence::Delete, tree);
    deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(deleteShortcut, &QShortcut::activated,
                      [tree, deleteNode] { deleteNode(tree->currentItem()); });

    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(
        tree, &QTreeWidget::customContextMenuRequested,
        [tree, rootItem, addCtTextBoxNode, addRtTextBoxNode, addNegativeSpaceNode, addRectNode,
         addSingleChoiceNode, addCanvasNode, addButtonNode, addSliderNode, deleteNode](const QPoint& pos) {
            QTreeWidgetItem* clicked = tree->itemAt(pos);
            const bool onSingleChoice = isSingleChoiceItem(clicked);
            const bool onButton = isButtonItem(clicked);
            const bool onSlider = isSliderItem(clicked);
            const bool onTextBoxContainer = onSingleChoice || onButton || onSlider;
            const bool onCanvas = isCanvasItem(clicked);
            QTreeWidgetItem* textBoxParent = (onTextBoxContainer || onCanvas) ? clicked : rootItem;
            QTreeWidgetItem* interactableParent = onCanvas ? clicked : rootItem;
            QMenu menu;
            QAction* addCtTextboxAction = menu.addAction("Add Compile-Time Textbox");
            QAction* addRtTextboxAction = menu.addAction("Add Runtime Textbox");
            QAction* addRectAction = menu.addAction("Add Rect");
            QAction* addNegSpaceAction =
                (onTextBoxContainer || onCanvas) ? nullptr : menu.addAction("Add Negative Space");
            QAction* addSingleChoiceAction = onTextBoxContainer ? nullptr : menu.addAction("Add Single Choice");
            QAction* addButtonAction = onTextBoxContainer ? nullptr : menu.addAction("Add Button");
            QAction* addSliderAction = onTextBoxContainer ? nullptr : menu.addAction("Add Slider");
            QAction* addCanvasAction =
                (onTextBoxContainer || onCanvas) ? nullptr : menu.addAction("Add Canvas");
            QAction* deleteAction = nullptr;
            if (clicked && clicked != rootItem) {
                menu.addSeparator();
                deleteAction = menu.addAction("Delete");
            }
            QAction* chosen = menu.exec(tree->viewport()->mapToGlobal(pos));
            if (chosen == addCtTextboxAction) {
                tree->setCurrentItem(addCtTextBoxNode(textBoxParent));
            } else if (chosen == addRtTextboxAction) {
                tree->setCurrentItem(addRtTextBoxNode(textBoxParent));
            } else if (chosen && chosen == addRectAction) {
                tree->setCurrentItem(addRectNode(textBoxParent));
            } else if (chosen && chosen == addNegSpaceAction) {
                tree->setCurrentItem(addNegativeSpaceNode(rootItem));
            } else if (chosen && chosen == addSingleChoiceAction) {
                tree->setCurrentItem(addSingleChoiceNode(interactableParent));
            } else if (chosen && chosen == addButtonAction) {
                tree->setCurrentItem(addButtonNode(interactableParent));
            } else if (chosen && chosen == addSliderAction) {
                tree->setCurrentItem(addSliderNode(interactableParent));
            } else if (chosen && chosen == addCanvasAction) {
                tree->setCurrentItem(addCanvasNode(rootItem));
            } else if (chosen && chosen == deleteAction) {
                deleteNode(clicked);
            }
        });

    auto* splitter = new QSplitter(Qt::Vertical, content);
    splitter->addWidget(tree);
    splitter->addWidget(properties);
    layout->addWidget(splitter, /*stretch=*/1);

    auto* xEdit = new QLineEdit(content);
    auto* yEdit = new QLineEdit(content);
    xEdit->setValidator(new QIntValidator(1, maxTilesX, xEdit));
    yEdit->setValidator(new QIntValidator(1, maxTilesY, yEdit));
    auto clamp = [](QLineEdit* edit, const QString& text, int maxTiles) {
        const int value = text.toInt();
        if (value < 1) {
            edit->setText("1");
        } else if (value > maxTiles) {
            edit->setText(QString::number(maxTiles));
        }
    };
    QObject::connect(xEdit, &QLineEdit::textChanged, [xEdit, clamp, maxTilesX](const QString& text) {
        clamp(xEdit, text, maxTilesX);
    });
    QObject::connect(yEdit, &QLineEdit::textChanged, [yEdit, clamp, maxTilesY](const QString& text) {
        clamp(yEdit, text, maxTilesY);
    });

    const int digitsWidth = xEdit->fontMetrics().horizontalAdvance(QLatin1String("9999"));
    const int fieldWidth = digitsWidth + 16;
    xEdit->setFixedWidth(fieldWidth);
    yEdit->setFixedWidth(fieldWidth);

    xEdit->setText(QString::number(std::min(32, maxTilesX)));
    yEdit->setText(QString::number(std::min(30, maxTilesY)));

    QObject::connect(targetCombo, &QComboBox::currentTextChanged, [xEdit, yEdit, maxTilesX, maxTilesY](const QString& target) {
        const auto tiles = viewportTilesForTarget(target);
        if (!tiles) {
            return;
        }
        xEdit->setText(QString::number(std::min(tiles->first, maxTilesX)));
        yEdit->setText(QString::number(std::min(tiles->second, maxTilesY)));
    });

    auto resolving = std::make_shared<bool>(false);
    *resolveAllPtr = [window, rootItem, xEdit, yEdit, tilePx, resolving, targetCombo]() {
        if (*resolving) {
            return;
        }
        *resolving = true;

        const long long viewportTilesX = xEdit->text().toInt();
        const long long viewportTilesY = yEdit->text().toInt();
        const long long viewportPixelsX = qRound(viewportTilesX * tilePx);
        const long long viewportPixelsY = qRound(viewportTilesY * tilePx);

        const QVector<QTreeWidgetItem*> components = collectComponentItems(rootItem);

        QHash<QString, QTreeWidgetItem*> byName;
        QSet<QString> duplicateNames;
        for (QTreeWidgetItem* item : components) {
            const QString base = item->text(0).mid(requiredPrefixFor(item).length());
            if (byName.contains(base)) {
                duplicateNames.insert(base);
            }
            byName.insert(base, item);
        }

        struct Pending {
            QTreeWidgetItem* item;
            int prop;
            ExprPtr ast;
        };

        QVector<Pending> pending;
        std::map<std::pair<QTreeWidgetItem*, int>, long long> resolved;
        QHash<QTreeWidgetItem*, int> errorMask;

        for (QTreeWidgetItem* item : components) {
            for (int prop = 0; prop < 4; ++prop) {
                QString src = item->data(0, kGeomExprRoles[prop]).toString().trimmed();
                if (src.isEmpty()) {
                    src = QString::number(kGeomDefaults[prop]);
                }
                bool ok = false;
                ExprPtr ast = ExprParser(src).parse(ok);
                if (!ok) {
                    errorMask[item] |= (1 << prop);
                    continue;
                }
                pending.push_back({item, prop, ast});
            }
        }

        std::function<std::optional<long long>(const QString&, int)> resolveIdent =
            [&](const QString& name, int prop) -> std::optional<long long> {
            if (prop < 0) {
                if (name == QLatin1String("VIEWPORT_TX")) return viewportTilesX;
                if (name == QLatin1String("VIEWPORT_TY")) return viewportTilesY;
                if (name == QLatin1String("VIEWPORT_PX")) return viewportPixelsX;
                if (name == QLatin1String("VIEWPORT_PY")) return viewportPixelsY;
                if (name.startsWith(QLatin1String("PANEL1_"))) {
                    const auto panel = panel1TilesForTarget(targetCombo->currentText());
                    if (name == QLatin1String("PANEL1_X")) return panel ? panel->x : 0;
                    if (name == QLatin1String("PANEL1_Y")) return panel ? panel->y : 0;
                    if (name == QLatin1String("PANEL1_TX")) return panel ? panel->tx : viewportTilesX;
                    if (name == QLatin1String("PANEL1_TY")) return panel ? panel->ty : viewportTilesY;
                }
                return std::nullopt;
            }
            if (duplicateNames.contains(name)) {
                return std::nullopt;
            }
            const auto it = byName.find(name);
            if (it == byName.end()) {
                return std::nullopt;
            }
            if (prop == 4) {
                return isComponentItem(it.value())
                           ? std::optional<long long>(it.value()->data(0, kTextContentRole).toString().length())
                           : std::nullopt;
            }
            if (prop == 5) {
                return isEffectivelyHidden(it.value(), targetCombo->currentText())
                           ? 0
                           : 1;
            }
            const auto found = resolved.find({it.value(), prop});
            return found != resolved.end() ? std::optional<long long>(found->second) : std::nullopt;
        };

        auto resolveIdentFor = [&resolveIdent](QTreeWidgetItem* self) {
            const QString selfName = self->text(0).mid(requiredPrefixFor(self).length());
            return [&resolveIdent, selfName](const QString& name, int prop) {
                return resolveIdent(name == QLatin1String("this") ? selfName : name, prop);
            };
        };

        bool progress = true;
        while (progress && !pending.isEmpty()) {
            progress = false;
            for (int i = pending.size() - 1; i >= 0; --i) {
                const auto v = evalExpr(pending[i].ast, resolveIdentFor(pending[i].item));
                if (v) {
                    resolved[{pending[i].item, pending[i].prop}] = *v;
                    pending.removeAt(i);
                    progress = true;
                }
            }
        }
        for (const Pending& p : pending) {
            errorMask[p.item] |= (1 << p.prop);
        }
        for (const QString& dup : duplicateNames) {
            if (QTreeWidgetItem* item = byName.value(dup)) {
                errorMask[item] |= 0xF;  // ambiguous which property -- flag all of them
            }
        }

        QHash<QTreeWidgetItem*, QRect> rects;
        for (QTreeWidgetItem* item : components) {
            long long values[4];
            for (int prop = 0; prop < 4; ++prop) {
                const auto it = resolved.find({item, prop});
                long long value = (it != resolved.end()) ? it->second : kGeomDefaults[prop];
                value = (prop >= 2) ? std::max<long long>(1, value) : std::max<long long>(0, value);
                values[prop] = value;
                if (item->data(0, kGeomRoles[prop]).toLongLong() != value) {
                    item->setData(0, kGeomRoles[prop], static_cast<int>(value));
                }
            }
            rects[item] = QRect(static_cast<int>(values[0]), static_cast<int>(values[1]),
                                 static_cast<int>(values[2]), static_cast<int>(values[3]));
        }

        QHash<QTreeWidgetItem*, QStringList> violationPartners;
        for (QTreeWidgetItem* negItem : components) {
            if (!isNegSpaceItem(negItem)) {
                continue;
            }
            for (QTreeWidgetItem* other : components) {
                if (other == negItem || isNegSpaceItem(other)) {
                    continue;
                }
                if (rects.value(negItem).intersects(rects.value(other))) {
                    violationPartners[negItem] << other->text(0).mid(requiredPrefixFor(other).length());
                    violationPartners[other] << negItem->text(0).mid(requiredPrefixFor(negItem).length());
                }
            }
        }

        QStringList errorNames;
        QStringList violationDescriptions;
        for (QTreeWidgetItem* item : components) {
            const int mask = errorMask.value(item, 0);
            const bool hasExprError = mask != 0;
            const bool hasViolation = violationPartners.contains(item);
            const bool hasProblem = hasExprError || hasViolation;
            const bool maskChanged = item->data(0, kErrorMaskRole).toInt() != mask;
            const bool violationChanged = item->data(0, kNegSpaceViolationRole).toBool() != hasViolation;
            if (maskChanged || violationChanged) {
                item->setData(0, kErrorMaskRole, mask);
                item->setData(0, kNegSpaceViolationRole, hasViolation);
                item->setData(0, kErrorRole, hasProblem);
                QFont font = item->font(0);
                font.setBold(hasProblem);
                item->setFont(0, font);
                item->setForeground(0, hasProblem ? QBrush(Qt::red) : QBrush());
                item->setBackground(0, hasProblem ? QBrush(QColor(90, 20, 20)) : QBrush());
                QStringList tooltipLines;
                if (hasExprError) {
                    tooltipLines << QStringLiteral(
                        "Cannot resolve one or more properties -- check for typos, unknown/duplicate names, or a "
                        "dependency cycle.");
                }
                if (hasViolation) {
                    tooltipLines << QStringLiteral("Violated negative space: overlaps %1")
                                        .arg(violationPartners.value(item).join(QStringLiteral(", ")));
                }
                item->setToolTip(0, tooltipLines.join(QStringLiteral("\n")));
            }
            if (hasExprError) {
                errorNames << item->text(0).mid(requiredPrefixFor(item).length());
            }
            if (hasViolation && isNegSpaceItem(item)) {
                violationDescriptions << QStringLiteral("%1 overlaps %2")
                                             .arg(item->text(0).mid(requiredPrefixFor(item).length()),
                                                  violationPartners.value(item).join(QStringLiteral(", ")));
            }
        }

        QStringList bannerParts;
        if (!errorNames.isEmpty()) {
            bannerParts << QStringLiteral("Cannot resolve: %1").arg(errorNames.join(QStringLiteral(", ")));
        }
        if (!violationDescriptions.isEmpty()) {
            bannerParts << QStringLiteral("Violated negative space: %1")
                                .arg(violationDescriptions.join(QStringLiteral("; ")));
        }
        if (bannerParts.isEmpty()) {
            window->statusBar()->clearMessage();
            window->statusBar()->setStyleSheet(QString());
        } else {
            window->statusBar()->setStyleSheet(
                QStringLiteral("QStatusBar{background:#7a1f1f;color:white;font-weight:bold;}"));
            window->statusBar()->showMessage(QStringLiteral("⚠ %1").arg(bannerParts.join(QStringLiteral(" | "))));
        }

        *resolving = false;
    };
    (*resolveAllPtr)();

    QObject::connect(xEdit, &QLineEdit::textChanged, [resolveAllPtr](const QString&) { (*resolveAllPtr)(); });
    QObject::connect(yEdit, &QLineEdit::textChanged, [resolveAllPtr](const QString&) { (*resolveAllPtr)(); });

    QObject::connect(targetCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                      [resolveAllPtr](int) { (*resolveAllPtr)(); });

    auto* globalGroup = new QGroupBox("Global", content);
    auto* globalForm = new QFormLayout(globalGroup);
    globalForm->addRow("Viewport X (tx):", xEdit);
    globalForm->addRow("Viewport Y (tx):", yEdit);
    globalForm->addRow("Target:", targetCombo);
    layout->addWidget(globalGroup);

    auto* sceneGroup = new QGroupBox("Scene", content);
    auto* sceneForm = new QFormLayout(sceneGroup);
    sceneForm->addRow("Quadrant:", quadrantCombo);
    sceneForm->addRow("Linker Prefix:", linkerPrefixEdit);
    sceneForm->addRow("BSS Prefix:", bssPrefixEdit);
    sceneForm->addRow("Data Prefix:", dataPrefixEdit);
    sceneForm->addRow("Default Charmap:", charmapEdit);
    sceneForm->addRow("Special Include:", specialIncludeEdit);
    sceneForm->addRow("Generate BSS Separately:", bssSeparateCheck);
    sceneForm->addRow("Symbol Prefix:", bssSymbolPrefixEdit);
    layout->addWidget(sceneGroup);

    auto* editorGroup = new QGroupBox("Editor", content);
    auto* editorForm = new QFormLayout(editorGroup);
    editorForm->addRow("Show Tile Grid:", showTileGridCheck);
    editorForm->addRow("Grid Width:", tileGridWSpin);
    editorForm->addRow("Grid Height:", tileGridHSpin);
    layout->addWidget(editorGroup);

    auto updateGlobalSceneVisibility = [globalGroup, sceneGroup, editorGroup, rootItem](QTreeWidgetItem* current) {
        const bool onRoot = (current == rootItem);
        globalGroup->setVisible(onRoot);
        sceneGroup->setVisible(onRoot);
        editorGroup->setVisible(onRoot);
    };
    QObject::connect(tree, &QTreeWidget::currentItemChanged,
                      [updateGlobalSceneVisibility](QTreeWidgetItem* current, QTreeWidgetItem*) {
                          updateGlobalSceneVisibility(current);
                      });
    updateGlobalSceneVisibility(tree->currentItem());

    return {content, tree, xEdit, yEdit, targetCombo,
             showTileGridCheck, tileGridWSpin, tileGridHSpin,
             sceneName, loadSceneFromFile, hasUnresolvedErrors, exportOneTarget,
             doSave, doSaveAs, confirmDiscard, displayName};
}

struct ViewportPanel {
    TileGridWidget* grid;
    std::function<void()> sync;
};

ViewportPanel createViewportPanel(QMainWindow* window, double tilePx, QTreeWidget* tree, QLineEdit* xEdit,
                                   QLineEdit* yEdit, QComboBox* targetCombo,
                                   QCheckBox* showTileGridCheck, QSpinBox* tileGridWSpin, QSpinBox* tileGridHSpin) {
    auto* grid = new TileGridWidget(tilePx, tree, targetCombo, showTileGridCheck, tileGridWSpin,
                                     tileGridHSpin);

    QObject::connect(tree, &QTreeWidget::itemChanged, grid, [grid](QTreeWidgetItem*, int) { grid->update(); });

    QObject::connect(targetCombo, qOverload<int>(&QComboBox::currentIndexChanged), grid, [grid](int) { grid->update(); });

    QObject::connect(showTileGridCheck, &QCheckBox::toggled, grid, [grid](bool) { grid->update(); });
    QObject::connect(tileGridWSpin, qOverload<int>(&QSpinBox::valueChanged), grid, [grid](int) { grid->update(); });
    QObject::connect(tileGridHSpin, qOverload<int>(&QSpinBox::valueChanged), grid, [grid](int) { grid->update(); });

    QObject::connect(tree->model(), &QAbstractItemModel::rowsRemoved, grid, [grid] { grid->update(); });
    QObject::connect(tree->model(), &QAbstractItemModel::rowsInserted, grid, [grid] { grid->update(); });
    QObject::connect(tree->model(), &QAbstractItemModel::modelReset, grid, [grid] { grid->update(); });

    auto sync = std::make_shared<std::function<void()>>();
    *sync = [grid, tilePx, xEdit, yEdit, window] {
        const int tilesX = std::max(1, xEdit->text().toInt());
        const int tilesY = std::max(1, yEdit->text().toInt());
        const int gridWidth = std::max(kMinGridDimensionPx, qRound(tilesX * tilePx));
        const int gridHeight = std::max(kMinGridDimensionPx, qRound(tilesY * tilePx));
        grid->setFixedSize(gridWidth, gridHeight);
        lockWindowToContents(window);
    };

    auto* debounce = new QTimer(window);
    debounce->setSingleShot(true);
    debounce->setInterval(0);
    QObject::connect(debounce, &QTimer::timeout, window, [sync] { (*sync)(); });
    QObject::connect(xEdit, &QLineEdit::textChanged, debounce, [debounce] { debounce->start(); });
    QObject::connect(yEdit, &QLineEdit::textChanged, debounce, [debounce] { debounce->start(); });

    return {grid, [sync] { (*sync)(); }};
}

struct TabManager {
    UitkMainWindow* window;
    QTabWidget* tabs;
    QStackedWidget* sidebarStack;
    double tilePx;
    int maxTilesX;
    int maxTilesY;

    std::vector<std::shared_ptr<Sidebar>> sidebars;
    std::vector<std::shared_ptr<ViewportPanel>> viewports;

    Sidebar* active() { return sidebars[tabs->currentIndex()].get(); }

    Sidebar* newTab() {
        auto sidebarBox = std::make_shared<std::shared_ptr<Sidebar>>();
        Sidebar sidebar = createSidebar(window, maxTilesX, maxTilesY, tilePx, [this, sidebarBox] {
            if (!*sidebarBox) return;
            const int i = sidebarStack->indexOf((*sidebarBox)->content);
            if (i < 0) return;
            tabs->setTabText(i, (*sidebarBox)->displayName());
        });
        auto sidebarPtr = std::make_shared<Sidebar>(sidebar);
        *sidebarBox = sidebarPtr;

        auto viewportPtr = std::make_shared<ViewportPanel>(
            createViewportPanel(window, tilePx, sidebar.tree, sidebar.xEdit, sidebar.yEdit, sidebar.targetCombo,
                                 sidebar.showTileGridCheck, sidebar.tileGridWSpin,
                                 sidebar.tileGridHSpin));

        sidebars.push_back(sidebarPtr);
        viewports.push_back(viewportPtr);

        sidebarStack->addWidget(sidebarPtr->content);
        const int index = tabs->addTab(viewportPtr->grid, sidebarPtr->displayName());
        tabs->setCurrentIndex(index);
        return sidebarPtr.get();
    }

    void openTab() {
        const QString path = QFileDialog::getOpenFileName(window, "Open Scene", QString(), "UI Scene (*.uis)");
        if (path.isEmpty()) return;
        const int index = tabs->count();
        Sidebar* sidebar = newTab();
        if (!sidebar->loadScene(path)) {
            QMessageBox::warning(window, "Open Failed", "Could not read file:\n" + path);
            removeTabAt(index);
        }
    }

    bool closeTabAt(int index) {
        if (index < 0 || index >= static_cast<int>(sidebars.size())) return false;
        if (sidebars.size() <= 1) return false;
        if (!sidebars[index]->confirmDiscard()) return false;
        removeTabAt(index);
        return true;
    }
    bool closeActive() { return closeTabAt(tabs->currentIndex()); }

    bool confirmCloseAll() {
        for (const auto& sidebar : sidebars) {
            if (!sidebar->confirmDiscard()) return false;
        }
        return true;
    }

private:
    void removeTabAt(int index) {
        QWidget* grid = viewports[index]->grid;
        QWidget* content = sidebars[index]->content;
        {
            const QSignalBlocker blocker(tabs);
            tabs->removeTab(index);
            sidebarStack->removeWidget(content);
        }
        grid->deleteLater();
        content->deleteLater();
        sidebars.erase(sidebars.begin() + index);
        viewports.erase(viewports.begin() + index);

        const int current = tabs->currentIndex();
        if (current >= 0) {
            sidebarStack->setCurrentIndex(current);
            viewports[current]->sync();
        }
    }
};

void createFileMenu(UitkMainWindow* window, TabManager& tabManager) {
    auto* fileMenu = window->menuBar()->addMenu("&File");
    auto addFileAction = [fileMenu](const QString& text, QKeySequence::StandardKey key, auto&& handler) {
        QAction* action = fileMenu->addAction(text);
        action->setShortcut(key);
        QObject::connect(action, &QAction::triggered, handler);
    };
    addFileAction("New", QKeySequence::New, [&tabManager] { tabManager.newTab(); });
    addFileAction("Open...", QKeySequence::Open, [&tabManager] { tabManager.openTab(); });
    fileMenu->addSeparator();
    addFileAction("Save", QKeySequence::Save, [&tabManager] { tabManager.active()->save(); });
    addFileAction("Save As...", QKeySequence::SaveAs, [&tabManager] { tabManager.active()->saveAs(); });
    addFileAction("Close Tab", QKeySequence::Close, [&tabManager] { tabManager.closeActive(); });

    auto showExportFailed = [window] {
        QMessageBox::warning(window, "Export Failed",
                              "Cannot export: one or more components have unresolved properties. "
                              "Fix the errors flagged in the sidebar first.");
    };

    auto doExportTarget = [window, &tabManager, showExportFailed] {
        Sidebar* sidebar = tabManager.active();
        if (sidebar->hasUnresolvedErrors()) {
            showExportFailed();
            return;
        }
        bool ok = false;
        const QString target =
            QInputDialog::getItem(window, "Export Target", "Target:", kTargetNames, 0, false, &ok);
        if (!ok) return;
        const QString dir = QFileDialog::getExistingDirectory(window, "Export Target To (gen/ root)");
        if (dir.isEmpty()) return;
        if (!sidebar->exportOneTarget(dir, target)) {
            QMessageBox::warning(window, "Export Failed", "Could not write generated files to:\n" + dir);
        }
    };

    auto doExportAll = [window, &tabManager, showExportFailed] {
        Sidebar* sidebar = tabManager.active();
        if (sidebar->hasUnresolvedErrors()) {
            showExportFailed();
            return;
        }
        const QString dir = QFileDialog::getExistingDirectory(window, "Export All To (gen/ root)");
        if (dir.isEmpty()) return;
        QStringList failed;
        for (const QString& target : kTargetNames) {
            if (!sidebar->exportOneTarget(dir, target)) failed << target;
        }
        if (!failed.isEmpty()) {
            QMessageBox::warning(window, "Export Failed",
                                  "Could not write generated files for:\n" + failed.join(", "));
        }
    };

    fileMenu->addSeparator();
    QObject::connect(fileMenu->addAction("Export Target..."), &QAction::triggered, doExportTarget);
    QObject::connect(fileMenu->addAction("Export All..."), &QAction::triggered, doExportAll);
}

QString canonicalTargetName(const QString& target) {
    for (const QString& t : kTargetNames) {
        if (t.compare(target, Qt::CaseInsensitive) == 0) return t;
    }
    return QString();
}

int runCliExport(int argc, char** argv, bool exportAll, const QString& inputPath, const QString& outputPath,
                  const QString& target) {
    QApplication app(argc, argv);

    UitkMainWindow window;
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = app.primaryScreen();
    }
    const double tilePx = tilePxForScreen(screen->geometry());
    const int sidebarWidthPx = sidebarWidthForScreen(screen->geometry());
    const QRect available = screen->availableGeometry();
    const int maxTilesX = std::max(
        1, static_cast<int>((available.width() - sidebarWidthPx - kWindowChromeMarginPx) / tilePx));
    const int maxTilesY =
        std::max(1, static_cast<int>((available.height() - kWindowChromeMarginPx) / tilePx));

    const Sidebar sidebar = createSidebar(&window, maxTilesX, maxTilesY, tilePx, [] {});

    if (!sidebar.loadScene(inputPath)) {
        std::fprintf(stderr, "uitk: could not read scene file: %s\n", qPrintable(inputPath));
        return 1;
    }
    if (sidebar.hasUnresolvedErrors()) {
        std::fprintf(stderr,
                      "uitk: cannot export %s: one or more components have unresolved properties\n",
                      qPrintable(inputPath));
        return 1;
    }

    std::fprintf(stdout, "uitk: exporting %s (scene \"%s\") to %s\n", qPrintable(inputPath),
                 qPrintable(sidebar.sceneName()), qPrintable(outputPath));

    if (exportAll) {
        QStringList failed;
        for (const QString& t : kTargetNames) {
            std::fprintf(stdout, "%s:\n", qPrintable(t));
            if (!sidebar.exportOneTarget(outputPath, t)) failed << t;
        }
        if (!failed.isEmpty()) {
            std::fprintf(stderr, "uitk: could not write generated files for: %s\n",
                          qPrintable(failed.join(", ")));
            return 1;
        }
        std::fprintf(stdout, "uitk: exported %d target(s) successfully\n", static_cast<int>(kTargetNames.size()));
        return 0;
    }

    const QString canonicalTarget = canonicalTargetName(target);
    if (canonicalTarget.isEmpty()) {
        std::fprintf(stderr, "uitk: unknown target \"%s\" -- expected one of: %s\n", qPrintable(target),
                      qPrintable(kTargetNames.join(", ")));
        return 1;
    }
    std::fprintf(stdout, "%s:\n", qPrintable(canonicalTarget));
    if (!sidebar.exportOneTarget(outputPath, canonicalTarget)) {
        std::fprintf(stderr, "uitk: could not write generated files to: %s\n", qPrintable(outputPath));
        return 1;
    }
    std::fprintf(stdout, "uitk: export complete\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    bool cliExport = false;
    bool cliExportAll = false;
    QString cliInput;
    QString cliOutput;
    QString cliTarget;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        const bool wantsValue = (arg == "-i" || arg == "--input" || arg == "-o" || arg == "--output" ||
                                  arg == "-t" || arg == "--target" || arg == "-n" || arg == "--name");
        if (wantsValue && i + 1 >= argc) {
            std::fprintf(stderr, "uitk: %s requires an argument\n", qPrintable(arg));
            return 1;
        }
        if (arg == "-e" || arg == "--export") {
            cliExport = true;
        } else if (arg == "-E" || arg == "--export-all") {
            cliExportAll = true;
        } else if (arg == "-i" || arg == "--input") {
            cliInput = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == "-o" || arg == "--output") {
            cliOutput = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == "-t" || arg == "--target") {
            cliTarget = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == "-n" || arg == "--name") {
            g_sceneNameOverride = QString::fromLocal8Bit(argv[++i]);
        }
    }

    if (cliExport || cliExportAll) {
        if (cliExport && cliExportAll) {
            std::fprintf(stderr, "uitk: -e/--export and -E/--export-all are mutually exclusive\n");
            return 1;
        }
        if (cliInput.isEmpty()) {
            std::fprintf(stderr, "uitk: -e/-E requires -i/--input <scene.uis>\n");
            return 1;
        }
        if (cliOutput.isEmpty()) {
            std::fprintf(stderr, "uitk: -e/-E requires -o/--output <output dir>\n");
            return 1;
        }
        if (cliExport && cliTarget.isEmpty()) {
            std::fprintf(stderr, "uitk: -e/--export requires -t/--target <target> "
                                  "(use -E/--export-all to export every target instead)\n");
            return 1;
        }
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
        }
        return runCliExport(argc, argv, cliExportAll, cliInput, cliOutput, cliTarget);
    }

    QApplication app(argc, argv);

    UitkMainWindow window;
    window.setWindowTitle("uitk");

    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = app.primaryScreen();
    }
    const double tilePx = tilePxForScreen(screen->geometry());
    const int sidebarWidthPx = sidebarWidthForScreen(screen->geometry());

    const QRect available = screen->availableGeometry();
    const int maxTilesX = std::max(
        1, static_cast<int>((available.width() - sidebarWidthPx - kWindowChromeMarginPx) / tilePx));
    const int maxTilesY =
        std::max(1, static_cast<int>((available.height() - kWindowChromeMarginPx) / tilePx));

    auto* sidebarDock = new QDockWidget("Sidebar", &window);
    sidebarDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    sidebarDock->setFixedWidth(sidebarWidthPx);
    QObject::connect(sidebarDock, &QDockWidget::visibilityChanged, sidebarDock, [sidebarDock](bool visible) {
        if (!visible) {
            QTimer::singleShot(0, sidebarDock, [sidebarDock] { sidebarDock->setVisible(true); });
        }
    });
    auto* sidebarStack = new QStackedWidget(sidebarDock);
    sidebarDock->setWidget(sidebarStack);

    auto* tabs = new QTabWidget(&window);
    tabs->setTabsClosable(true);
    window.setCentralWidget(tabs);
    window.addDockWidget(Qt::RightDockWidgetArea, sidebarDock);

    TabManager tabManager{&window, tabs, sidebarStack, tilePx, maxTilesX, maxTilesY};
    createFileMenu(&window, tabManager);
    window.confirmClose = [&tabManager] { return tabManager.confirmCloseAll(); };

    window.installEventFilter(new LayoutChangeNotifier([&window] { lockWindowToContents(&window); }, &window));

    QObject::connect(tabs, &QTabWidget::currentChanged, &window, [&tabManager](int index) {
        if (index < 0) return;
        tabManager.sidebarStack->setCurrentIndex(index);
        tabManager.viewports[index]->sync();
    });
    QObject::connect(tabs, &QTabWidget::tabCloseRequested, &window,
                      [&tabManager](int index) { tabManager.closeTabAt(index); });

    tabManager.newTab();

    window.show();

    auto* captureTimer = new QTimer(&window);
    QObject::connect(captureTimer, &QTimer::timeout, [&window, screen] {
        screen->grabWindow(window.winId()).save("/tmp/uitk_capture.png");
    });
    captureTimer->start(200);

    return app.exec();
}

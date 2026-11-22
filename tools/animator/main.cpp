// 'pnes' Animation Designer
//
// Loads a blobman manifest, builds a table of every included tile with the PPU
// id the hardware would see, and shows it 16-across down the left of the window.
// Clicking a tile picks it; the tabbed area beside holds the metasprites, each
// itself tabbed one tab per animation frame.

#include "chr_table.hpp"
#include "codegen.hpp"
#include "exporter.hpp"
#include "metasprite_page.hpp"
#include "palette_store.hpp"
#include "frame_window.hpp"
#include "palette_window.hpp"
#include "project.hpp"
#include "tile_canvas.hpp"

#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QMenu>
#include <QAction>
#include <QKeySequence>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QToolButton>
#include <QComboBox>
#include <QLabel>
#include <QCloseEvent>
#include <QBoxLayout>
#include <QWidget>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QStyle>
#include <QFrame>
#include <QSignalBlocker>
#include <QString>
#include <QFileInfo>
#include <QDir>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

namespace {

constexpr int kWindowChromeMarginPx = 60;

constexpr int kMinWindowHeightPx = 320;

constexpr int kMetaspriteAreaMinPxAt1080p = 480;
constexpr int kZoomAt1080p = 4;


class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(int zoom) : zoom_(zoom) {
        setWindowTitle("pnes Animation Designer");

        auto* central = new QWidget(this);
        auto* columns = new QHBoxLayout(central);
        columns->setContentsMargins(0, 0, 0, 0);
        columns->setSpacing(0);
        setCentralWidget(central);

        canvas_ = new TileCanvas(zoom, nullptr);
        connect(canvas_, &TileCanvas::cellHovered, this, &MainWindow::showCell);
        connect(canvas_, &TileCanvas::cellPicked, this, &MainWindow::pickCell);

        sheetScroll_ = new QScrollArea(central);
        sheetScroll_->setWidget(canvas_);
        sheetScroll_->setWidgetResizable(false);
        sheetScroll_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        sheetScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        sheetScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sheetScroll_->setFrameShape(QFrame::StyledPanel);
        sheetScroll_->setFixedWidth(sheetWidth());

        sheetPalette_ = new QComboBox(central);
        connect(sheetPalette_, &QComboBox::currentIndexChanged, this, [this] { applySheetPalette(); });

        auto* sheetColumn = new QVBoxLayout;
        sheetColumn->setContentsMargins(0, 0, 0, 0);
        auto* sheetHeader = new QHBoxLayout;
        sheetHeader->addWidget(new QLabel("Palette", central));
        sheetHeader->addWidget(sheetPalette_, 1);
        sheetColumn->addLayout(sheetHeader);
        sheetColumn->addWidget(sheetScroll_, 1);

        auto* sheetPanel = new QWidget(central);
        sheetPanel->setLayout(sheetColumn);
        sheetPanel->setFixedWidth(sheetWidth());
        columns->addWidget(sheetPanel);

        tabs_ = new QTabWidget(central);
        tabs_->setTabsClosable(true);
        tabs_->setDocumentMode(true);
        connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
        connect(tabs_, &QTabWidget::currentChanged, this, &MainWindow::tabChanged);

        auto* addMetasprite = new QToolButton(tabs_);
        addMetasprite->setText("+");
        addMetasprite->setToolTip("New metasprite (Ctrl+N)");
        addMetasprite->setAutoRaise(true);
        connect(addMetasprite, &QToolButton::clicked, this, &MainWindow::newMetasprite);
        tabs_->setCornerWidget(addMetasprite, Qt::TopRightCorner);

        columns->addWidget(tabs_, 1);

        connect(paletteStore_, &PaletteStore::changed, this, &MainWindow::refreshSheetPalettes);
        connect(paletteStore_, &PaletteStore::changed, this, &MainWindow::markDirty);
        refreshSheetPalettes();

        buildMenus();
        statusBar()->showMessage(summary_);
    }

    void setTallTiles(bool tall) { tallTiles_->setChecked(tall); }

    bool loadManifest(const QString& path, bool quiet = false) {
        const chr::TileMode mode = tileMode();

        std::string error;
        auto table = chr::loadManifest(path.toStdString(), mode, frames_, error);
        if (!table) {
            const QString message = QString("Cannot load %1: %2")
                                        .arg(QFileInfo(path).fileName(), QString::fromStdString(error));
            std::fprintf(stderr, "animator: %s\n", qPrintable(message));
            statusBar()->showMessage(message);
            if (!quiet) {
                QMessageBox::warning(this, "Open Manifest", message);
            }
            return false;
        }

        const bool differentManifest = manifestPath_ != path;
        table_ = std::move(table);
        manifestPath_ = path;
        reload_->setEnabled(true);
        if (frames_action_) frames_action_->setEnabled(true);
        canvas_->setTable(*table_);
        canvas_->resize(canvas_->sizeHint());
        sheetScroll_->verticalScrollBar()->setValue(0);
        if (differentManifest) markDirty();
        refreshTitle();

        clearPick();

        for (MetaspritePage* page : pages_) {
            page->setTileMode(mode);
        }

        for (const std::string& warning : table_->warnings) {
            std::fprintf(stderr, "animator: warning: %s\n", warning.c_str());
        }

        summary_ = QString("%1: %2 %3 from %4 %5")
                       .arg(QFileInfo(path).fileName())
                       .arg(canvas_->cellCount())
                       .arg(mode == chr::TileMode::Size8x16 ? "cells" : "tiles")
                       .arg(table_->entries.size())
                       .arg(table_->entries.size() == 1 ? "entry" : "entries");
        summary_ += frames_.empty()
                        ? QString(" (no frames -- every entry, unnumbered)")
                        : QString(" through %1 frame%2")
                              .arg(frames_.size())
                              .arg(frames_.size() == 1 ? "" : "s");
        if (!table_->warnings.empty()) {
            summary_ += QString(" (%1 warning%2 -- see stderr)")
                            .arg(table_->warnings.size())
                            .arg(table_->warnings.size() == 1 ? "" : "s");
        }
        std::fprintf(stderr, "animator: %s\n", qPrintable(summary_));
        statusBar()->showMessage(summary_);
        return true;
    }

    [[nodiscard]] int sheetWidth() const {
        const int scrollBarPx = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, sheetScroll_);
        const int framePx = 2 * sheetScroll_->frameWidth();
        return kCellsPerRow * chr::kTilePixels * zoom_ + scrollBarPx + framePx;
    }

    void fitToScreen(const QRect& available) {
        const int chromeHeight = menuBar()->sizeHint().height() + statusBar()->sizeHint().height();

        const int wanted = sheetWidth() + metaspriteAreaWidth();
        const int width = std::min(wanted, available.width() - kWindowChromeMarginPx);
        const int height = std::max(kMinWindowHeightPx,
                                    available.height() - kWindowChromeMarginPx - chromeHeight);
        resize(std::max(1, width), height);
    }

    [[nodiscard]] int metaspriteAreaWidth() const {
        const int wanted = tabs_->currentWidget() ? tabs_->currentWidget()->sizeHint().width() : 0;
        const int floorPx = kMetaspriteAreaMinPxAt1080p * zoom_ / kZoomAt1080p;
        return std::max(wanted, floorPx);
    }

    void newMetasprite() {
        auto* page = new MetaspritePage(zoom_, paletteStore_, tabs_);
        connect(page, &MetaspritePage::statusChanged, this, [this, page](const QString& text) {
            if (tabs_->currentWidget() == page) {
                statusBar()->showMessage(text);
            }
        });
        connect(page, &MetaspritePage::framesChanged, this, [this, page] { retitle(page); });
        connect(page, &MetaspritePage::nameChanged, this, [this, page] { retitle(page); });
        connect(page, &MetaspritePage::dirtied, this, &MainWindow::markDirty);
        page->setTileMode(tileMode());

        if (pick_) {
            page->setBrush(&table_->cells[*pick_], pickEntry_);
        }

        page->setName(QString("metasprite_%1").arg(++metaspriteCount_));

        const int index = tabs_->addTab(page, page->name());
        pages_.push_back(page);
        retitle(page);
        tabs_->setCurrentIndex(index);
    }

    MetaspritePage* appendMetasprite() {
        newMetasprite();
        MetaspritePage* page = pages_.empty() ? nullptr : pages_.back();
        if (page) page->clearFrames();
        return page;
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        if (confirmDiscard("closing")) {
            event->accept();
        } else {
            event->ignore();
        }
    }

private:
    void buildMenus() {
        QMenu* file = menuBar()->addMenu("&File");
        QAction* openProject = file->addAction("Open &Project...");
        openProject->setShortcut(QKeySequence("Ctrl+Shift+O"));
        connect(openProject, &QAction::triggered, this, &MainWindow::chooseProject);
        QAction* saveProject = file->addAction("&Save Project");
        saveProject->setShortcut(QKeySequence::Save);
        connect(saveProject, &QAction::triggered, this, [this] { saveProjectTo(projectPath_); });
        QAction* saveProjectAs = file->addAction("Save Project &As...");
        saveProjectAs->setShortcut(QKeySequence::SaveAs);
        connect(saveProjectAs, &QAction::triggered, this, [this] { saveProjectTo(QString()); });
        file->addSeparator();
        exportOne_ = file->addAction("&Export Header...");
        exportOne_->setShortcut(QKeySequence("Ctrl+E"));
        connect(exportOne_, &QAction::triggered, this, &MainWindow::exportCurrent);
        exportAll_ = file->addAction("Export All &Headers...");
        exportAll_->setShortcut(QKeySequence("Ctrl+Shift+E"));
        connect(exportAll_, &QAction::triggered, this, &MainWindow::exportAll);
        file->addSeparator();
        QAction* open = file->addAction("&Open Manifest...");
        open->setShortcut(QKeySequence::Open);
        connect(open, &QAction::triggered, this, &MainWindow::chooseManifest);
        reload_ = file->addAction("&Reload Manifest");
        reload_->setShortcut(QKeySequence::Refresh);
        reload_->setEnabled(false);
        connect(reload_, &QAction::triggered, this, [this] { loadManifest(manifestPath_); });
        file->addSeparator();
        QAction* newMeta = file->addAction("&New Metasprite");
        newMeta->setShortcut(QKeySequence::New);
        connect(newMeta, &QAction::triggered, this, &MainWindow::newMetasprite);
        closeMeta_ = file->addAction("&Close Metasprite");
        closeMeta_->setShortcut(QKeySequence::Close);
        connect(closeMeta_, &QAction::triggered, this, [this] { closeTab(tabs_->currentIndex()); });
        file->addSeparator();
        newFrame_ = file->addAction("New &Frame");
        newFrame_->setShortcut(QKeySequence("Ctrl+T"));
        connect(newFrame_, &QAction::triggered, this, [this] {
            if (MetaspritePage* page = currentPage()) page->newFrame();
        });
        closeFrame_ = file->addAction("Close &Frame");
        closeFrame_->setShortcut(QKeySequence("Ctrl+Shift+W"));
        connect(closeFrame_, &QAction::triggered, this, [this] {
            if (MetaspritePage* page = currentPage()) page->closeFrame();
        });
        file->addSeparator();
        QAction* quit = file->addAction("&Quit");
        quit->setShortcut(QKeySequence::Quit);
        connect(quit, &QAction::triggered, this, &QWidget::close);

        QMenu* edit = menuBar()->addMenu("&Edit");
        clearFrame_ = edit->addAction("Clear &Frame");
        connect(clearFrame_, &QAction::triggered, this, [this] {
            MetaspritePage* page = currentPage();
            if (MetaspriteFrame* frame = page ? page->currentFrame() : nullptr) {
                frame->field()->clearSprites();
            }
        });

        QMenu* window = menuBar()->addMenu("&Window");
        QAction* palettes = window->addAction("&Palettes...");
        connect(palettes, &QAction::triggered, this, &MainWindow::showPalettes);
        frames_action_ = window->addAction("&Frames...");
        connect(frames_action_, &QAction::triggered, this, &MainWindow::showFrames);
        frames_action_->setEnabled(false);

        QMenu* view = menuBar()->addMenu("&View");
        tallTiles_ = view->addAction("&8x16 Tiles");
        tallTiles_->setCheckable(true);
        connect(tallTiles_, &QAction::toggled, this, [this] {
            markDirty();
            if (!manifestPath_.isEmpty()) loadManifest(manifestPath_);
        });

        tabChanged(tabs_->currentIndex());
    }

public:
    void markDirty() {
        if (dirty_) return;
        dirty_ = true;
        refreshTitle();
    }

    void markClean() {
        dirty_ = false;
        refreshTitle();
    }

private:
    void refreshTitle() {
        QString title = "pnes Animation Designer";
        if (!projectPath_.isEmpty()) {
            title += QString(" -- %1").arg(QFileInfo(projectPath_).fileName());
        } else if (!manifestPath_.isEmpty()) {
            title += QString(" -- %1").arg(QFileInfo(manifestPath_).fileName());
        }
        if (dirty_) title += " *";
        setWindowTitle(title);
    }

    bool confirmDiscard(const QString& action) {
        if (!dirty_) return true;

        const auto answer = QMessageBox::warning(
            this, "Unsaved changes",
            QString("This project has unsaved changes to its metasprites, palettes or settings.\n\n"
                    "Save before %1?")
                .arg(action),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);

        if (answer == QMessageBox::Cancel) return false;
        if (answer == QMessageBox::Discard) return true;

        saveProjectTo(projectPath_);
        return !dirty_;
    }

    void showFrames() {
        if (!table_) return;

        std::vector<FrameWindow::Entry> entries;
        entries.reserve(table_->entries.size());
        for (const chr::Entry& entry : table_->entries) {
            entries.push_back({ QString::fromStdString(entry.name), entry.size });
        }

        std::string error;
        auto tiles = chr::loadManifest(manifestPath_.toStdString(), chr::TileMode::Size8x8, error);
        if (!tiles) {
            QMessageBox::warning(this, "Frames",
                                 QString("Cannot read %1 for the preview: %2")
                                     .arg(QFileInfo(manifestPath_).fileName(),
                                          QString::fromStdString(error)));
            return;
        }

        FrameWindow editor(frames_, std::move(entries), *tiles, this);
        if (editor.exec() != QDialog::Accepted) return;

        int moved = 0;
        for (const auto& [from, to] : editor.renames()) {
            for (MetaspritePage* page : pages_) {
                for (MetaspriteFrame* frame : page->frames()) {
                    moved += frame->field()->renameFrame(from, to);
                }
            }
        }

        frames_ = editor.frames();
        markDirty();

        if (!manifestPath_.isEmpty()) loadManifest(manifestPath_);

        if (moved != 0) {
            statusBar()->showMessage(QString("Frames applied -- %1 placed sprite%2 followed a "
                                             "rename")
                                         .arg(moved)
                                         .arg(moved == 1 ? "" : "s"));
        }
    }

    void showPalettes() {
        if (!paletteWindow_) {
            paletteWindow_ = new PaletteWindow(paletteStore_, this);
        }
        paletteWindow_->show();
        paletteWindow_->raise();
        paletteWindow_->activateWindow();
    }

    void refreshSheetPalettes() {
        const QString wanted = sheetPalette_->currentIndex() <= 0 ? QString()
                                                                  : sheetPalette_->currentText();
        {
            const QSignalBlocker blocked(sheetPalette_);
            sheetPalette_->clear();
            sheetPalette_->addItem("(placeholder)");
            sheetPalette_->addItems(paletteStore_->names());

            const int index = wanted.isEmpty() ? 0 : sheetPalette_->findText(wanted);
            sheetPalette_->setCurrentIndex(index >= 0 ? index : 0);
        }
        applySheetPalette();
    }

    void applySheetPalette() {
        const QString name =
            sheetPalette_->currentIndex() <= 0 ? QString() : sheetPalette_->currentText();
        canvas_->setPaletteTable(paletteTable(name.isEmpty() ? nullptr : paletteStore_->find(name)));
    }

    [[nodiscard]] MetaspritePage* currentPage() const {
        return qobject_cast<MetaspritePage*>(tabs_->currentWidget());
    }

    [[nodiscard]] chr::TileMode tileMode() const {
        return tallTiles_->isChecked() ? chr::TileMode::Size8x16 : chr::TileMode::Size8x8;
    }

    void closeTab(int index) {
        QWidget* widget = tabs_->widget(index);
        if (!widget) return;
        if (auto* page = qobject_cast<MetaspritePage*>(widget)) {
            if (!replacing_ && page->hasContent()) {
                const auto answer = QMessageBox::question(
                    this, "Close metasprite",
                    QString("Close '%1'? Its %2 frame%3 and their sprites will be discarded.")
                        .arg(page->name())
                        .arg(page->frameCount())
                        .arg(page->frameCount() == 1 ? "" : "s"),
                    QMessageBox::Close | QMessageBox::Cancel, QMessageBox::Cancel);
                if (answer != QMessageBox::Close) return;
            }
            markDirty();
            pages_.erase(std::remove(pages_.begin(), pages_.end(), page), pages_.end());
        }
        tabs_->removeTab(index);
        widget->deleteLater();
    }

    void tabChanged(int) {
        if (!closeMeta_ || !clearFrame_) return;

        MetaspritePage* page = currentPage();
        closeMeta_->setEnabled(page != nullptr);
        newFrame_->setEnabled(page != nullptr);
        exportOne_->setEnabled(page != nullptr);
        exportAll_->setEnabled(!pages_.empty());
        closeFrame_->setEnabled(page && page->currentFrame() != nullptr);
        clearFrame_->setEnabled(page && page->currentFrame() != nullptr);
        statusBar()->showMessage(page ? page->summary() : summary_);
    }

    void retitle(MetaspritePage* page) {
        const int index = tabs_->indexOf(page);
        if (index < 0) return;
        const QString name = page->name().trimmed();
        tabs_->setTabText(index, QString("%1 (%2)")
                                     .arg(name.isEmpty() ? QString("metasprite") : name)
                                     .arg(page->frameCount()));
        closeFrame_->setEnabled(page->currentFrame() != nullptr);
        clearFrame_->setEnabled(page->currentFrame() != nullptr);
    }

    void pickCell(int index) {
        if (!table_ || index < 0 || static_cast<std::size_t>(index) >= table_->cells.size()) return;

        const chr::Cell&  cell  = table_->cells[static_cast<std::size_t>(index)];
        const chr::Entry& entry = table_->entries[cell.entry];

        if (!cell.numbered()) {
            statusBar()->showMessage(
                QString("%1::%2 has no id -- no frame reaches it. Open a project, or add a "
                        "frame with a window for '%1' (Window -> Frames...), to stamp it.")
                    .arg(QString::fromStdString(entry.name),
                         QString::fromStdString(cell.include)));
            return;
        }

        pick_ = index;
        pickEntry_ = QString::fromStdString(entry.name);

        for (MetaspritePage* page : pages_) {
            page->setBrush(&cell, pickEntry_);
        }
        statusBar()->showMessage(QString("Picked %1::%2 tile %3  id %4%5")
                                     .arg(pickEntry_, QString::fromStdString(cell.include))
                                     .arg(cell.tileInInclude)
                                     .arg(formatCellId(cell), formatCellFrame(cell)));
    }

    void clearPick() {
        pick_.reset();
        pickEntry_.clear();
        canvas_->setSelected(-1);
        for (MetaspritePage* page : pages_) {
            page->setBrush(nullptr, QString());
        }
    }

    [[nodiscard]] const chr::Cell* findCell(const std::string& frame, const std::string& entry,
                                            const std::string& include, int tile, int id) const {
        return table_ ? proj::findCell(*table_, frame, entry, include, tile, id) : nullptr;
    }

    static QString formatCellFrame(const chr::Cell& cell) {
        return cell.frame.empty()
            ? QString()
            : QString("  (frame %1, window %2)")
                  .arg(QString::fromStdString(cell.frame))
                  .arg(cell.window);
    }

    static QString formatCellId(const chr::Cell& cell) {
        if (!cell.numbered()) return QString("unnumbered");

        return cell.heightTiles == 2
            ? QString("$%1-$%2, OAM $%3")
                  .arg(cell.id, 2, 16, QChar('0'))
                  .arg(cell.id + 1, 2, 16, QChar('0'))
                  .arg(cell.patternByte, 2, 16, QChar('0'))
            : QString("$%1").arg(cell.id, 2, 16, QChar('0'));
    }

    void showCell(int index) {
        if (!table_ || index < 0 || static_cast<std::size_t>(index) >= table_->cells.size()) {
            statusBar()->showMessage(summary_);
            return;
        }

        const chr::Cell&  cell  = table_->cells[static_cast<std::size_t>(index)];
        const chr::Entry& entry = table_->entries[cell.entry];
        statusBar()->showMessage(QString("%1::%2  tile %3  id %4  (%5x%6)%7")
                                     .arg(QString::fromStdString(entry.name),
                                          QString::fromStdString(cell.include))
                                     .arg(cell.tileInInclude)
                                     .arg(formatCellId(cell))
                                     .arg(cell.widthPx())
                                     .arg(cell.heightPx())
                                     .arg(formatCellFrame(cell)));
    }

public:

    [[nodiscard]] proj::Project buildProject(const QString& path) const {
        proj::Project project;
        project.path = path.toStdString();
        project.mode = tileMode();
        project.metaspritesDir = metaspritesDir_;
        project.frames = frames_;

        std::error_code ec;
        std::filesystem::path base = project.path.parent_path();
        if (base.empty()) base = ".";
        const auto relative = std::filesystem::relative(
            std::filesystem::absolute(manifestPath_.toStdString()), std::filesystem::absolute(base), ec);
        project.chrManifest = (ec || relative.empty()) ? manifestPath_.toStdString() : relative.generic_string();

        project.palettes = paletteStore_->palettes();

        for (const MetaspritePage* page : pages_) {
            project.metasprites.push_back(buildMetasprite(page));
        }

        return project;
    }

    [[nodiscard]] proj::Metasprite buildMetasprite(const MetaspritePage* page) const {
        proj::Metasprite metasprite;
        metasprite.palette = page->paletteName().toStdString();
        metasprite.name      = page->name().toStdString();
        metasprite.savedStem = page->savedStem().toStdString();
        metasprite.tilesX = page->tilesX();
        metasprite.tilesY = page->tilesY();

        for (const MetaspriteFrame* frame : page->frames()) {
            proj::Frame out;
            out.name  = frame->frameName().toStdString();
            out.timer = frame->timerFrames();
            out.isPublic = frame->isPublicFrame();

            for (const PlacedSprite& sprite : frame->field()->sprites()) {
                proj::Sprite saved;
                saved.col     = sprite.col;
                saved.row     = sprite.row;
                saved.id      = sprite.id;
                saved.patternByte = sprite.patternByte;
                saved.frame   = sprite.frame.toStdString();
                saved.entry   = sprite.entry.toStdString();
                saved.include = sprite.include.toStdString();
                saved.tile    = sprite.tile;
                saved.flipH   = static_cast<int>(sprite.mirror) &
                                static_cast<int>(Mirror::Horizontal);
                saved.flipV   = static_cast<int>(sprite.mirror) &
                                static_cast<int>(Mirror::Vertical);
                out.sprites.push_back(std::move(saved));
            }

            metasprite.frames.push_back(std::move(out));
        }

        return metasprite;
    }

    void saveProjectTo(QString path) {
        if (manifestPath_.isEmpty()) {
            QMessageBox::warning(this, "Save Project",
                                 "Open a blobman manifest first -- a project records which one its "
                                 "tiles come from.");
            return;
        }

        if (path.isEmpty()) {
            path = QFileDialog::getSaveFileName(
                this, "Save Project", QDir::currentPath(),
                QString("Animator projects (*%1);;All files (*)").arg(proj::kProjectExtension));
            if (path.isEmpty()) return;
            if (!path.endsWith(proj::kProjectExtension)) path += proj::kProjectExtension;
        }

        proj::Project project = buildProject(path);
        std::string error;
        if (!proj::save(project, error)) {
            const QString message = QString("Cannot save %1: %2")
                                        .arg(QFileInfo(path).fileName(), QString::fromStdString(error));
            std::fprintf(stderr, "animator: %s\n", qPrintable(message));
            QMessageBox::warning(this, "Save Project", message);
            return;
        }

        for (std::size_t i = 0; i < pages_.size() && i < project.metasprites.size(); ++i) {
            pages_[i]->setSavedStem(QString::fromStdString(project.metasprites[i].savedStem));
        }

        projectPath_ = path;
        markClean();
        statusBar()->showMessage(QString("Saved %1 (%2 metasprite%3) beside %4/")
                                     .arg(QFileInfo(path).fileName())
                                     .arg(pages_.size())
                                     .arg(pages_.size() == 1 ? "" : "s")
                                     .arg(QString::fromStdString(metaspritesDir_)));
    }


    [[nodiscard]] QString exportDir() const {
        const QString base = projectPath_.isEmpty() ? QDir::currentPath()
                                                    : QFileInfo(projectPath_).absolutePath();
        const QString gen = QDir(base).filePath("gen");
        return QFileInfo(gen).isDir() ? gen : base;
    }

    void exportCurrent() {
        MetaspritePage* page = currentPage();
        if (!page) {
            QMessageBox::warning(this, "Export Header", "There is no metasprite to export.");
            return;
        }
        if (page->frameCount() == 0) {
            QMessageBox::warning(this, "Export Header",
                                 QString("'%1' has no frames -- there is no animation to export.")
                                     .arg(page->name()));
            return;
        }

        const proj::Metasprite metasprite = buildMetasprite(page);
        const QString suggested = QDir(exportDir()).filePath(QString::fromStdString(
            codegen::identifier(metasprite.name) + codegen::kHeaderExtension));

        QString path = QFileDialog::getSaveFileName(
            this, "Export Header", suggested,
            QString("C++ headers (*%1);;All files (*)").arg(codegen::kHeaderExtension));
        if (path.isEmpty()) return;
        if (!path.endsWith(codegen::kHeaderExtension)) path += codegen::kHeaderExtension;

        std::string error;
        const std::string source =
            projectPath_.isEmpty() ? std::string() : QFileInfo(projectPath_).fileName().toStdString();
        if (!codegen::writeHeader(metasprite, path.toStdString(), source, error)) {
            const QString message = QString("Cannot export %1: %2")
                                        .arg(QFileInfo(path).fileName(), QString::fromStdString(error));
            std::fprintf(stderr, "animator: %s\n", qPrintable(message));
            QMessageBox::warning(this, "Export Header", message);
            return;
        }

        statusBar()->showMessage(QString("Exported %1 (%2 frame%3 of %4)")
                                     .arg(QFileInfo(path).fileName())
                                     .arg(page->frameCount())
                                     .arg(page->frameCount() == 1 ? "" : "s")
                                     .arg(page->name()));
    }

    void exportAll() {
        if (pages_.empty()) {
            QMessageBox::warning(this, "Export All Headers", "There are no metasprites to export.");
            return;
        }

        const QString dir = QFileDialog::getExistingDirectory(this, "Export All Headers", exportDir());
        if (dir.isEmpty()) return;

        const proj::Project project = buildProject(projectPath_);
        std::vector<std::string> skipped;
        std::string error;
        if (!codegen::writeAll(project, dir.toStdString(), skipped, error)) {
            const QString message = QString("Cannot export into %1: %2")
                                        .arg(QDir(dir).dirName(), QString::fromStdString(error));
            std::fprintf(stderr, "animator: %s\n", qPrintable(message));
            QMessageBox::warning(this, "Export All Headers", message);
            return;
        }

        for (const std::string& note : skipped) {
            std::fprintf(stderr, "animator: warning: %s\n", note.c_str());
        }

        const std::size_t written = project.metasprites.size() - skipped.size();
        QString message = QString("Exported %1 header%2 into %3/")
                              .arg(written)
                              .arg(written == 1 ? "" : "s")
                              .arg(QDir(dir).dirName());
        if (!skipped.empty()) {
            message += QString(" (%1 skipped -- see stderr)").arg(skipped.size());
        }
        statusBar()->showMessage(message);
    }

    void chooseProject() {
        if (!confirmDiscard("opening another project")) return;
        const QString path = QFileDialog::getOpenFileName(
            this, "Open Project", QDir::currentPath(),
            QString("Animator projects (*%1);;All files (*)").arg(proj::kProjectExtension));
        if (!path.isEmpty()) openProject(path);
    }

    bool openProject(const QString& path) {
        std::string error;
        const auto project = proj::load(path.toStdString(), error);
        if (!project) {
            const QString message = QString("Cannot open %1: %2")
                                        .arg(QFileInfo(path).fileName(), QString::fromStdString(error));
            std::fprintf(stderr, "animator: %s\n", qPrintable(message));
            QMessageBox::warning(this, "Open Project", message);
            return false;
        }

        for (const std::string& warning : project->warnings) {
            std::fprintf(stderr, "animator: warning: %s\n", warning.c_str());
        }

        {
            const QSignalBlocker blocked(tallTiles_);
            tallTiles_->setChecked(project->mode == chr::TileMode::Size8x16);
        }

        std::vector<chr::Frame> previousFrames = frames_;
        frames_ = project->frames;

        const auto manifest = proj::resolve(*project, project->chrManifest);
        if (!loadManifest(QString::fromStdString(manifest.string()))) {
            frames_ = std::move(previousFrames);
            return false;
        }

        replacing_ = true;
        while (tabs_->count() > 0) {
            closeTab(0);
        }
        replacing_ = false;
        paletteStore_->setPalettes(project->palettes);
        metaspriteCount_ = 0;
        metaspritesDir_ = project->metaspritesDir;
        projectPath_ = path;

        int missing = 0;
        for (const proj::Metasprite& metasprite : project->metasprites) {
            MetaspritePage* page = appendMetasprite();
            if (!page) continue;
            page->setName(QString::fromStdString(metasprite.name));
            page->setPaletteName(QString::fromStdString(metasprite.palette));
            page->setSavedStem(QString::fromStdString(metasprite.savedStem));
            page->setSize(metasprite.tilesX, metasprite.tilesY);

            for (const proj::Frame& frame : metasprite.frames) {
                MetaspriteFrame* built = page->appendFrame();
                if (!built) continue;
                built->setFrameName(QString::fromStdString(frame.name));
                built->setTimerFrames(frame.timer);
                built->setPublicFrame(frame.isPublic);

                for (const proj::Sprite& sprite : frame.sprites) {
                    const chr::Cell* cell = findCell(sprite.frame, sprite.entry, sprite.include,
                                                     sprite.tile, sprite.id);
                    if (!cell) {
                        ++missing;
                        continue;
                    }
                    const auto mirror = static_cast<Mirror>(
                        (sprite.flipH ? static_cast<int>(Mirror::Horizontal) : 0) |
                        (sprite.flipV ? static_cast<int>(Mirror::Vertical) : 0));
                    if (!built->field()->addSprite(*cell, QString::fromStdString(sprite.entry),
                                                   sprite.col, sprite.row, mirror)) {
                        ++missing;
                    }
                }
            }

            if (page->frameCount() == 0) page->newFrame();  // never leave a metasprite frameless
        }

        if (tabs_->count() > 0) tabs_->setCurrentIndex(0);

        markClean();
        QString message = QString("Opened %1: %2 metasprite%3")
                              .arg(QFileInfo(path).fileName())
                              .arg(project->metasprites.size())
                              .arg(project->metasprites.size() == 1 ? "" : "s");
        if (missing != 0) {
            message += QString(" -- %1 sprite%2 could not be placed (see stderr)")
                           .arg(missing)
                           .arg(missing == 1 ? "" : "s");
            std::fprintf(stderr,
                         "animator: warning: %d sprite(s) named art or slots this manifest and "
                         "extent do not have\n", missing);
        }
        if (!project->warnings.empty()) {
            message += QString(" (%1 file warning%2 -- see stderr)")
                           .arg(project->warnings.size())
                           .arg(project->warnings.size() == 1 ? "" : "s");
        }
        statusBar()->showMessage(message);
        return true;
    }

private:
    void chooseManifest() {
        const QString start =
            manifestPath_.isEmpty() ? QDir::currentPath() : QFileInfo(manifestPath_).absolutePath();
        const QString path = QFileDialog::getOpenFileName(this, "Open Manifest", start,
                                                          "blobman manifests (*.toml);;All files (*)");
        if (!path.isEmpty()) {
            loadManifest(path);
        }
    }

    int                           zoom_;
    QTabWidget*                   tabs_ = nullptr;
    QComboBox*                    sheetPalette_ = nullptr;
    PaletteStore*                 paletteStore_ = new PaletteStore(this);
    PaletteWindow*                paletteWindow_ = nullptr;
    QScrollArea*                  sheetScroll_ = nullptr;
    TileCanvas*                   canvas_ = nullptr;
    QAction*                      reload_ = nullptr;
    QAction*                      closeMeta_ = nullptr;
    QAction*                      newFrame_ = nullptr;
    QAction*                      closeFrame_ = nullptr;
    QAction*                      clearFrame_ = nullptr;
    QAction*                      exportOne_ = nullptr;
    QAction*                      exportAll_ = nullptr;
    QAction*                      tallTiles_ = nullptr;
    QAction*                      frames_action_ = nullptr;

    std::vector<chr::Frame>       frames_;
    std::optional<chr::Table>     table_;
    std::vector<MetaspritePage*>  pages_;
    std::optional<int>            pick_;        ///< Picked cell index into table_->cells.
    QString                       pickEntry_;   ///< blobman entry the pick belongs to.
    QString                       projectPath_; ///< The .anim being edited, if any.
    std::string                   metaspritesDir_ = proj::kDefaultMetaspritesDir;
    int                           metaspriteCount_ = 0;
    bool                          dirty_ = false;
    bool                          replacing_ = false;  ///< True while a project load swaps the tabs out.
    QString                       manifestPath_;
    QString                       summary_ = "No manifest loaded";
};

} // namespace

int main(int argc, char** argv) {
    {
        static constexpr const char* kGuiUsage =
            "  -t, --tall         open a bare manifest with 8x16 tile grouping\n"
            "\n"
            "Without -E/--export-all the designer opens, on the project or "
            "manifest given.\n";
        exporter::Options options;
        switch (exporter::parseArgs(argc, argv, options, /*exportImplied=*/false, kGuiUsage)) {
            case exporter::Parse::Export: return exporter::run(options);
            case exporter::Parse::Help:   return 0;
            case exporter::Parse::Error:  return 1;
            case exporter::Parse::NoExport: break;  // open the window as usual
        }
    }

    QApplication app(argc, argv);

    bool tall = false;
    QString file;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "-t" || arg == "--tall") {
            tall = true;
        } else if (file.isEmpty()) {
            file = arg;
        } else {
            std::fprintf(stderr, "animator: unexpected argument '%s'\n", qPrintable(arg));
            return 1;
        }
    }
    const bool isProject = file.endsWith(proj::kProjectExtension, Qt::CaseInsensitive);

    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = app.primaryScreen();
    }
    const QRect geometry = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
    const QRect available = screen ? screen->availableGeometry() : geometry;

    MainWindow window(zoomForScreen(geometry));
    if (!isProject) window.setTallTiles(tall);
    if (!file.isEmpty() && !isProject) {
        window.loadManifest(file, /*quiet=*/true);
    }
    if (isProject) {
        window.openProject(file);
    } else {
        window.newMetasprite();
    }
    window.markClean();
    window.fitToScreen(available);
    window.show();

    return app.exec();
}

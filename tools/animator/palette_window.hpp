/**
 * @file palette_window.hpp
 * @brief The palette editor: a separate window listing the project palettes.
 */
#pragma once

#include "palette_store.hpp"

#include <QDialog>

class QListWidget;
class QLineEdit;
class QPushButton;
class QToolButton;

class PaletteWindow final : public QDialog {
    Q_OBJECT

public:
    PaletteWindow(PaletteStore* store, QWidget* parent = nullptr);

private:
    void refreshList();

    void refreshEditor();

    [[nodiscard]] QString selectedName() const;

    void pickColour(int slot);

    PaletteStore* store_;
    QListWidget*  list_ = nullptr;
    QLineEdit*    name_ = nullptr;
    QToolButton*  swatches_[pal::kColoursPerPalette] = {};
    QPushButton*  remove_ = nullptr;
};

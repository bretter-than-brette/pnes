/**
 * @file frame_window.hpp
 * @brief The frame editor: the dialog that authors a project chr::Frame list.
 */
#pragma once

#include "chr_table.hpp"

#include <QDialog>
#include <QString>

#include <cstdint>
#include <utility>
#include <vector>

class FramePreview;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QWidget;

class FrameWindow final : public QDialog {
    Q_OBJECT

public:
    struct Entry {
        QString       name;
        std::uint32_t size = 0;  ///< Bytes the entry reserves.
    };

    FrameWindow(std::vector<chr::Frame> frames, std::vector<Entry> entries,
                const chr::Table& tiles, QWidget* parent = nullptr);

    [[nodiscard]] const std::vector<chr::Frame>& frames() const { return frames_; }

    [[nodiscard]] const std::vector<std::pair<QString, QString>>& renames() const {
        return renames_;
    }

private:
    static const std::vector<int>& windowCounts();

    void refreshList();

    void refreshEditor();

    void refreshWindows();

    [[nodiscard]] int selected() const;

    [[nodiscard]] QString firstProblem() const;

    void addFrame();
    void removeFrame();

    std::vector<chr::Frame> frames_;
    std::vector<Entry>      entries_;

    std::vector<QString>    original_;

    std::vector<std::pair<QString, QString>> renames_;

    QListWidget*  list_ = nullptr;
    QLineEdit*    name_ = nullptr;
    QComboBox*    table_ = nullptr;
    QComboBox*    count_ = nullptr;
    QWidget*      windowBox_ = nullptr;
    QGridLayout*  windowRows_ = nullptr;
    QLabel*       note_ = nullptr;
    QPushButton*  remove_ = nullptr;
    FramePreview* preview_ = nullptr;

    bool loading_ = false;
};

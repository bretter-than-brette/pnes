/**
 * @file palette_store.hpp
 * @brief The project palettes, and who to tell when they change.
 */
#pragma once

#include "palette.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

class PaletteStore final : public QObject {
    Q_OBJECT

public:
    explicit PaletteStore(QObject* parent = nullptr) : QObject(parent) {}

    [[nodiscard]] const std::vector<pal::Palette>& palettes() const { return palettes_; }

    [[nodiscard]] const pal::Palette* find(const QString& name) const;

    [[nodiscard]] QStringList names() const;

    QString add(const QString& name);

    void remove(const QString& name);

    bool rename(const QString& from, const QString& to);

    void setColour(const QString& name, int slot, std::uint8_t nesIndex);

    void setPalettes(std::vector<pal::Palette> palettes);

signals:
    void changed();

private:
    [[nodiscard]] pal::Palette* mutableFind(const QString& name);

    std::vector<pal::Palette> palettes_;
};

#include "project.hpp"

#include <toml++/toml.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <ios>
#include <sstream>
#include <system_error>

namespace proj {
namespace {

std::string quote(const std::string& text) {
    std::string out = "\"";
    for (const char ch : text) {
        if (ch == '"' || ch == '\\') out += '\\';
        out += ch;
    }
    out += '"';
    return out;
}

std::string hex2(int value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%02x", value);
    return text;
}

std::string hexAddr(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%04x", value);
    return text;
}

const char* modeName(const chr::TileMode mode) {
    return mode == chr::TileMode::Size8x16 ? "8x16" : "8x8";
}

bool writeFile(const std::filesystem::path& path, const std::string& contents, std::string& error) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        error = "could not open for writing: " + path.string();
        return false;
    }
    out << contents;
    out.close();
    if (!out) {
        error = "failed writing: " + path.string();
        return false;
    }
    return true;
}

std::string metaspriteToml(const Metasprite& metasprite) {
    std::ostringstream toml;
    toml << "# pnes animator metasprite -- see the .anim beside this folder\n\n"
            "[metasprite]\n"
            "name   = " << quote(metasprite.name) << "\n"
            "palette = " << quote(metasprite.palette)
         << "    # a [[palette]] in the .anim; empty for the placeholder colours\n"
            "width  = " << metasprite.tilesX
         << "    # in tiles; a tile is 8x8 or 8x16 by the project's tile_mode\n"
            "height = " << metasprite.tilesY << "\n";

    for (const Frame& frame : metasprite.frames) {
        toml << "\n[[frame]]\n"
                "name  = " << quote(frame.name) << "\n"
                "timer = " << frame.timer;
        if (frame.timer == 0) toml << "    # 0 -- the animation stops on this frame";
        toml << "\n";
        if (frame.isPublic) {
            toml << "public = true    # name this frame's bytes in the generated header\n";
        }

        if (frame.sprites.empty()) {
            toml << "sprites = []\n";
            continue;
        }

        toml << "sprites = [\n";
        for (const Sprite& sprite : frame.sprites) {
            toml << "    { col = " << sprite.col
                 << ", row = " << sprite.row
                 << ", id = " << hex2(sprite.id)
                 << ", pattern = " << hex2(sprite.patternByte)
                 << ", frame = " << quote(sprite.frame)
                 << ", entry = " << quote(sprite.entry)
                 << ", include = " << quote(sprite.include)
                 << ", tile = " << sprite.tile
                 << ", flip_h = " << (sprite.flipH ? "true" : "false")
                 << ", flip_v = " << (sprite.flipV ? "true" : "false")
                 << " },\n";
        }
        toml << "]\n";
    }

    return toml.str();
}

} // namespace

std::string fileStem(const std::string& name) {
    std::string stem;
    stem.reserve(name.size());
    for (const char ch : name) {
        stem += (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-') ? ch : '_';
    }
    if (stem.empty()) stem = "metasprite";
    if (std::isdigit(static_cast<unsigned char>(stem.front()))) stem.insert(stem.begin(), '_');
    return stem;
}

std::filesystem::path resolve(const Project& project, const std::string& relative) {
    const std::filesystem::path given(relative);
    if (given.is_absolute()) return given;

    std::filesystem::path base = project.path.parent_path();
    if (base.empty()) base = ".";
    return base / given;
}

bool save(Project& project, std::string& error) {
    const std::filesystem::path sidecar = resolve(project, project.metaspritesDir);

    std::error_code ec;
    std::filesystem::create_directories(sidecar, ec);
    if (ec) {
        error = "could not create the metasprites folder " + sidecar.string() + ": " + ec.message();
        return false;
    }

    std::vector<std::string> stems;
    stems.reserve(project.metasprites.size());
    for (const Metasprite& metasprite : project.metasprites) {
        const std::string stem = fileStem(metasprite.name);
        for (const std::string& taken : stems) {
            if (taken == stem) {
                error = "two metasprites both save as '" + stem + kMetaspriteExtension +
                        "' -- rename one of them";
                return false;
            }
        }
        stems.push_back(stem);
    }

    std::ostringstream anim;
    anim << "# pnes animator project\n\n"
            "[project]\n"
            "# The blobman manifest the tiles come from, relative to this file.\n"
            "chr         = " << quote(project.chrManifest) << "\n"
            "# Sidecar folder holding the .msprite manifests, relative to this file.\n"
            "metasprites = " << quote(project.metaspritesDir) << "\n"
            "# How tiles were grouped when this was authored: \"8x8\" or \"8x16\".\n"
            "# One tile slot of a metasprite is 8x8 or 8x16 accordingly.\n"
            "tile_mode   = " << quote(modeName(project.mode)) << "\n"
            "# The metasprites of this project, in order. Each names a file in\n"
            "# the folder above.\n"
            "files       = [\n";
    for (const std::string& stem : stems) {
        anim << "    " << quote(stem + kMetaspriteExtension) << ",\n";
    }
    anim << "]\n";

    for (const pal::Palette& palette : project.palettes) {
        anim << "\n[[palette]]\n"
                "name    = " << quote(palette.name) << "\n"
                "colours = [ "
             << hex2(palette.colours[0]) << ", "
             << hex2(palette.colours[1]) << ", "
             << hex2(palette.colours[2])
             << " ]    # NES master-palette indices for colours 1-3\n";
    }

    for (const chr::Frame& frame : project.frames) {
        anim << "\n[[frame]]\n"
                "# The tile-id space (0x00-0xff) sprites of this project draw from.\n"
                "# The windows are equally sized -- 256 / however many there are --\n"
                "# and their ORDER is what gives a tile its id. An empty name\n"
                "# reserves a window's ids for whatever else the game banks there.\n"
                "name          = " << quote(frame.name) << "\n"
                "pattern_table = " << hexAddr(frame.patternTable)
             << "    # 0x0000 or 0x1000; bit 0 of an 8x16 pattern byte\n"
                "windows       = [\n";
        for (const std::string& window : frame.windows) {
            anim << "    " << quote(window) << ",\n";
        }
        anim << "]\n";
    }

    std::vector<std::string> previous;
    previous.reserve(project.metasprites.size());
    for (const Metasprite& metasprite : project.metasprites) {
        previous.push_back(metasprite.savedStem);
    }

    for (std::size_t i = 0; i < project.metasprites.size(); ++i) {
        const std::filesystem::path file = sidecar / (stems[i] + kMetaspriteExtension);
        if (!writeFile(file, metaspriteToml(project.metasprites[i]), error)) return false;
    }

    for (std::size_t i = 0; i < previous.size(); ++i) {
        if (previous[i].empty() || previous[i] == stems[i]) continue;
        if (std::find(stems.begin(), stems.end(), previous[i]) != stems.end()) continue;

        const std::filesystem::path stale = sidecar / (previous[i] + kMetaspriteExtension);
        std::error_code removeEc;
        if (!std::filesystem::exists(stale, removeEc) || removeEc) continue;  // already gone by hand
        if (!std::filesystem::remove(stale, removeEc) || removeEc) {
            error = "wrote the metasprites, but could not remove the renamed-away " +
                    stale.string() + (removeEc ? ": " + removeEc.message() : "");
            return false;
        }
    }

    for (std::size_t i = 0; i < project.metasprites.size(); ++i) {
        project.metasprites[i].savedStem = stems[i];
    }

    return writeFile(project.path, anim.str(), error);
}

std::optional<Project> load(const std::filesystem::path& path, std::string& error) {
    toml::table config;
    try {
        config = toml::parse_file(path.string());
    } catch (const toml::parse_error& err) {
        std::ostringstream message;
        message << "failed to parse: " << err.description() << " (" << err.source().begin << ")";
        error = message.str();
        return std::nullopt;
    }

    const auto table = config["project"].as_table();
    if (!table) {
        error = "requires a [project] table";
        return std::nullopt;
    }

    Project project;
    project.path = path;
    project.chrManifest = (*table)["chr"].value_or<std::string>("");
    project.metaspritesDir = (*table)["metasprites"].value_or<std::string>(kDefaultMetaspritesDir);
    project.mode = (*table)["tile_mode"].value_or<std::string>("8x8") == "8x16"
                       ? chr::TileMode::Size8x16
                       : chr::TileMode::Size8x8;

    if (project.chrManifest.empty()) {
        error = "[project] has no 'chr' path to a blobman manifest";
        return std::nullopt;
    }

    if (const auto palettes = config["palette"].as_array()) {
        for (const auto& node : *palettes) {
            const auto palette = node.as_table();
            if (!palette) continue;

            pal::Palette loaded;
            loaded.name = (*palette)["name"].value_or<std::string>("palette");
            if (const auto colours = (*palette)["colours"].as_array()) {
                for (std::size_t i = 0; i < loaded.colours.size() && i < colours->size(); ++i) {
                    loaded.colours[i] = static_cast<std::uint8_t>(
                        colours->get(i)->value_or<std::int64_t>(pal::kTransparencyColour) & 0x3F);
                }
            }
            project.palettes.push_back(std::move(loaded));
        }
    }

    if (const auto frames = config["frame"].as_array()) {
        for (const auto& node : *frames) {
            const auto frameTable = node.as_table();
            if (!frameTable) continue;

            chr::Frame frame;
            frame.name = (*frameTable)["name"].value_or<std::string>("");
            frame.patternTable = static_cast<std::uint32_t>(
                (*frameTable)["pattern_table"].value_or<std::int64_t>(0));
            if (const auto windows = (*frameTable)["windows"].as_array()) {
                for (const auto& window : *windows) {
                    const auto name = window.value<std::string>();
                    if (!name) {
                        project.warnings.push_back(
                            "frame '" + frame.name +
                            "' has a window that is not an entry name; read as an empty window");
                        frame.windows.emplace_back();
                        continue;
                    }
                    frame.windows.push_back(*name);
                }
            }
            project.frames.push_back(std::move(frame));
        }
    }

    const auto files = (*table)["files"].as_array();
    if (!files) return project;  // a project with no metasprites yet is fine

    const std::filesystem::path sidecar = resolve(project, project.metaspritesDir);

    for (const auto& node : *files) {
        const auto name = node.value<std::string>();
        if (!name) {
            project.warnings.push_back("[project] files must all be filenames; skipped one that is not");
            continue;
        }

        const std::filesystem::path file = sidecar / *name;
        toml::table doc;
        try {
            doc = toml::parse_file(file.string());
        } catch (const toml::parse_error& err) {
            project.warnings.push_back(*name + ": " + std::string(err.description()) + " -- skipped");
            continue;
        }

        const auto head = doc["metasprite"].as_table();
        if (!head) {
            project.warnings.push_back(*name + ": no [metasprite] table -- skipped");
            continue;
        }

        Metasprite metasprite;
        metasprite.name   = (*head)["name"].value_or<std::string>(std::filesystem::path(*name).stem().string());
        metasprite.savedStem = std::filesystem::path(*name).stem().string();
        metasprite.palette = (*head)["palette"].value_or<std::string>("");
        metasprite.tilesX = static_cast<int>((*head)["width"].value_or<std::int64_t>(1));
        metasprite.tilesY = static_cast<int>((*head)["height"].value_or<std::int64_t>(1));

        if (const auto frames = doc["frame"].as_array()) {
            for (const auto& frameNode : *frames) {
                const auto frameTable = frameNode.as_table();
                if (!frameTable) continue;

                Frame frame;
                frame.name  = (*frameTable)["name"].value_or<std::string>("frame");
                frame.timer = static_cast<int>((*frameTable)["timer"].value_or<std::int64_t>(0));
                frame.isPublic = (*frameTable)["public"].value_or(false);

                if (const auto sprites = (*frameTable)["sprites"].as_array()) {
                    for (const auto& spriteNode : *sprites) {
                        const auto spriteTable = spriteNode.as_table();
                        if (!spriteTable) continue;

                        Sprite sprite;
                        sprite.col     = static_cast<int>((*spriteTable)["col"].value_or<std::int64_t>(0));
                        sprite.row     = static_cast<int>((*spriteTable)["row"].value_or<std::int64_t>(0));
                        sprite.id      = static_cast<int>((*spriteTable)["id"].value_or<std::int64_t>(0));
                        sprite.patternByte = static_cast<int>(
                            (*spriteTable)["pattern"].value_or<std::int64_t>(sprite.id));
                        sprite.frame   = (*spriteTable)["frame"].value_or<std::string>(
                            project.frames.empty() ? std::string() : project.frames.front().name);
                        sprite.entry   = (*spriteTable)["entry"].value_or<std::string>("");
                        sprite.include = (*spriteTable)["include"].value_or<std::string>("");
                        sprite.tile    = static_cast<int>((*spriteTable)["tile"].value_or<std::int64_t>(0));
                        sprite.flipH   = (*spriteTable)["flip_h"].value_or(false);
                        sprite.flipV   = (*spriteTable)["flip_v"].value_or(false);
                        frame.sprites.push_back(std::move(sprite));
                    }
                }

                metasprite.frames.push_back(std::move(frame));
            }
        }

        project.metasprites.push_back(std::move(metasprite));
    }

    return project;
}

const chr::Cell* findCell(const chr::Table& table, const std::string& frame,
                          const std::string& entry, const std::string& include, int tile, int id) {
    for (const chr::Cell& cell : table.cells) {
        if (!frame.empty() && cell.frame != frame) continue;
        if (cell.include == include && cell.tileInInclude == tile &&
            table.entries[cell.entry].name == entry) {
            return &cell;
        }
    }
    for (const chr::Cell& cell : table.cells) {
        if (cell.id == id) return &cell;
    }
    return nullptr;
}

} // namespace proj

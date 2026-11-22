// The one TU that embeds the CHR blob into the platform CHR section and defines
// ppu::chrRomBytes beside it, so the count always matches what is embedded.

#include <pnes/video.hpp>

#include <array>
#include <cstddef>

namespace {
    constexpr std::size_t chrRomSize = 0x4000;   // 16 KiB -- two MMC3 CHR pages

    constexpr int chrRomRaw[] = {
#embed "../../chr/chr.bin"
    };

    static_assert(sizeof(chrRomRaw) / sizeof(*chrRomRaw) == chrRomSize,
                  "demo/chr/chr.bin is not chrRomSize bytes -- repack it, or "
                  "update chrRomSize, link.ld's __chr_rom_size and header.hpp "
                  "together");
}


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"

[[gnu::section(_CHR_SECTION), gnu::used, gnu::retain]]
inline constexpr auto chr_rom_image = [] {
    std::array<u8, chrRomSize> image{};
    for (std::size_t i = 0; i < chrRomSize; i++) {
        image[i] = static_cast<u8>(chrRomRaw[i]);
    }
    return image;
}();

[[gnu::used, gnu::retain]]
inline constexpr unsigned ppu::chrRomBytes = chrRomSize;

#pragma GCC diagnostic pop

#pragma once

#include <cstddef>
#include <intsh>

#ifdef TARGET_GC
// card_stat, which GetMetaData/SetMetaData move as void*. Included here so a
// GameCube game can declare one without hunting for it.
#include <ogc/card.h>
#endif

#include "technology.hpp"

using namespace br0::intsh;

namespace save {

#ifdef TARGET_GC
#define OGC_COMMENT(name) char name [32]
#define OGC_ICON_CI(name) u8 name[1536]
#define OGC_BANNER_CI(name) u8 name[3584]
#endif

    enum eError : u8 {
        found,
        missing,
        no_device,
        busy,
        exists,
        full,
        too_large,
        denied,
        bad_path,
        failed,
    };

    /// done says the operation is over, err whether it worked -- so the loop is
    /// `while (!response.done)` and the test `if (!response.err)`.
    struct Response {
        bool done;
        eError err;
    #ifndef TARGET_NES
        /// Borrowed from the backend, valid until the next GetSaveLocation call.
        /// Absent on NES, where a battery cart's save has no name.
        const char* path;
#endif
    };

    Response SetMetaData(const void* src);
    Response GetMetaData(void* tar);
    Response Save(const void* src, std::size_t size);
    Response Load(void* tar, std::size_t size);
    Response Load(void* tar, std::size_t size, const Response* response);
    Response CreateSave(const void* src, std::size_t size, const Response* response);
    Response GetSaveLocation();

    Response Delete();

    /// Bit `n` is the device GetDeviceStatus takes index `n` for. A clear bit
    /// does not say why, including "not settled yet", so poll a few times a
    /// second rather than every frame.
    u8 AccessibleDevices();

    /// no_device covers empty, unusable and past-the-last-index alike; busy with
    /// done false means ask again next frame. Being broken is only discoverable
    /// by mounting, so a slot reads usable until something tries.
    Response GetDeviceStatus(u8 index);

    /// AccessibleDevices narrowed by capacity; a device already holding a big
    /// enough save counts as free. Free space can only be mounted for, so this is
    /// a sweep: zero means "nowhere, or not yet".
    u8 FreeDevices(std::size_t size);
}
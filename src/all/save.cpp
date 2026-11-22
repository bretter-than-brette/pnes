/**
 * @file save.cpp
 * @brief Weak "no save device here" answers, overridden by any real backend.
 *
 * save.hpp declares the API on every target so a game needs no #ifdef; without
 * these a target with no backend would fail at link. Weak rather than an #ifdef
 * chain, so adding a backend means writing it, not editing this file. Every
 * answer is no_device with done true -- missing would invite a game to offer to
 * create a save we would then refuse.
 */
#include <pnes/save.hpp>

namespace save {

__attribute__((weak)) Response Save(const void*, const std::size_t) { return { .done = true, .err = no_device }; }
__attribute__((weak)) Response Load(void*, const std::size_t) { return { .done = true, .err = no_device }; }
__attribute__((weak)) Response Load(void*, const std::size_t, const Response*) { return { .done = true, .err = no_device }; }
__attribute__((weak)) Response CreateSave(const void*, const std::size_t, const Response*) { return { .done = true, .err = no_device }; }

__attribute__((weak)) Response GetSaveLocation() { return { .done = true, .err = no_device }; }

__attribute__((weak)) Response Delete() { return { .done = true, .err = no_device }; }

__attribute__((weak)) Response GetMetaData(void*) { return { .done = true, .err = no_device }; }
__attribute__((weak)) Response SetMetaData(const void*) { return { .done = true, .err = no_device }; }

__attribute__((weak)) u8 AccessibleDevices() { return 0; }

__attribute__((weak)) Response GetDeviceStatus(u8) { return { .done = true, .err = no_device }; }

__attribute__((weak)) u8 FreeDevices(const std::size_t) { return 0; }

}

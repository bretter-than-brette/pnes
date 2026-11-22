/**
 * @file internal.hpp
 * @brief Shared declarations for the libogc (GameCube + Wii) backend.
 *
 * One backend serves both consoles -- same CPU, same GX pipeline -- so the only
 * divergence is input (PAD vs WPAD) and a couple of libogc machine flags. A Wii
 * .dol built here also runs unmodified on Wii U via vWii.
 */
#ifndef OGC_INTERNAL_H
#define OGC_INTERNAL_H

#include <gccore.h>
#include <intsh>
using namespace br0::intsh;

extern void nmi_vector();
extern int quit;

void ogc_input_init();

#endif // OGC_INTERNAL_H

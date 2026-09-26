// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef PLATFORM_RESOURCE_LIB_H_
#define PLATFORM_RESOURCE_LIB_H_
#include <Base.h>

/** Publish SoC MMIO resources after the FDT HOB is available. */
EFI_STATUS
EFIAPI
PlatformResourceInit (IN VOID *Fdt);

#endif

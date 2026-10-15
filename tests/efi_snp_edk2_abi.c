// SPDX-License-Identifier: GPL-2.0-only
/* Compile with the local EDK II MdePkg headers for each tested architecture. */
#include <Uefi.h>
#include <Protocol/SimpleNetwork.h>
#include <stddef.h>
_Static_assert(offsetof(EFI_SIMPLE_NETWORK_PROTOCOL, Stop) == 16, "Stop");
_Static_assert(offsetof(EFI_SIMPLE_NETWORK_PROTOCOL, Shutdown) == 40, "Shutdown");
_Static_assert(offsetof(EFI_SIMPLE_NETWORK_PROTOCOL, Mode) == 120, "Mode");
_Static_assert(sizeof(EFI_SIMPLE_NETWORK_PROTOCOL) == 128, "SNP size");
_Static_assert(offsetof(EFI_SIMPLE_NETWORK_MODE, State) == 0, "State prefix");
_Static_assert(sizeof(((EFI_SIMPLE_NETWORK_MODE *)0)->State) == 4, "State UINT32");
_Static_assert(offsetof(EFI_BOOT_SERVICES, LocateHandleBuffer) == 312, "BS offset");
_Static_assert(sizeof(EFI_STATUS) == 8, "Native status");

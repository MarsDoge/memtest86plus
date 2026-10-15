// SPDX-License-Identifier: GPL-2.0-only
#ifndef EFISNP_H
#define EFISNP_H

#include <stddef.h>
#include "efi.h"

#define EFI_SNP_STOPPED       0
#define EFI_SNP_STARTED       1
#define EFI_SNP_INITIALIZED   2
#define EFI_SNP_REVISION      UINT64_C(0x10000)

/* Only State is accessed: the remaining EFI_SIMPLE_NETWORK_MODE is opaque. */
typedef struct {
    volatile uint32_t state;
} efi_snp_mode_prefix_t;

/* UEFI SNP v1.0 layout. Unused entry points are pointer-sized placeholders,
 * as in efi.h; callable methods use the platform's EFI ABI. */
typedef struct efi_snp_s {
    uint64_t revision;
    void *start;
    efi_status_t (efiapi *stop)(struct efi_snp_s *);
    void *initialize;
    void *reset;
    efi_status_t (efiapi *shutdown)(struct efi_snp_s *);
    void *receive_filters;
    void *station_address;
    void *statistics;
    void *mcast_ip_to_mac;
    void *nvdata;
    void *get_status;
    void *transmit;
    void *receive;
    void *wait_for_packet;
    efi_snp_mode_prefix_t *mode;
} efi_snp_t;

#if ARCH_BITS == 64
_Static_assert(offsetof(efi_snp_t, stop) == 16, "SNP Stop ABI");
_Static_assert(offsetof(efi_snp_t, shutdown) == 40, "SNP Shutdown ABI");
_Static_assert(offsetof(efi_snp_t, mode) == 120, "SNP Mode ABI");
_Static_assert(sizeof(efi_snp_t) == 128, "SNP protocol ABI");
_Static_assert(offsetof(efi_boot_services_t, locate_handle_buffer) == 312,
               "BootServices LocateHandleBuffer ABI");
#endif

typedef void (*snp_log_t)(efi_handle_t, const char *, efi_status_t, uint32_t);

static uint32_t snp_state(efi_snp_t *snp)
{
    return snp && snp->mode ? snp->mode->state : UINT32_MAX;
}

static efi_status_t snp_stop_one(efi_handle_t handle, efi_snp_t *snp, snp_log_t log)
{
    uint32_t state = snp_state(snp);
    log(handle, "initial", EFI_SUCCESS, state);
    if (!snp || !snp->mode || snp->revision < EFI_SNP_REVISION) {
        log(handle, "invalid-protocol", EFI_UNSUPPORTED, state);
        return EFI_UNSUPPORTED;
    }
    if (state == EFI_SNP_INITIALIZED) {
        if (!snp->shutdown) {
            log(handle, "missing-Shutdown", EFI_UNSUPPORTED, state);
            return EFI_UNSUPPORTED;
        }
        efi_status_t status = snp->shutdown(snp);
        state = snp_state(snp);
        log(handle, "Shutdown", status, state);
        if (status != EFI_SUCCESS) return status;
        if (state != EFI_SNP_STARTED) {
            log(handle, "bad-Shutdown-state", EFI_ABORTED, state);
            return EFI_ABORTED;
        }
    }
    if (state == EFI_SNP_STARTED) {
        if (!snp->stop) {
            log(handle, "missing-Stop", EFI_UNSUPPORTED, state);
            return EFI_UNSUPPORTED;
        }
        efi_status_t status = snp->stop(snp);
        state = snp_state(snp);
        log(handle, "Stop", status, state);
        if (status != EFI_SUCCESS) return status;
    }
    if (state != EFI_SNP_STOPPED) {
        log(handle, "not-stopped", EFI_ABORTED, state);
        return EFI_ABORTED;
    }
    log(handle, "stopped", EFI_SUCCESS, state);
    return EFI_SUCCESS;
}

/* Direct SNP enumeration includes SNP child handles with no PCI I/O protocol.
 * Called exactly once, after image/console setup, before the final memory map.
 * Retain the small firmware-allocated handle buffer until EBS (then reclaimable).
 * Replace each handle slot with its SNP pointer for a last, callback-free read.
 * No protocol call, print, or key wait occurs after that successful check.
 * This cannot prevent asynchronous firmware or EBS callbacks rearming a NIC.
 */
static efi_status_t snp_stop_all(efi_boot_services_t *bs, snp_log_t log)
{
    efi_guid_t guid = {0xa19832b9, 0xac25, 0x11d3,
                      {0x9a, 0x2d, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}};
    efi_handle_t *handles = NULL;
    uintn_t count = 0;
    efi_status_t status = bs->locate_handle_buffer(EFI_LOCATE_BY_PROTOCOL,
                                                   &guid, NULL, &count, &handles);
    log(NULL, "enumerate", status, (uint32_t)count);
    if (status == EFI_NOT_FOUND) {
        log(NULL, "NO-SNP-coverage", status, 0);
        return EFI_SUCCESS;
    }
    if (status != EFI_SUCCESS) return status;
    if (!count || !handles) {
        log(NULL, "invalid-handle-list", EFI_ABORTED, 0);
        return EFI_ABORTED;
    }
    efi_status_t result = EFI_SUCCESS;
    for (uintn_t i = 0; i < count; i++) {
        efi_snp_t *snp = NULL;
        efi_handle_t handle = handles[i];
        status = bs->handle_protocol(handle, &guid, (void **)&snp);
        log(handle, "HandleProtocol", status, snp_state(snp));
        if (status == EFI_SUCCESS) status = snp_stop_one(handle, snp, log);
        if (status != EFI_SUCCESS && result == EFI_SUCCESS) result = status;
        handles[i] = snp;
    }
    if (result != EFI_SUCCESS) return result;
    log(NULL, "operations-ok-final-state-check-next", EFI_SUCCESS, (uint32_t)count);
    for (uintn_t i = 0; i < count; i++) {
        uint32_t state = snp_state((efi_snp_t *)handles[i]);
        if (state != EFI_SNP_STOPPED) {
            log(NULL, "REARMED-or-invalid", EFI_ABORTED, state);
            return EFI_ABORTED;
        }
    }
    return EFI_SUCCESS;
}
#endif

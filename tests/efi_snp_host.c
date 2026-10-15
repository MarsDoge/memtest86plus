// SPDX-License-Identifier: GPL-2.0-only
/* Host mocks exercise the exact EFI stub and SNP code, using EFIABI callbacks. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#define ARCH_BITS 64
#define EFI_SNP_AB_TEST 1
#include "../boot/efisetup.c"

uint8_t startup32[1];
static efi_snp_mode_prefix_t modes[3];
static efi_snp_t snps[3];
static efi_handle_t handles[3];
static unsigned shutdowns, stops, enumerations, maps, exits, allocations;
static efi_status_t shutdown_status, stop_status, enum_status, protocol_status;
static int bad_shutdown, bad_stop, rearm, retry_exit, forbid_console;
static uintn_t nhandles;
static char console[32768];
static size_t console_len;
static jmp_buf halted;
static efi_memory_desc_t memory_map[32];

static efi_status_t efiapi shutdown_mock(efi_snp_t *snp)
{
    assert(!maps && !exits);
    shutdowns++;
    if (!shutdown_status && !bad_shutdown) snp->mode->state = EFI_SNP_STARTED;
    return shutdown_status;
}
static efi_status_t efiapi stop_mock(efi_snp_t *snp)
{
    assert(!maps && !exits);
    assert(snp->mode->state == EFI_SNP_STARTED);
    stops++;
    if (!stop_status && !bad_stop) snp->mode->state = EFI_SNP_STOPPED;
    return stop_status;
}
static efi_status_t efiapi enumerate_mock(int search, efi_guid_t *guid, void *key,
                                         uintn_t *count, efi_handle_t **out)
{
    assert(search == EFI_LOCATE_BY_PROTOCOL && key == NULL);
    assert(guid->a == 0xa19832b9 && guid->b == 0xac25 && guid->c == 0x11d3);
    assert(!maps && !exits); enumerations++;
    if (enum_status) return enum_status;
    *count = nhandles; *out = handles;
    return EFI_SUCCESS;
}
static efi_status_t efiapi protocol_mock(efi_handle_t handle, efi_guid_t *guid, void **out)
{
    assert(guid->a == 0xa19832b9);
    assert(!maps && !exits);
    if (protocol_status) return protocol_status;
    *out = handle; return EFI_SUCCESS;
}
static void log_mock(efi_handle_t handle, const char *op, efi_status_t status, uint32_t state)
{
    (void)handle; (void)status; (void)state;
    if (rearm && strcmp(op, "operations-ok-final-state-check-next") == 0)
        modes[0].state = EFI_SNP_INITIALIZED;
}
static efi_status_t efiapi output_mock(efi_simple_text_out_t *out, efi_char16_t *text)
{
    (void)out;
    assert(!forbid_console);
    for (; *text; text++) {
        assert(console_len + 1 < sizeof(console));
        console[console_len++] = (char)*text;
        console[console_len] = 0;
    }
    if (strstr(console, "SNP-AB FAILED:")) longjmp(halted, 1);
    return EFI_SUCCESS;
}
static efi_status_t efiapi locate_gop_mock(int search, efi_guid_t *guid, void *key,
                                          uintn_t *size, efi_handle_t *out)
{
    (void)search; (void)key; (void)size; (void)out;
    assert(guid->a == 0x9042a9de);
    return EFI_NOT_FOUND;
}
static efi_status_t efiapi allocate_mock(int type, uintn_t size, void **out)
{
    (void)type;
    assert(!exits && size <= sizeof(memory_map)); allocations++;
    *out = memory_map; return EFI_SUCCESS;
}
static efi_status_t efiapi map_mock(uintn_t *size, void *map, uintn_t *key,
                                   uintn_t *desc_size, uint32_t *version)
{
    assert(enumerations == 1 && modes[0].state == EFI_SNP_STOPPED);
    maps++; *size = sizeof(efi_memory_desc_t); *key = maps;
    *desc_size = sizeof(efi_memory_desc_t); *version = 1;
    memset(map, 0, *size);
    ((efi_memory_desc_t *)map)->type = EFI_CONVENTIONAL_MEMORY;
    ((efi_memory_desc_t *)map)->phys_addr = 0x100000;
    ((efi_memory_desc_t *)map)->num_pages = 4;
    return EFI_SUCCESS;
}
static efi_status_t efiapi exit_mock(efi_handle_t handle, uintn_t key)
{
    (void)handle;
    assert(key == maps && shutdowns == 1 && stops == 1);
    forbid_console = 1; exits++;
    return retry_exit && exits == 1 ? EFI_INVALID_PARAMETER : EFI_SUCCESS;
}
static efi_boot_services_t bs = {
    .locate_handle_buffer = enumerate_mock, .handle_protocol = protocol_mock,
    .locate_handle = locate_gop_mock, .allocate_pool = allocate_mock,
    .get_memory_map = map_mock, .exit_boot_services = exit_mock
};
static efi_simple_text_out_t output = {.output_string = output_mock};
static efi_system_table_t table = {
    .header = {.signature = EFI_SYSTEM_TABLE_SIGNATURE},
    .boot_services = &bs, .con_out = &output
};
static void reset(uint32_t state)
{
    memset(modes, 0, sizeof(modes)); memset(snps, 0, sizeof(snps));
    for (unsigned i = 0; i < 3; i++) {
        snps[i].revision = EFI_SNP_REVISION; snps[i].mode = &modes[i];
        snps[i].shutdown = shutdown_mock; snps[i].stop = stop_mock;
        handles[i] = &snps[i];
    }
    modes[0].state = state;
    nhandles = 1; shutdowns = stops = enumerations = maps = exits = allocations = 0;
    shutdown_status = stop_status = enum_status = protocol_status = EFI_SUCCESS;
    bad_shutdown = bad_stop = rearm = retry_exit = forbid_console = 0;
    console_len = 0; console[0] = 0;
}
#define RUN(name, body) do { body; puts("PASS " name); } while (0)
int main(void)
{
    _Static_assert(offsetof(efi_snp_t, stop) == 16, "SNP Stop ABI");
    _Static_assert(offsetof(efi_snp_t, shutdown) == 40, "SNP Shutdown ABI");
    _Static_assert(offsetof(efi_snp_t, mode) == 120, "SNP Mode ABI");
    _Static_assert(sizeof(efi_status_t) == 8, "native EFI_STATUS");
    _Static_assert(offsetof(efi_boot_services_t, locate_handle_buffer) == 312, "BootServices ABI");
    RUN("Initialized Shutdown then Stop", reset(2); assert(!snp_stop_all(&bs, log_mock)); assert(shutdowns == 1 && stops == 1));
    RUN("Started Stop only", reset(1); assert(!snp_stop_all(&bs, log_mock)); assert(!shutdowns && stops == 1));
    RUN("Stopped skip", reset(0); assert(!snp_stop_all(&bs, log_mock)); assert(!shutdowns && !stops));
    RUN("Shutdown error no Stop", reset(2); shutdown_status = EFI_UNSUPPORTED; assert(snp_stop_all(&bs, log_mock) == EFI_UNSUPPORTED); assert(!stops));
    RUN("Shutdown warning rejected", reset(2); shutdown_status = 1; assert(snp_stop_all(&bs, log_mock) == 1); assert(!stops));
    RUN("Shutdown bad state rejected", reset(2); bad_shutdown = 1; assert(snp_stop_all(&bs, log_mock) == EFI_ABORTED); assert(!stops));
    RUN("Stop error rejected", reset(1); stop_status = EFI_UNSUPPORTED; assert(snp_stop_all(&bs, log_mock) == EFI_UNSUPPORTED));
    RUN("Stop bad state rejected", reset(1); bad_stop = 1; assert(snp_stop_all(&bs, log_mock) == EFI_ABORTED));
    RUN("Unknown state rejected", reset(3); assert(snp_stop_all(&bs, log_mock) == EFI_ABORTED));
    RUN("NULL mode rejected", reset(2); snps[0].mode = NULL; assert(snp_stop_all(&bs, log_mock) == EFI_UNSUPPORTED));
    RUN("NULL protocol rejected", reset(2); handles[0] = NULL; assert(snp_stop_all(&bs, log_mock) == EFI_UNSUPPORTED));
    RUN("Bad revision rejected", reset(2); snps[0].revision = 0; assert(snp_stop_all(&bs, log_mock) == EFI_UNSUPPORTED));
    RUN("Missing Shutdown rejected", reset(2); snps[0].shutdown = NULL; assert(snp_stop_all(&bs, log_mock) == EFI_UNSUPPORTED));
    RUN("Missing Stop rejected", reset(1); snps[0].stop = NULL; assert(snp_stop_all(&bs, log_mock) == EFI_UNSUPPORTED));
    RUN("Enumeration error rejected", reset(2); enum_status = EFI_ABORTED; assert(snp_stop_all(&bs, log_mock) == EFI_ABORTED));
    RUN("No SNP explicit uncovered path", reset(2); enum_status = EFI_NOT_FOUND; assert(!snp_stop_all(&bs, log_mock)); assert(!shutdowns && !stops));
    RUN("HandleProtocol error rejected", reset(2); protocol_status = EFI_NOT_FOUND; assert(snp_stop_all(&bs, log_mock) == EFI_NOT_FOUND));
    RUN("Invalid empty success rejected", reset(2); nhandles = 0; assert(snp_stop_all(&bs, log_mock) == EFI_ABORTED));
    RUN("All three handle states", reset(2); nhandles = 3; modes[1].state = 1; assert(!snp_stop_all(&bs, log_mock)); assert(shutdowns == 1 && stops == 2));
    RUN("Continue other handles on failure", reset(3); nhandles = 3; modes[1].state = 1; assert(snp_stop_all(&bs, log_mock) == EFI_ABORTED); assert(stops == 1));
    RUN("Console callback rearm caught", reset(2); rearm = 1; assert(snp_stop_all(&bs, log_mock) == EFI_ABORTED));
    for (int retry = 0; retry <= 1; retry++) {
        reset(2); retry_exit = retry;
        boot_params_t params = {0};
        assert(efi_setup(NULL, &table, &params) == &params);
        assert(enumerations == 1 && shutdowns == 1 && stops == 1);
        assert(exits == (unsigned)(1 + retry) && maps == exits && allocations == 1);
        assert(params.e820_entries == 1);
        puts(retry ? "PASS actual EFI setup EBS retry: no SNP/console/allocation on retry" :
                     "PASS actual EFI setup cleanup before first map/EBS");
    }
    reset(2); shutdown_status = EFI_UNSUPPORTED;
    if (!setjmp(halted)) {
        boot_params_t params = {0};
        efi_setup(NULL, &table, &params);
        assert(0);
    }
    assert(!maps && !exits);
    puts("PASS actual EFI setup failure gate: no map/EBS");
    return 0;
}

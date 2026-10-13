#ifndef STORAGE_BOOT_PROBE_H
#define STORAGE_BOOT_PROBE_H

#include <cstdint>

/* Host-test-only ordering probe for production storage boot; compiled out on target. */
#if defined(PATTERN_STORE_HOST_TEST)
enum StorageBootProbeEvent : uint8_t
{
    STORAGE_BOOT_PROBE_RECOVERY_RESOLVED = 1u,
    STORAGE_BOOT_PROBE_DIRECTORY_LOOKUP,
    STORAGE_BOOT_PROBE_PATTERN_LOAD,
};
inline void (*g_storage_boot_probe)(StorageBootProbeEvent event) = nullptr;
#define STORAGE_BOOT_PROBE(event) \
    do { if (g_storage_boot_probe) g_storage_boot_probe(event); } while (0)
#else
#define STORAGE_BOOT_PROBE(event) ((void)0)
#endif

#endif

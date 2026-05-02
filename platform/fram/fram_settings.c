#include "platform/fram/fram_settings.h"
#include "platform/fram/mb85rc256.h"
#include <string.h>

static uint32_t Fnv1a32(const uint8_t* data, uint16_t len)
{
    uint32_t h = 2166136261u;
    for (uint16_t i = 0; i < len; ++i)
    {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

void FramSettings_Default(FramSettingsBlob* blob)
{
    if (!blob) return;

    memset(blob, 0, sizeof(*blob));
    blob->magic = FRAM_SETTINGS_MAGIC;
    blob->version = (uint16_t)FRAM_SETTINGS_VERSION;
    blob->payload_size = 0u;
    blob->flags = 0u;
    blob->checksum = FramSettings_CalcChecksum(blob);
}

uint32_t FramSettings_CalcChecksum(const FramSettingsBlob* blob)
{
    if (!blob) return 0u;

    uint16_t payload_size = blob->payload_size;
    if (payload_size > (uint16_t)sizeof(blob->reserved))
    {
        return 0u;
    }

    uint32_t h = 2166136261u;
    h ^= (uint32_t)blob->version;
    h *= 16777619u;
    h ^= (uint32_t)payload_size;
    h *= 16777619u;
    h ^= blob->flags;
    h *= 16777619u;

    if (payload_size > 0u)
    {
        uint32_t p = Fnv1a32(blob->reserved, payload_size);
        h ^= p;
        h *= 16777619u;
    }

    return h;
}

static uint8_t FramSettings_IsValid(const FramSettingsBlob* blob)
{
    if (!blob) return 0u;
    if (blob->magic != FRAM_SETTINGS_MAGIC) return 0u;
    if (blob->version != (uint16_t)FRAM_SETTINGS_VERSION) return 0u;
    if (blob->payload_size > (uint16_t)sizeof(blob->reserved)) return 0u;
    return (blob->checksum == FramSettings_CalcChecksum(blob)) ? 1u : 0u;
}

uint8_t FramSettings_Read(FramSettingsBlob* out_blob)
{
    if (!out_blob) return 0u;

    if (!MB85RC256_Read(FRAM_SETTINGS_ADDR, (uint8_t*)out_blob, (uint16_t)sizeof(*out_blob)))
    {
        return 0u;
    }

    return FramSettings_IsValid(out_blob);
}

uint8_t FramSettings_Write(const FramSettingsBlob* blob)
{
    FramSettingsBlob tmp;

    if (!blob) return 0u;
    if (blob->payload_size > (uint16_t)sizeof(blob->reserved)) return 0u;

    tmp = *blob;
    tmp.magic = FRAM_SETTINGS_MAGIC;
    tmp.version = (uint16_t)FRAM_SETTINGS_VERSION;
    tmp.checksum = FramSettings_CalcChecksum(&tmp);

    return MB85RC256_WriteAndVerify(FRAM_SETTINGS_ADDR,
                                    (const uint8_t*)&tmp,
                                    (uint16_t)sizeof(tmp));
}

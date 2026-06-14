#include "boot_security.h"

#include <stddef.h>
#include <string.h>

#include "boot_crypto.h"
#include "boot_keys.h"
#include "boot_policy.h"
#include "boot_platform.h"

typedef struct
{
    bool session_active;
    bool manifest_required;
    bool manifest_valid;
    boot_security_begin_t begin;
    boot_manifest_t manifest;
    boot_sha256_context_t image_hash;
    boot_aes128_ctr_context_t stream_cipher;
} boot_security_context_t;

static boot_security_context_t g_security;

static void boot_security_manifest_digest(const boot_manifest_t *manifest,
                                          uint8_t digest[BOOT_SHA256_DIGEST_SIZE])
{
    boot_sha256_context_t sha;

    boot_sha256_init(&sha);
    boot_sha256_update(&sha, manifest, offsetof(boot_manifest_t, signature));
    boot_sha256_final(&sha, digest);
}

static bool boot_security_manifest_signature_is_valid(const boot_manifest_t *manifest)
{
    uint8_t digest[BOOT_SHA256_DIGEST_SIZE];

    boot_security_manifest_digest(manifest, digest);
    return boot_rsa2048_pkcs1v15_sha256_verify(g_boot_manifest_rsa2048_modulus,
                                               manifest->signature,
                                               digest);
}

static void boot_security_manifest_from_metadata(const boot_metadata_t *metadata,
                                                 boot_manifest_t *manifest)
{
    memset(manifest, 0, sizeof(*manifest));
    manifest->magic = BOOT_MANIFEST_MAGIC;
    manifest->manifest_version = BOOT_MANIFEST_VERSION;
    manifest->target_id = metadata->target_id;
    manifest->image_size = metadata->image_size;
    manifest->image_crc32 = metadata->image_crc32;
    manifest->firmware_version = metadata->version;
    manifest->flags = metadata->flags;
    manifest->key_id = metadata->key_id;
    memcpy(manifest->image_sha256, metadata->image_sha256, sizeof(manifest->image_sha256));
    memcpy(manifest->encryption_nonce, metadata->encryption_nonce, sizeof(manifest->encryption_nonce));
    memcpy(manifest->signature, metadata->manifest_signature, sizeof(manifest->signature));
}

void boot_security_reset(void)
{
    memset(&g_security, 0, sizeof(g_security));
}

boot_error_t boot_security_begin(const boot_security_begin_t *request)
{
    const bool signed_update = (request != NULL) && ((request->flags & BOOT_FLAG_VERIFY_SIGNATURE) != 0U);
    const bool anti_rollback_update = (request != NULL) && ((request->flags & BOOT_FLAG_ANTI_ROLLBACK) != 0U);
    const bool rollback_required =
        anti_rollback_update ||
        ((AFFINE_BOOT_REQUIRE_ANTI_ROLLBACK_FOR_SIGNED != 0) && signed_update);

    if (request == NULL)
    {
        return BOOT_ERR_RANGE;
    }

    if (!boot_security_supports_flags(request->flags))
    {
        return BOOT_ERR_UNSUPPORTED_OPCODE;
    }

    if (!signed_update &&
        ((AFFINE_BOOT_REQUIRE_SIGNED_UPDATES != 0) || (AFFINE_BOOT_ALLOW_UNSIGNED_UPDATES == 0)))
    {
        return BOOT_ERR_SIGNATURE;
    }

    if (((request->flags & BOOT_FLAG_ENCRYPTED_IMAGE) != 0U) &&
        ((request->flags & BOOT_FLAG_VERIFY_SIGNATURE) == 0U))
    {
        return BOOT_ERR_UNSUPPORTED_OPCODE;
    }

    if (anti_rollback_update && !signed_update)
    {
        return BOOT_ERR_UNSUPPORTED_OPCODE;
    }

    if (anti_rollback_update &&
        (AFFINE_BOOT_ALLOW_OPTIONAL_ANTI_ROLLBACK == 0) &&
        (AFFINE_BOOT_REQUIRE_ANTI_ROLLBACK_FOR_SIGNED == 0))
    {
        return BOOT_ERR_ROLLBACK;
    }

    if (rollback_required &&
        request->has_rollback_floor &&
        (request->version < request->rollback_floor_version))
    {
        return BOOT_ERR_ROLLBACK;
    }

    memset(&g_security, 0, sizeof(g_security));
    g_security.session_active = true;
    g_security.manifest_required = (request->flags & BOOT_FLAG_VERIFY_SIGNATURE) != 0U;
    g_security.begin = *request;
    boot_sha256_init(&g_security.image_hash);
    return BOOT_ERR_NONE;
}

boot_error_t boot_security_set_manifest(const boot_manifest_t *manifest)
{
    if ((manifest == NULL) || !g_security.session_active)
    {
        return BOOT_ERR_BAD_STATE;
    }

    if (!g_security.manifest_required)
    {
        return BOOT_ERR_BAD_STATE;
    }

    if (manifest->magic != BOOT_MANIFEST_MAGIC)
    {
        return BOOT_ERR_MANIFEST;
    }

    if (manifest->manifest_version != BOOT_MANIFEST_VERSION)
    {
        return BOOT_ERR_MANIFEST;
    }

    if ((manifest->target_id != g_security.begin.target_id) ||
        (manifest->image_size != g_security.begin.image_size) ||
        (manifest->image_crc32 != g_security.begin.expected_crc32) ||
        (manifest->firmware_version != g_security.begin.version) ||
        (manifest->flags != g_security.begin.flags))
    {
        return BOOT_ERR_MANIFEST;
    }

    if (!boot_security_manifest_signature_is_valid(manifest))
    {
        return BOOT_ERR_SIGNATURE;
    }

    g_security.manifest = *manifest;
    g_security.manifest_valid = true;
    if ((g_security.begin.flags & BOOT_FLAG_ENCRYPTED_IMAGE) != 0U)
    {
        boot_aes128_ctr_init(&g_security.stream_cipher,
                             g_boot_stream_aes128_key,
                             manifest->encryption_nonce);
    }
    return BOOT_ERR_NONE;
}

boot_error_t boot_security_transform_chunk(uint32_t offset,
                                          uint8_t *data,
                                          uint32_t length)
{
    if (!g_security.session_active)
    {
        return BOOT_ERR_BAD_STATE;
    }

    if (g_security.manifest_required && !g_security.manifest_valid)
    {
        return BOOT_ERR_MANIFEST;
    }

    if ((g_security.begin.flags & BOOT_FLAG_ENCRYPTED_IMAGE) != 0U)
    {
        boot_aes128_ctr_xcrypt(&g_security.stream_cipher, offset, data, length);
    }

    return BOOT_ERR_NONE;
}

boot_error_t boot_security_note_plaintext(const uint8_t *data, uint32_t length)
{
    if (!g_security.session_active)
    {
        return BOOT_ERR_BAD_STATE;
    }

    boot_sha256_update(&g_security.image_hash, data, length);
    return BOOT_ERR_NONE;
}

boot_error_t boot_security_finalize_metadata(boot_metadata_t *metadata)
{
    uint8_t image_sha256[BOOT_SHA256_DIGEST_SIZE];

    if ((metadata == NULL) || !g_security.session_active)
    {
        return BOOT_ERR_BAD_STATE;
    }

    if (g_security.manifest_required && !g_security.manifest_valid)
    {
        return BOOT_ERR_MANIFEST;
    }

    boot_sha256_final(&g_security.image_hash, image_sha256);

    if (g_security.manifest_valid)
    {
        if (memcmp(image_sha256, g_security.manifest.image_sha256, sizeof(image_sha256)) != 0)
        {
            return BOOT_ERR_SIGNATURE;
        }

        metadata->key_id = g_security.manifest.key_id;
        memcpy(metadata->image_sha256, g_security.manifest.image_sha256, sizeof(metadata->image_sha256));
        memcpy(metadata->encryption_nonce, g_security.manifest.encryption_nonce, sizeof(metadata->encryption_nonce));
        memcpy(metadata->manifest_signature,
               g_security.manifest.signature,
               sizeof(metadata->manifest_signature));
    }
    else
    {
        metadata->key_id = 0U;
        memcpy(metadata->image_sha256, image_sha256, sizeof(metadata->image_sha256));
        memset(metadata->encryption_nonce, 0, sizeof(metadata->encryption_nonce));
        memset(metadata->manifest_signature, 0, sizeof(metadata->manifest_signature));
    }

    boot_metadata_update_crc(metadata);
    return BOOT_ERR_NONE;
}

boot_error_t boot_security_verify_image_with_state(const boot_target_config_t *cfg,
                                                   const boot_metadata_t *metadata,
                                                   const boot_security_state_t *candidate_state)
{
    boot_sha256_context_t sha;
    boot_manifest_t manifest;
    boot_security_state_t security_state;
    uint8_t image_sha256[BOOT_SHA256_DIGEST_SIZE];
    uint32_t offset = 0U;
    const bool signed_image = (metadata != NULL) && ((metadata->flags & BOOT_FLAG_VERIFY_SIGNATURE) != 0U);
    const bool rollback_required =
        ((metadata != NULL) && ((metadata->flags & BOOT_FLAG_ANTI_ROLLBACK) != 0U)) ||
        ((AFFINE_BOOT_REQUIRE_ANTI_ROLLBACK_FOR_SIGNED != 0) && signed_image);

    if ((cfg == NULL) || (metadata == NULL))
    {
        return BOOT_ERR_RANGE;
    }

    if (!signed_image &&
        ((AFFINE_BOOT_REQUIRE_SIGNED_UPDATES != 0) || (AFFINE_BOOT_ALLOW_UNSIGNED_UPDATES == 0)))
    {
        return BOOT_ERR_SIGNATURE;
    }

    if (((metadata->flags & (BOOT_FLAG_ENCRYPTED_IMAGE | BOOT_FLAG_ANTI_ROLLBACK)) != 0U) &&
        !signed_image)
    {
        return BOOT_ERR_MANIFEST;
    }

    if (rollback_required)
    {
        const bool have_security_state =
            ((candidate_state != NULL) && boot_security_state_is_valid(cfg, candidate_state));

        if (have_security_state)
        {
            security_state = *candidate_state;
        }
        else if (!boot_security_state_select_valid(cfg, &security_state))
        {
            return BOOT_ERR_ROLLBACK;
        }

        if (metadata->version < security_state.rollback_floor_version)
        {
            return BOOT_ERR_ROLLBACK;
        }
    }

    if (!signed_image)
    {
        return BOOT_ERR_NONE;
    }

    boot_security_manifest_from_metadata(metadata, &manifest);
    if (!boot_security_manifest_signature_is_valid(&manifest))
    {
        return BOOT_ERR_SIGNATURE;
    }

    boot_sha256_init(&sha);
    while (offset < metadata->image_size)
    {
        const uint32_t remaining = metadata->image_size - offset;
        const uint32_t chunk_len = (remaining > 1024U) ? 1024U : remaining;
        const uint8_t *image = (const uint8_t *)(metadata->app_base + offset);

        boot_sha256_update(&sha, image, chunk_len);
        boot_platform_watchdog_kick();
        offset += chunk_len;
    }
    boot_sha256_final(&sha, image_sha256);

    if (memcmp(image_sha256, metadata->image_sha256, sizeof(image_sha256)) != 0)
    {
        return BOOT_ERR_SIGNATURE;
    }

    return BOOT_ERR_NONE;
}

boot_error_t boot_security_verify_image(const boot_target_config_t *cfg,
                                        const boot_metadata_t *metadata)
{
    return boot_security_verify_image_with_state(cfg, metadata, NULL);
}

uint32_t boot_security_capabilities(void)
{
    uint32_t capabilities = BOOT_CAP_VERIFY_SIGNATURE | BOOT_CAP_ENCRYPTED_STREAM;

    if ((AFFINE_BOOT_ALLOW_OPTIONAL_ANTI_ROLLBACK != 0) ||
        (AFFINE_BOOT_REQUIRE_ANTI_ROLLBACK_FOR_SIGNED != 0))
    {
        capabilities |= BOOT_CAP_ANTI_ROLLBACK;
    }

    if ((AFFINE_BOOT_ALLOW_UNSIGNED_UPDATES != 0) && (AFFINE_BOOT_REQUIRE_SIGNED_UPDATES == 0))
    {
        capabilities |= BOOT_CAP_UNSIGNED_UPDATES;
    }

    return capabilities;
}

bool boot_security_supports_flags(uint32_t flags)
{
    const uint32_t supported_flags =
        BOOT_FLAG_VERIFY_SIGNATURE | BOOT_FLAG_ENCRYPTED_IMAGE | BOOT_FLAG_ANTI_ROLLBACK;
    return (flags & ~supported_flags) == 0U;
}

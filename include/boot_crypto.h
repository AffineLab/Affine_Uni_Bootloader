#ifndef AFFINE_BOOT_CRYPTO_H
#define AFFINE_BOOT_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BOOT_SHA256_DIGEST_SIZE 32U

typedef struct
{
    uint32_t state[8];
    uint64_t total_len;
    uint8_t buffer[64];
    uint32_t buffer_len;
} boot_sha256_context_t;

typedef struct
{
    uint8_t round_key[176];
    uint8_t counter[16];
} boot_aes128_ctr_context_t;

void boot_sha256_init(boot_sha256_context_t *ctx);
void boot_sha256_update(boot_sha256_context_t *ctx, const void *data, size_t length);
void boot_sha256_final(boot_sha256_context_t *ctx, uint8_t digest[BOOT_SHA256_DIGEST_SIZE]);

bool boot_rsa2048_pkcs1v15_sha256_verify(const uint8_t modulus[256],
                                          const uint8_t signature[256],
                                          const uint8_t digest[BOOT_SHA256_DIGEST_SIZE]);
void boot_aes128_ctr_init(boot_aes128_ctr_context_t *ctx,
                          const uint8_t key[16],
                          const uint8_t nonce[16]);
void boot_aes128_ctr_xcrypt(boot_aes128_ctr_context_t *ctx,
                            uint32_t offset,
                            uint8_t *data,
                            uint32_t length);

#endif

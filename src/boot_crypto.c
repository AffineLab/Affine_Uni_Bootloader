#include "boot_crypto.h"

#include <string.h>

#define SHA256_BLOCK_SIZE 64U
#define RSA2048_WORDS     64U
#define RSA2048_BYTES     256U
#define AES128_BLOCK_SIZE  16U

static uint32_t rotr32(uint32_t value, uint32_t shift)
{
    return (value >> shift) | (value << (32U - shift));
}

static uint32_t load_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static void store_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static void sha256_process_block(boot_sha256_context_t *ctx, const uint8_t block[SHA256_BLOCK_SIZE])
{
    static const uint32_t k[64] = {
        0x428A2F98UL, 0x71374491UL, 0xB5C0FBCFUL, 0xE9B5DBA5UL,
        0x3956C25BUL, 0x59F111F1UL, 0x923F82A4UL, 0xAB1C5ED5UL,
        0xD807AA98UL, 0x12835B01UL, 0x243185BEUL, 0x550C7DC3UL,
        0x72BE5D74UL, 0x80DEB1FEUL, 0x9BDC06A7UL, 0xC19BF174UL,
        0xE49B69C1UL, 0xEFBE4786UL, 0x0FC19DC6UL, 0x240CA1CCUL,
        0x2DE92C6FUL, 0x4A7484AAUL, 0x5CB0A9DCUL, 0x76F988DAUL,
        0x983E5152UL, 0xA831C66DUL, 0xB00327C8UL, 0xBF597FC7UL,
        0xC6E00BF3UL, 0xD5A79147UL, 0x06CA6351UL, 0x14292967UL,
        0x27B70A85UL, 0x2E1B2138UL, 0x4D2C6DFCUL, 0x53380D13UL,
        0x650A7354UL, 0x766A0ABBUL, 0x81C2C92EUL, 0x92722C85UL,
        0xA2BFE8A1UL, 0xA81A664BUL, 0xC24B8B70UL, 0xC76C51A3UL,
        0xD192E819UL, 0xD6990624UL, 0xF40E3585UL, 0x106AA070UL,
        0x19A4C116UL, 0x1E376C08UL, 0x2748774CUL, 0x34B0BCB5UL,
        0x391C0CB3UL, 0x4ED8AA4AUL, 0x5B9CCA4FUL, 0x682E6FF3UL,
        0x748F82EEUL, 0x78A5636FUL, 0x84C87814UL, 0x8CC70208UL,
        0x90BEFFFAUL, 0xA4506CEBUL, 0xBEF9A3F7UL, 0xC67178F2UL
    };
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;

    for (uint32_t i = 0U; i < 16U; ++i)
    {
        w[i] = load_be32(&block[i * 4U]);
    }

    for (uint32_t i = 16U; i < 64U; ++i)
    {
        const uint32_t s0 = rotr32(w[i - 15U], 7U) ^ rotr32(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
        const uint32_t s1 = rotr32(w[i - 2U], 17U) ^ rotr32(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
        w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (uint32_t i = 0U; i < 64U; ++i)
    {
        const uint32_t s1 = rotr32(e, 6U) ^ rotr32(e, 11U) ^ rotr32(e, 25U);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + s1 + ch + k[i] + w[i];
        const uint32_t s0 = rotr32(a, 2U) ^ rotr32(a, 13U) ^ rotr32(a, 22U);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void boot_sha256_init(boot_sha256_context_t *ctx)
{
    ctx->state[0] = 0x6A09E667UL;
    ctx->state[1] = 0xBB67AE85UL;
    ctx->state[2] = 0x3C6EF372UL;
    ctx->state[3] = 0xA54FF53AUL;
    ctx->state[4] = 0x510E527FUL;
    ctx->state[5] = 0x9B05688CUL;
    ctx->state[6] = 0x1F83D9ABUL;
    ctx->state[7] = 0x5BE0CD19UL;
    ctx->total_len = 0U;
    ctx->buffer_len = 0U;
}

void boot_sha256_update(boot_sha256_context_t *ctx, const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;

    if ((ctx == NULL) || ((bytes == NULL) && (length > 0U)))
    {
        return;
    }

    ctx->total_len += (uint64_t)length;

    while (length > 0U)
    {
        const uint32_t space = SHA256_BLOCK_SIZE - ctx->buffer_len;
        const uint32_t take = (length < space) ? (uint32_t)length : space;

        memcpy(&ctx->buffer[ctx->buffer_len], bytes, take);
        ctx->buffer_len += take;
        bytes += take;
        length -= take;

        if (ctx->buffer_len == SHA256_BLOCK_SIZE)
        {
            sha256_process_block(ctx, ctx->buffer);
            ctx->buffer_len = 0U;
        }
    }
}

void boot_sha256_final(boot_sha256_context_t *ctx, uint8_t digest[BOOT_SHA256_DIGEST_SIZE])
{
    const uint64_t bit_len = ctx->total_len * 8U;

    ctx->buffer[ctx->buffer_len++] = 0x80U;
    if (ctx->buffer_len > 56U)
    {
        while (ctx->buffer_len < SHA256_BLOCK_SIZE)
        {
            ctx->buffer[ctx->buffer_len++] = 0U;
        }
        sha256_process_block(ctx, ctx->buffer);
        ctx->buffer_len = 0U;
    }

    while (ctx->buffer_len < 56U)
    {
        ctx->buffer[ctx->buffer_len++] = 0U;
    }

    for (uint32_t i = 0U; i < 8U; ++i)
    {
        ctx->buffer[56U + i] = (uint8_t)(bit_len >> (56U - (i * 8U)));
    }
    sha256_process_block(ctx, ctx->buffer);

    for (uint32_t i = 0U; i < 8U; ++i)
    {
        store_be32(&digest[i * 4U], ctx->state[i]);
    }
}

static const uint8_t aes_sbox[256] = {
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76,
    0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0, 0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0,
    0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75,
    0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0, 0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84,
    0x53, 0xD1, 0x00, 0xED, 0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8,
    0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5, 0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2,
    0xCD, 0x0C, 0x13, 0xEC, 0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
    0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14, 0xDE, 0x5E, 0x0B, 0xDB,
    0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C, 0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79,
    0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F, 0x4B, 0xBD, 0x8B, 0x8A,
    0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E, 0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E,
    0xE1, 0xF8, 0x98, 0x11, 0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16
};

static uint8_t aes_xtime(uint8_t value)
{
    return (uint8_t)((value << 1U) ^ (((value >> 7U) & 1U) * 0x1BU));
}

static void aes_key_expansion(uint8_t round_key[176], const uint8_t key[16])
{
    static const uint8_t rcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36};
    uint8_t temp[4];
    uint32_t bytes = 16U;
    uint32_t rcon_index = 0U;

    memcpy(round_key, key, 16U);
    while (bytes < 176U)
    {
        memcpy(temp, &round_key[bytes - 4U], sizeof(temp));
        if ((bytes % 16U) == 0U)
        {
            const uint8_t first = temp[0];
            temp[0] = aes_sbox[temp[1]] ^ rcon[rcon_index++];
            temp[1] = aes_sbox[temp[2]];
            temp[2] = aes_sbox[temp[3]];
            temp[3] = aes_sbox[first];
        }

        for (uint32_t i = 0U; i < 4U; ++i)
        {
            round_key[bytes] = round_key[bytes - 16U] ^ temp[i];
            ++bytes;
        }
    }
}

static void aes_add_round_key(uint8_t state[16], const uint8_t *round_key)
{
    for (uint32_t i = 0U; i < 16U; ++i)
    {
        state[i] ^= round_key[i];
    }
}

static void aes_sub_bytes(uint8_t state[16])
{
    for (uint32_t i = 0U; i < 16U; ++i)
    {
        state[i] = aes_sbox[state[i]];
    }
}

static void aes_shift_rows(uint8_t state[16])
{
    uint8_t temp;

    temp = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = temp;

    temp = state[2];
    state[2] = state[10];
    state[10] = temp;
    temp = state[6];
    state[6] = state[14];
    state[14] = temp;

    temp = state[3];
    state[3] = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = temp;
}

static void aes_mix_columns(uint8_t state[16])
{
    for (uint32_t col = 0U; col < 4U; ++col)
    {
        uint8_t *column = &state[col * 4U];
        const uint8_t t = column[0] ^ column[1] ^ column[2] ^ column[3];
        const uint8_t u = column[0];

        column[0] ^= t ^ aes_xtime((uint8_t)(column[0] ^ column[1]));
        column[1] ^= t ^ aes_xtime((uint8_t)(column[1] ^ column[2]));
        column[2] ^= t ^ aes_xtime((uint8_t)(column[2] ^ column[3]));
        column[3] ^= t ^ aes_xtime((uint8_t)(column[3] ^ u));
    }
}

static void aes128_encrypt_block(const uint8_t round_key[176], const uint8_t in[16], uint8_t out[16])
{
    uint8_t state[16];

    memcpy(state, in, sizeof(state));
    aes_add_round_key(state, round_key);

    for (uint32_t round = 1U; round < 10U; ++round)
    {
        aes_sub_bytes(state);
        aes_shift_rows(state);
        aes_mix_columns(state);
        aes_add_round_key(state, &round_key[round * 16U]);
    }

    aes_sub_bytes(state);
    aes_shift_rows(state);
    aes_add_round_key(state, &round_key[160]);
    memcpy(out, state, sizeof(state));
}

void boot_aes128_ctr_init(boot_aes128_ctr_context_t *ctx,
                          const uint8_t key[16],
                          const uint8_t nonce[16])
{
    aes_key_expansion(ctx->round_key, key);
    memcpy(ctx->counter, nonce, 16U);
}

static void aes_ctr_counter_at(const uint8_t base_counter[16], uint32_t block_index, uint8_t out[16])
{
    memcpy(out, base_counter, 16U);
    for (uint32_t i = 0U; i < 4U; ++i)
    {
        const uint32_t byte_index = 15U - i;
        const uint32_t add = block_index & 0xFFU;
        const uint32_t sum = (uint32_t)out[byte_index] + add;
        out[byte_index] = (uint8_t)sum;
        block_index = (block_index >> 8U) + (sum >> 8U);
    }
}

void boot_aes128_ctr_xcrypt(boot_aes128_ctr_context_t *ctx,
                            uint32_t offset,
                            uint8_t *data,
                            uint32_t length)
{
    uint32_t processed = 0U;

    while (processed < length)
    {
        uint8_t counter[AES128_BLOCK_SIZE];
        uint8_t stream[AES128_BLOCK_SIZE];
        const uint32_t absolute = offset + processed;
        const uint32_t block_offset = absolute % AES128_BLOCK_SIZE;
        uint32_t take = AES128_BLOCK_SIZE - block_offset;

        if (take > (length - processed))
        {
            take = length - processed;
        }

        aes_ctr_counter_at(ctx->counter, absolute / AES128_BLOCK_SIZE, counter);
        aes128_encrypt_block(ctx->round_key, counter, stream);
        for (uint32_t i = 0U; i < take; ++i)
        {
            data[processed + i] ^= stream[block_offset + i];
        }
        processed += take;
    }
}

static void rsa_words_from_be(uint32_t out[RSA2048_WORDS], const uint8_t in[RSA2048_BYTES])
{
    for (uint32_t i = 0U; i < RSA2048_WORDS; ++i)
    {
        const uint32_t src = RSA2048_BYTES - ((i + 1U) * 4U);
        out[i] = ((uint32_t)in[src] << 24) |
                 ((uint32_t)in[src + 1U] << 16) |
                 ((uint32_t)in[src + 2U] << 8) |
                 (uint32_t)in[src + 3U];
    }
}

static void rsa_words_to_be(uint8_t out[RSA2048_BYTES], const uint32_t in[RSA2048_WORDS])
{
    for (uint32_t i = 0U; i < RSA2048_WORDS; ++i)
    {
        const uint32_t dst = RSA2048_BYTES - ((i + 1U) * 4U);
        out[dst] = (uint8_t)(in[i] >> 24);
        out[dst + 1U] = (uint8_t)(in[i] >> 16);
        out[dst + 2U] = (uint8_t)(in[i] >> 8);
        out[dst + 3U] = (uint8_t)in[i];
    }
}

static int rsa_cmp(const uint32_t a[RSA2048_WORDS], const uint32_t b[RSA2048_WORDS])
{
    for (uint32_t i = RSA2048_WORDS; i > 0U; --i)
    {
        const uint32_t index = i - 1U;
        if (a[index] > b[index])
        {
            return 1;
        }
        if (a[index] < b[index])
        {
            return -1;
        }
    }
    return 0;
}

static void rsa_sub_in_place(uint32_t a[RSA2048_WORDS], const uint32_t b[RSA2048_WORDS])
{
    uint64_t borrow = 0U;

    for (uint32_t i = 0U; i < RSA2048_WORDS; ++i)
    {
        const uint64_t av = a[i];
        const uint64_t bv = (uint64_t)b[i] + borrow;
        a[i] = (uint32_t)(av - bv);
        borrow = (av < bv) ? 1U : 0U;
    }
}

static void rsa_add_mod(uint32_t out[RSA2048_WORDS],
                        const uint32_t a[RSA2048_WORDS],
                        const uint32_t b[RSA2048_WORDS],
                        const uint32_t mod[RSA2048_WORDS])
{
    uint64_t carry = 0U;

    for (uint32_t i = 0U; i < RSA2048_WORDS; ++i)
    {
        const uint64_t sum = (uint64_t)a[i] + b[i] + carry;
        out[i] = (uint32_t)sum;
        carry = sum >> 32;
    }

    if ((carry != 0U) || (rsa_cmp(out, mod) >= 0))
    {
        rsa_sub_in_place(out, mod);
    }
}

static void rsa_double_mod(uint32_t value[RSA2048_WORDS], const uint32_t mod[RSA2048_WORDS])
{
    uint32_t copy[RSA2048_WORDS];

    memcpy(copy, value, sizeof(copy));
    rsa_add_mod(value, copy, copy, mod);
}

static void rsa_modmul(uint32_t out[RSA2048_WORDS],
                       const uint32_t a[RSA2048_WORDS],
                       const uint32_t b[RSA2048_WORDS],
                       const uint32_t mod[RSA2048_WORDS])
{
    uint32_t result[RSA2048_WORDS] = {0};
    uint32_t temp[RSA2048_WORDS];

    memcpy(temp, a, sizeof(temp));

    for (uint32_t bit = 0U; bit < (RSA2048_WORDS * 32U); ++bit)
    {
        if ((b[bit / 32U] & (1UL << (bit % 32U))) != 0U)
        {
            uint32_t next[RSA2048_WORDS];
            rsa_add_mod(next, result, temp, mod);
            memcpy(result, next, sizeof(result));
        }
        rsa_double_mod(temp, mod);
    }

    memcpy(out, result, sizeof(result));
}

static bool rsa2048_public_decrypt(uint8_t out[RSA2048_BYTES],
                                   const uint8_t modulus_be[RSA2048_BYTES],
                                   const uint8_t signature_be[RSA2048_BYTES])
{
    uint32_t modulus[RSA2048_WORDS];
    uint32_t base[RSA2048_WORDS];
    uint32_t acc[RSA2048_WORDS];

    rsa_words_from_be(modulus, modulus_be);
    rsa_words_from_be(base, signature_be);

    if (((modulus[0] & 1U) == 0U) || (rsa_cmp(base, modulus) >= 0))
    {
        return false;
    }

    memcpy(acc, base, sizeof(acc));

    for (uint32_t i = 0U; i < 16U; ++i)
    {
        uint32_t squared[RSA2048_WORDS];
        rsa_modmul(squared, acc, acc, modulus);
        memcpy(acc, squared, sizeof(acc));
    }

    rsa_modmul(acc, acc, base, modulus);

    rsa_words_to_be(out, acc);
    return true;
}

bool boot_rsa2048_pkcs1v15_sha256_verify(const uint8_t modulus[256],
                                          const uint8_t signature[256],
                                          const uint8_t digest[BOOT_SHA256_DIGEST_SIZE])
{
    static const uint8_t sha256_digest_info_prefix[19] = {
        0x30, 0x31, 0x30, 0x0D, 0x06, 0x09, 0x60, 0x86,
        0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
        0x00, 0x04, 0x20
    };
    uint8_t encoded[RSA2048_BYTES];
    const uint32_t digest_info_offset = RSA2048_BYTES - sizeof(sha256_digest_info_prefix) - BOOT_SHA256_DIGEST_SIZE;

    if (!rsa2048_public_decrypt(encoded, modulus, signature))
    {
        return false;
    }

    if ((encoded[0] != 0x00U) || (encoded[1] != 0x01U))
    {
        return false;
    }

    for (uint32_t i = 2U; i < (digest_info_offset - 1U); ++i)
    {
        if (encoded[i] != 0xFFU)
        {
            return false;
        }
    }

    if (encoded[digest_info_offset - 1U] != 0x00U)
    {
        return false;
    }

    if (memcmp(&encoded[digest_info_offset], sha256_digest_info_prefix, sizeof(sha256_digest_info_prefix)) != 0)
    {
        return false;
    }

    return memcmp(&encoded[digest_info_offset + sizeof(sha256_digest_info_prefix)],
                  digest,
                  BOOT_SHA256_DIGEST_SIZE) == 0;
}

/* mbedTLS backend for nomiq_crypto (ESP-IDF builds; AES uses the ESP32-S3 hardware accelerator). */
#include "nomiq_crypto.h"

#include <string.h>
#include "esp_random.h"
#include "mbedtls/ccm.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"

static int rng(void *p, unsigned char *b, size_t n) { (void)p; esp_fill_random(b, n); return 0; }

int nomiq_ccm_encrypt(const uint8_t key[16], const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                      const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag, size_t tag_len)
{
    mbedtls_ccm_context c;
    mbedtls_ccm_init(&c);
    int rc = mbedtls_ccm_setkey(&c, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (rc == 0) rc = mbedtls_ccm_encrypt_and_tag(&c, len, nonce, 13, aad, aad_len, in, out, tag, tag_len);
    mbedtls_ccm_free(&c);
    return rc == 0 ? 0 : -1;
}

int nomiq_ccm_decrypt(const uint8_t key[16], const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                      const uint8_t *in, size_t len, uint8_t *out, const uint8_t *tag, size_t tag_len)
{
    mbedtls_ccm_context c;
    mbedtls_ccm_init(&c);
    int rc = mbedtls_ccm_setkey(&c, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (rc == 0) rc = mbedtls_ccm_auth_decrypt(&c, len, nonce, 13, aad, aad_len, in, out, tag, tag_len);
    mbedtls_ccm_free(&c);
    if (rc != 0) { memset(out, 0, len); return -1; }
    return 0;
}

int nomiq_hkdf_sha256(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len,
                      const uint8_t *info, size_t info_len, uint8_t *out, size_t out_len)
{
    return mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), salt, salt_len, ikm, ikm_len,
                        info, info_len, out, out_len) == 0 ? 0 : -1;
}

static int x25519(const uint8_t k[32], const uint8_t u[32] /* NULL = base point */, uint8_t out[32])
{
    mbedtls_ecp_group g; mbedtls_ecp_point P, R; mbedtls_mpi d;
    mbedtls_ecp_group_init(&g); mbedtls_ecp_point_init(&P); mbedtls_ecp_point_init(&R); mbedtls_mpi_init(&d);
    int rc = mbedtls_ecp_group_load(&g, MBEDTLS_ECP_DP_CURVE25519);
    if (rc == 0) rc = mbedtls_mpi_read_binary_le(&d, k, 32);
    if (rc == 0) {
        if (u) {
            uint8_t uu[32]; memcpy(uu, u, 32); uu[31] &= 0x7F;   /* RFC 7748: mask the top bit */
            rc = mbedtls_mpi_read_binary_le(&P.MBEDTLS_PRIVATE(X), uu, 32);
            if (rc == 0) rc = mbedtls_mpi_lset(&P.MBEDTLS_PRIVATE(Z), 1);
        } else {
            rc = mbedtls_ecp_copy(&P, &g.G);
        }
    }
    if (rc == 0) rc = mbedtls_ecp_mul(&g, &R, &d, &P, rng, NULL);
    if (rc == 0) rc = mbedtls_mpi_write_binary_le(&R.MBEDTLS_PRIVATE(X), out, 32);
    mbedtls_mpi_free(&d); mbedtls_ecp_point_free(&R); mbedtls_ecp_point_free(&P); mbedtls_ecp_group_free(&g);
    return rc == 0 ? 0 : -1;
}

int nomiq_x25519_public(uint8_t priv[32], uint8_t pub[32])
{
    priv[0] &= 248; priv[31] &= 127; priv[31] |= 64;
    return x25519(priv, NULL, pub);
}

int nomiq_x25519_shared(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t shared[32])
{
    return x25519(priv, peer_pub, shared);
}

int nomiq_random(uint8_t *buf, size_t len) { esp_fill_random(buf, len); return 0; }

_Static_assert(sizeof(nomiq_sha256_t) >= sizeof(mbedtls_sha256_context), "opaque too small");
int nomiq_sha256_init(nomiq_sha256_t *c)
{
    mbedtls_sha256_context *m = (mbedtls_sha256_context *)c->opaque;
    mbedtls_sha256_init(m);
    return mbedtls_sha256_starts(m, 0) == 0 ? 0 : -1;
}
int nomiq_sha256_update(nomiq_sha256_t *c, const uint8_t *d, size_t n)
{
    return mbedtls_sha256_update((mbedtls_sha256_context *)c->opaque, d, n) == 0 ? 0 : -1;
}
int nomiq_sha256_final(nomiq_sha256_t *c, uint8_t out[32])
{
    mbedtls_sha256_context *m = (mbedtls_sha256_context *)c->opaque;
    int rc = mbedtls_sha256_finish(m, out);
    mbedtls_sha256_free(m);
    return rc == 0 ? 0 : -1;
}

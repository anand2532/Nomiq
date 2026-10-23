/* OpenSSL backend for nomiq_crypto (host builds / unit tests). */
#include "nomiq_crypto.h"

#include <string.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

int nomiq_ccm_encrypt(const uint8_t key[16], const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                      const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag, size_t tag_len)
{
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    int l, ok = c &&
        EVP_EncryptInit_ex(c, EVP_aes_128_ccm(), NULL, NULL, NULL) &&
        EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_CCM_SET_IVLEN, 13, NULL) &&
        EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_CCM_SET_TAG, (int)tag_len, NULL) &&
        EVP_EncryptInit_ex(c, NULL, NULL, key, nonce) &&
        EVP_EncryptUpdate(c, NULL, &l, NULL, (int)len) &&
        (aad_len == 0 || EVP_EncryptUpdate(c, NULL, &l, aad, (int)aad_len)) &&
        EVP_EncryptUpdate(c, out, &l, in, (int)len) &&
        EVP_EncryptFinal_ex(c, out + l, &l) &&
        EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_CCM_GET_TAG, (int)tag_len, tag);
    EVP_CIPHER_CTX_free(c);
    return ok ? 0 : -1;
}

int nomiq_ccm_decrypt(const uint8_t key[16], const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                      const uint8_t *in, size_t len, uint8_t *out, const uint8_t *tag, size_t tag_len)
{
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    int l, ok = c &&
        EVP_DecryptInit_ex(c, EVP_aes_128_ccm(), NULL, NULL, NULL) &&
        EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_CCM_SET_IVLEN, 13, NULL) &&
        EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_CCM_SET_TAG, (int)tag_len, (void *)tag) &&
        EVP_DecryptInit_ex(c, NULL, NULL, key, nonce) &&
        EVP_DecryptUpdate(c, NULL, &l, NULL, (int)len) &&
        (aad_len == 0 || EVP_DecryptUpdate(c, NULL, &l, aad, (int)aad_len)) &&
        EVP_DecryptUpdate(c, out, &l, in, (int)len) > 0;
    EVP_CIPHER_CTX_free(c);
    if (!ok) { memset(out, 0, len); return -1; }
    return 0;
}

int nomiq_hkdf_sha256(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len,
                      const uint8_t *info, size_t info_len, uint8_t *out, size_t out_len)
{
    EVP_PKEY_CTX *k = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);
    size_t ol = out_len;
    int ok = k &&
        EVP_PKEY_derive_init(k) > 0 &&
        EVP_PKEY_CTX_set_hkdf_md(k, EVP_sha256()) > 0 &&
        EVP_PKEY_CTX_set1_hkdf_salt(k, salt, (int)salt_len) > 0 &&
        EVP_PKEY_CTX_set1_hkdf_key(k, ikm, (int)ikm_len) > 0 &&
        EVP_PKEY_CTX_add1_hkdf_info(k, info, (int)info_len) > 0 &&
        EVP_PKEY_derive(k, out, &ol) > 0 && ol == out_len;
    EVP_PKEY_CTX_free(k);
    return ok ? 0 : -1;
}

int nomiq_x25519_public(uint8_t priv[32], uint8_t pub[32])
{
    priv[0] &= 248; priv[31] &= 127; priv[31] |= 64;
    EVP_PKEY *p = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, priv, 32);
    size_t l = 32;
    int ok = p && EVP_PKEY_get_raw_public_key(p, pub, &l) && l == 32;
    EVP_PKEY_free(p);
    return ok ? 0 : -1;
}

int nomiq_x25519_shared(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t shared[32])
{
    EVP_PKEY *me = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, priv, 32);
    EVP_PKEY *peer = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, peer_pub, 32);
    EVP_PKEY_CTX *c = me ? EVP_PKEY_CTX_new(me, NULL) : NULL;
    size_t l = 32;
    int ok = c && peer && EVP_PKEY_derive_init(c) > 0 && EVP_PKEY_derive_set_peer(c, peer) > 0 &&
             EVP_PKEY_derive(c, shared, &l) > 0 && l == 32;
    EVP_PKEY_CTX_free(c);
    EVP_PKEY_free(peer);
    EVP_PKEY_free(me);
    return ok ? 0 : -1;
}

int nomiq_random(uint8_t *buf, size_t len) { return RAND_bytes(buf, (int)len) == 1 ? 0 : -1; }

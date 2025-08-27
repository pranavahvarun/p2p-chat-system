#include "encryption.h"
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

// Simple XOR function for testing
void encrypt_decrypt(char *data, const char *key) {
    size_t len = strlen(data);
    size_t key_len = strlen(key);
    for (size_t i = 0; i < len; ++i) {
        data[i] ^= key[i % key_len];
    }
}


void secure_bzero(void *ptr, size_t len) {
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) *p++ = 0;
}

void derive_key_from_password(const char *password, unsigned char key[ENC_KEY_LEN]) {
    SHA256((const unsigned char *)password, strlen(password), key);
}

int encrypt_message(const unsigned char *plaintext, int plaintext_len,
                    const unsigned char key[ENC_KEY_LEN],
                    unsigned char *out, int out_cap) {
    if (!plaintext || !out || plaintext_len < 0) return -1;

    int need = ENC_IV_LEN + plaintext_len + ENC_IV_LEN;
    if (out_cap < need) return -1;

    unsigned char *iv = out;
    unsigned char *cipher = out + ENC_IV_LEN;

    if (RAND_bytes(iv, ENC_IV_LEN) != 1) return -1;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    int len = 0, cipher_len = 0;

    if (EVP_EncryptUpdate(ctx, cipher, &len, plaintext, plaintext_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    cipher_len = len;

    if (EVP_EncryptFinal_ex(ctx, cipher + cipher_len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    cipher_len += len;

    EVP_CIPHER_CTX_free(ctx);

    return ENC_IV_LEN + cipher_len;
}

int decrypt_message(const unsigned char *in, int in_len,
                    const unsigned char key[ENC_KEY_LEN],
                    unsigned char *plaintext, int plaintext_cap) {
    if (!in || in_len < ENC_IV_LEN || !plaintext) return -1;

    const unsigned char *iv = in;
    const unsigned char *cipher = in + ENC_IV_LEN;
    int cipher_len = in_len - ENC_IV_LEN;

    if (plaintext_cap < cipher_len) return -1;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    int len = 0, pt_len = 0;

    if (EVP_DecryptUpdate(ctx, plaintext, &len, cipher, cipher_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    pt_len = len;

    if (EVP_DecryptFinal_ex(ctx, plaintext + pt_len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    pt_len += len;

    EVP_CIPHER_CTX_free(ctx);
    return pt_len;
}

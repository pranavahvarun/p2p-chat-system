#ifndef ENCRYPTION_H
#define ENCRYPTION_H

#include <stddef.h>
#include <stdint.h>

#define ENC_KEY_LEN 32       // AES-256 key size
#define ENC_IV_LEN 16        // AES block size for CBC
#define ENC_MAX_IN  4096     // max plaintext per message

void encrypt_decrypt(char *data, const char *key);

void derive_key_from_password(const char *password, unsigned char key[ENC_KEY_LEN]);

int encrypt_message(const unsigned char *plaintext, int plaintext_len,
                    const unsigned char key[ENC_KEY_LEN],
                    unsigned char *out, int out_cap);

int decrypt_message(const unsigned char *in, int in_len,
                    const unsigned char key[ENC_KEY_LEN],
                    unsigned char *plaintext, int plaintext_cap);

void secure_bzero(void *ptr, size_t len);

#endif // ENCRYPTION_H

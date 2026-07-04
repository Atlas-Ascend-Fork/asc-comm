/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2020. All rights reserved.
 * Description: encrypt and decrypt private key header file
 * Create: 2020-06-03
 */

#ifndef _ENCRYPT_H_
#define _ENCRYPT_H_

#include "tls_common.h"

/*lint -e116*/
typedef struct do_encry_info {
    const unsigned char *key; /* KEK, use for aes algo */
    unsigned char *iv;      /* initial vector, use for aes algo */
    unsigned char *in;      /* origin private key pointer */
    int            in_len;  /* private key length */
    unsigned char *tag;     /* tag, check for dec */
    int            tag_len; /* tag length */
    int            en_dec;  /* encrypt or decrypt */
} crypt_info;  //lint !e17

typedef struct do_hash_info {
    unsigned char *pwd;      /* pwd */
    int            pwd_len;  /* len of pwd */
    unsigned char *salt;     /* salt for gen KEY */
    int            salt_len; /* len of salt */
    int            iter;     /* number of iterations */
} hash_info;  //lint !e17
/*lint +e116*/

int crypto_gen_key_with_pbkdf2(hash_info *input, unsigned char *ky, unsigned int kylen);
TLS_ATTRI_VISI_DEF int crypto_encrypt_with_aes_gcm(crypt_info *info, unsigned char *out_buf, unsigned int *out_len);
int crypto_decrypt_with_aes_gcm(crypt_info *info, unsigned char *out_buf, unsigned int *out_len);
#endif

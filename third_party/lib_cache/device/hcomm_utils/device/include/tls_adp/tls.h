/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2019. All rights reserved.
 * Description: tls statement
 * Create: 2019-12-11
 */

#ifndef __TLS_H
#define __TLS_H
#include <stdint.h>
#if defined (CA_CONFIG_LLT) || defined (CONFIG_LLT)
#include "stub_ssl.h"
#else
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/ossl_typ.h>
#include <openssl/crypto.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#endif

#define TLS_RSA_KY_BITS_MIN_LEN 2048
#define TLS_DSA_KY_BITS_MIN_LEN 2048
#define TLS_DH_KY_BITS_MIN_LEN 2048
#define TLS_EC_KY_BITS_MIN_LEN 256
#define TLS_KY_NONCE_LEN 48
#define TLS_KY_RSA_LEN   512

#define MAX_TLS_CFG_COUNT 18
#define MAX_TLS_CA_ALIAS_LEN 63
#define TLS_RES_LEN 160

#define MAX_CERT_COUNT 15
#define MAX_SHOW_INFO_COUNT 16
#define MAX_CA_CERT_INDEX 14
#define CA_CERT_BEGIN_INDEX 2

#define CERT_MAX_SIZE 3072
#define OLD_CERT_MAX_SIZE 2048
#define KY_MAX_SIZE 5120
#define PUB_KY_MAX_SIZE 3072
#define CRL_MAX_SIZE (1024 * 20)
#define CERT_NAME_MAX_LEN 64

#define TIME_LEN 26
#define TLS_TYPE_LEN 10

#define TLS_MAGIC_WORDS_LEN 8

#define TLS_SALT_LEN 48
#define IV_LEN 16

#define BLOCK_KY_LEN 32

#define PWD_MIN_LEN 8
#define PWD_MAX_LEN 15
#define PWD_MAX_ENC_LEN 15
#define FGETS_MAX_LEN 32
#define PWD_ENC_LEN 256
#define WORK_KEY_LEN 516
#define TAG_LEN 16

#define  ENVELOPE_SYMM_KY_LEN 32
#define  ENVELOPE_SYMM_ENC_KY_LEN 512

#define PWD_TPYE_CNT 4
#define PWD_NUM_INDEX 0
#define PWD_LOW_LET_INDEX 1
#define PWD_UP_LET_INDEX 2
#define PWD_SYMBOL_INDEX 3

#define PWD_COMPLEXITY_THR 2

#define TLS_ITER_MAX_NUM 10000

#define START_TIME 0
#define END_TIME 1
#define YEAR_MON_DAY_INDEX 2
#define YEAR_MON_DAY_LEN 8
#define TLS_DEFAULT_ALARM_TIME 60 // day
#define TLS_DAY_TO_S (60 * 60 * 24) // s

#define TLS_PRI_PLAINTEXT 0
#define TLS_PRI_CIPHERTEXT 1
#define TLS_PUB_PLAINTEXT 2

#define TLS_DEC_MODE 0
#define TLS_ENC_MODE 1

#define TLS_VERSION 2

#define TLS_CERT_ILLEGAL (-100)

enum tls_err_num {
    TLS_CERT_LOAD_ERR = -1,
    TLS_CERT_VERIFY_ERR = -2,
    TLS_CERT_KYMATCH_ERR = -3,
    TLS_CERT_LACK_PUB_ERR = -4,
    TLS_CERT_DISCONSEQ_ERR = -5,
    TLS_CERT_CTX_INIT_ERR = -6,
};

#define TLS_CA_CERT (-1)

enum tls_cert_tpye {
    TLS_PUB_CERT = 0,
    TLS_CA1_CERT,
    TLS_CA2_CERT,
    TLS_CA3_CERT,
    TLS_CA4_CERT,
    TLS_CA5_CERT,
    TLS_CA6_CERT,
    TLS_CA7_CERT,
    TLS_CA8_CERT,
    TLS_CA9_CERT,
    TLS_CA10_CERT,
    TLS_CA11_CERT,
    TLS_CA12_CERT,
    TLS_CA13_CERT,
    TLS_CA14_CERT,

    TLS_PRI_KY,

    TLS_CRL,
    TLS_HOST,
};

#define TLS_SAVE_TO_FlASH 0
#define TLS_SAVE_TO_FILE 1
#define TLS_ENABLE_INVALID 0xFFFFFFFF

#define TLS_MNG_INFO_DEFAULT_VERSION   0x0
// flash mode won't use kmc
#define TLS_MNG_INFO_VERSION_1         0x00010000

struct tls_cert_mng_info {
    char magic_words[TLS_MAGIC_WORDS_LEN]; /* 1234567 */
    unsigned int cert_count; /* num of certs */
    int state; /* 0:not ok 1:ok */
    unsigned int ca_wcout; /* counts of ca writing flash */
    unsigned int cert_ky_wcout; /* counts of eqpt and key writing flash */
    unsigned int crl_wcout; /* counts of crl writing flash */
    unsigned int crl_len; /* len of crl */
    unsigned int ky_len; /* len of key */
    unsigned int ky_enc_len; /* len of enc key */
    unsigned char salt[TLS_SALT_LEN]; /* salt */
    unsigned int salt_size; /* len of salt */
    unsigned int cert_len[MAX_CERT_COUNT];
    unsigned int total_cert_len; /* not include head only len of certs */
    unsigned int tls_enable;
    unsigned int tls_alarm;
    unsigned int pwd_len; /* len of pwd */
    unsigned int pwd_enc_len; /* len of enc pwd */
    union {
        unsigned char enc_pwd[PWD_ENC_LEN];
        unsigned char pwd[PWD_ENC_LEN];
    };
    unsigned int work_key_len; /* len of work_key */
    unsigned char work_key[WORK_KEY_LEN];
    unsigned char iv[IV_LEN]; /* initial vector */
    unsigned int iv_size; /* len of initial vector */
    unsigned char tag[TAG_LEN];
    unsigned int tag_len;
    unsigned int save_mode;
    unsigned char envelope_iv[IV_LEN]; /* initial vector for envelope */
    unsigned char envelope_tag[TAG_LEN];
    unsigned int version;
    char res[TLS_RES_LEN];
};

#define TLS_CA_SSL_NEW_CERT_LEN 3072
#define TLS_CA_SSL_MAX_NEW_CERT_NUM  8
#define TLS_CA_SSL_MAX_FLASH_NUM  2 // falsh块cb1与cb2为一组，cb3与cb4为一组
#define TLS_CA_SSL_NEW_CERT_ALIAS_LEN 64
#define TLS_CA_SSL_RSV_LEN 7160

struct tls_ca_new_cert_info {
    char ncert_info[TLS_CA_SSL_NEW_CERT_LEN];
};

struct tls_ca_alias_names {
    char name[TLS_CA_SSL_NEW_CERT_ALIAS_LEN];        // 证书别名
    char thumbprint[TLS_CA_SSL_NEW_CERT_ALIAS_LEN];  // 证书指纹
};

// 910B旧证书格式
struct tls_cert_info {
    char certInfo[OLD_CERT_MAX_SIZE];
};

// 实验局天工款型旧证书格式
struct tls_atlas_9000_cert_info {
    char certInfo[TLS_CA_SSL_NEW_CERT_LEN];
};

// 新证书格式。每个cb里面都需要存储：8个3072字节的证书、8个证书对应别名、证书数量、预留字段大小
struct tls_ca_new_certs {
    struct tls_ca_new_cert_info certs[TLS_CA_SSL_MAX_NEW_CERT_NUM];
    struct tls_ca_alias_names alias[TLS_CA_SSL_MAX_NEW_CERT_NUM];
    unsigned int ncert_count;
    char res[TLS_CA_SSL_RSV_LEN];
};

struct tls_ky_info {
    unsigned char ky_info[KY_MAX_SIZE];
};

struct tls_crl_info {
    unsigned char crl_info[CRL_MAX_SIZE];
};

struct tls_pwd_info {
    unsigned char pwd_info[PWD_MAX_LEN + 1];
};

struct envelope_symm_enc_ky_info {
    unsigned int symm_enc_ky_len;
    unsigned char symm_enc_ky[ENVELOPE_SYMM_ENC_KY_LEN];
};

struct tls_cert_ky_crl_info {
    struct tls_cert_mng_info mng;
    struct tls_ky_info ky;
    struct tls_cert_info certs[MAX_CERT_COUNT];
    struct tls_ca_new_certs ncerts[TLS_CA_SSL_MAX_FLASH_NUM];
    struct tls_crl_info crl;
    struct tls_pwd_info pwd;
    struct envelope_symm_enc_ky_info symm_enc_ky_info;
};

struct tls_alarm_info {
    unsigned int alarm;
    unsigned int save_mode;
};

struct tls_enable_info {
    unsigned int enable;
    unsigned int save_mode;
    int machine_type;
};

struct tls_cert_show_info {
    unsigned int tls_alarm;
    unsigned int tls_enable;
    char issuer[CERT_NAME_MAX_LEN];
    char subject[CERT_NAME_MAX_LEN];
    char start_time[TIME_LEN];
    char end_time[TIME_LEN];
};

struct leaf_cert_info {
    X509 *leaf_cert;
    unsigned int leaf_cert_idx;
};

#define HCCP_CERTS_MNG_NAME   "hccp_certs_mng_cb"
#define HCCP_CERTS_EQPT_NAME  "hccp_certs_eqpt_cb"
#define HCCP_CERTS_EQPT1_NAME  "hccp_certs_eqpt_cb1"
#define HCCP_CERTS_EQPT2_NAME  "hccp_certs_eqpt_cb2"
#define HCCP_CERTS_EQPT3_NAME  "hccp_certs_eqpt_cb3"
#define HCCP_CERTS_EQPT4_NAME  "hccp_certs_eqpt_cb4"
#define HCCP_PRI_DATA_NAME    "hccp_pri_data_cb"
#define HCCP_CERTS_REVOC_NAME "hccp_certs_revoc_cb"

#define MAGIC_WORD_FOR_TLS "1234567"

#define KMC_SECU_PATH_LEN 64
#define KMC_STORE_PATH_LEN 64

#define TLS_LOCK_FILE_LEN 128
#define TLS_HOST_SAVE_PATH_LEN 128
#define MAX_TLS_LEN         30

struct tls_ky_match_info {
    unsigned char pri_ky_info[KY_MAX_SIZE];
    unsigned char pub_ky_info[PUB_KY_MAX_SIZE];
    unsigned int pri_ky_len;
    unsigned int pub_ky_len;
    unsigned int pub_type;
    unsigned int pri_type;
    uint8_t random[TLS_KY_NONCE_LEN];
    uint8_t sig[TLS_KY_RSA_LEN];
    size_t random_len;
    size_t sig_len;
};

#define  ENVELOPE_PUB_CERT 0
#define  ENVELOPE_PUB_KY 1

struct digital_envelope_mng_info {
    unsigned int pri_ky_len; /* len of pri key */
    unsigned int pri_ky_enc_len; /* len of enc pri key */
    unsigned int work_key_len; /* len of work_key */
    unsigned char work_key[WORK_KEY_LEN];
};

struct digital_envelope_info {
    struct digital_envelope_mng_info mng;
    unsigned char pri_ky_info[KY_MAX_SIZE];
};

struct symmetric_enc_info {
    unsigned char *iv;
    unsigned char *tag;
    unsigned char *out_buf;
    unsigned int *out_len;
};

struct envelope_pub_info {
    unsigned int pub_ky_len;
    unsigned char pub_ky_info[PUB_KY_MAX_SIZE];
};

struct envelope_pri_info {
    unsigned int pri_ky_len;
    unsigned char pri_ky_info[KY_MAX_SIZE];
};

struct envelope_kmc_info {
    unsigned int work_key_len;
    unsigned char work_key[WORK_KEY_LEN];
};

#ifndef CONFIG_LLT
#define TLS_PWD_SECU_PATH "%s/tls_%d.secu"
#define TLS_PWD_STORE_PATH "%s/tls_%d.store"
#define TLS_LOCK_FILE_NAME "%s/tls_file.lock"
#else
#define TLS_PWD_SECU_PATH "/var/log/tls.secu"
#define TLS_PWD_STORE_PATH "/var/log/tls.store"
#define TLS_LOCK_FILE_NAME "/var/log/tls_file.lock"
#endif

#define ENCRYPTED_FLAG "ENCRYPTED"
#define VALID_ENCCRY_ALGO "AES-256"
extern char* strptime(const char* restrict, const char* format, struct tm* restr);  //lint !e116
#endif

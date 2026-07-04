/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2014-2020. All rights reserved.
 * Description: error code
 * Author: Luan Shipeng l00171031
 * Create: 2014-06-16
 * History: 2018-10-08 Zhang Jie (employee ID: 00316590) Rectification by UK
 */

#ifndef KMC_INCLUDE_WSECV2_ERRORCODE_H
#define KMC_INCLUDE_WSECV2_ERRORCODE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Note: Define this macro in the IDE project instead of in the wsecv2_config.h
 * file to ensure that the app provides the correct definition.
 */
#ifndef WSEC_ERR_CODE_BASE
#define WSEC_ERR_CODE_BASE 0
#endif

#if (WSEC_ERR_CODE_BASE == 0)
    #define WSEC_ERROR_CODE(seq) ((unsigned long)(seq))
    #define WSEC_BASE_ERROR_CODE(code) ((unsigned int)(code))
#else
    #define WSEC_ERROR_CODE(seq) ((unsigned long)(WSEC_ERR_CODE_BASE + seq))
    #define WSEC_BASE_ERROR_CODE(code) ((unsigned int)(code - WSEC_ERR_CODE_BASE))
#endif

#define WSEC_SUCCESS                                           (unsigned long)0    /* Success */
#define WSEC_FAILURE                                           WSEC_ERROR_CODE(1)   /* Common error. */

/* File operation error. */
#define WSEC_ERR_OPEN_FILE_FAIL                                WSEC_ERROR_CODE(11)   /* Failed to open the file. */
#define WSEC_ERR_READ_FILE_FAIL                                WSEC_ERROR_CODE(12)   /* Failed to read the file. */
#define WSEC_ERR_WRI_FILE_FAIL                                 WSEC_ERROR_CODE(13)   /* Failed to write the file. */
#define WSEC_ERR_GET_FILE_LEN_FAIL                             WSEC_ERROR_CODE(14)   /* Failed to obtain the file length. */
#define WSEC_ERR_FILE_FORMAT                                   WSEC_ERROR_CODE(15)   /* Incorrect file format. */
#define WSEC_ERR_FILE_COPY_FAIL                                WSEC_ERROR_CODE(16)   /* Failed to copy the file. */
#define WSEC_ERR_FILE_FLUSH_FAIL                               WSEC_ERROR_CODE(17)   /* Failed to synchronize files. */
#define WSEC_ERR_FILE_LEN_TOO_MAX                              WSEC_ERROR_CODE(18)   /* File is too max. */

/* Memory operation error. */
#define WSEC_ERR_MALLOC_FAIL                                   WSEC_ERROR_CODE(51)  /* Failed to allocate memory. */
#define WSEC_ERR_MEMCPY_FAIL                                   WSEC_ERROR_CODE(52)  /* Failed to copy the memory. */
#define WSEC_ERR_MEMCLONE_FAIL                                 WSEC_ERROR_CODE(53)  /* Failed to clone the memory. */
#define WSEC_ERR_STRCPY_FAIL                                   WSEC_ERROR_CODE(54)  /* Failed to copy the character string. */
#define WSEC_ERR_OPER_ARRAY_FAIL                               WSEC_ERROR_CODE(55)  /* Array operation failed. */
#define WSEC_ERR_MEMSET_FAIL                                   WSEC_ERROR_CODE(56)  /* Failed to set the memory. */
#define WSEC_ERR_ARRAY_ITEM_REPEAT                             WSEC_ERROR_CODE(57)  /* Element repeat in array. */

/* Security function processing error. */
#define WSEC_ERR_CRPTO_LIB_FAIL                                WSEC_ERROR_CODE(101) /* Failed to operate the algorithm library (iPSI). */
#define WSEC_ERR_GEN_HASH_CODE_FAIL                            WSEC_ERROR_CODE(102) /* Failed to generate the hash value. */
#define WSEC_ERR_HASH_NOT_MATCH                                WSEC_ERROR_CODE(103) /* The hash value does not match. */
#define WSEC_ERR_INTEGRITY_FAIL                                WSEC_ERROR_CODE(104) /* Integrity is damaged. */
#define WSEC_ERR_HMAC_FAIL                                     WSEC_ERROR_CODE(105) /* HMAC failure */
#define WSEC_ERR_HMAC_AUTH_FAIL                                WSEC_ERROR_CODE(106) /* HMAC verification failed. */
#define WSEC_ERR_GET_RAND_FAIL                                 WSEC_ERROR_CODE(107) /* Failed to obtain the random number. */
#define WSEC_ERR_PBKDF2_FAIL                                   WSEC_ERROR_CODE(108) /* Failed to derive the key. */
#define WSEC_ERR_ENCRPT_FAIL                                   WSEC_ERROR_CODE(109) /* Failed to encrypt data. */
#define WSEC_ERR_DECRPT_FAIL                                   WSEC_ERROR_CODE(110) /* Failed to decrypt the data. */
#define WSEC_ERR_GET_ALG_NAME_FAIL                             WSEC_ERROR_CODE(111) /* Failed to obtain the security algorithm name. */

/* Function invoking error. */
#define WSEC_ERR_INVALID_ARG                                   WSEC_ERROR_CODE(151) /* Invalid parameter. */
#define WSEC_ERR_OUTPUT_BUFF_NOT_ENOUGH                        WSEC_ERROR_CODE(152) /* The output buffer is insufficient. */
#define WSEC_ERR_INPUT_BUFF_NOT_ENOUGH                         WSEC_ERROR_CODE(153) /* The input buffer is insufficient. */
#define WSEC_ERR_CANCEL_BY_APP                                 WSEC_ERROR_CODE(154) /* Canceling an App Operation */
#define WSEC_ERR_INVALID_CALL_SEQ                              WSEC_ERROR_CODE(155) /* The app invoking sequence is incorrect. */
#define WSEC_ERR_CALLBACKS_NOT_REG                             WSEC_ERROR_CODE(156) /* The callback function is not registered. */
#define WSEC_ERR_INVALID_INSTANCE                              WSEC_ERROR_CODE(157) /* Using the instance flag wrong */
#define WSEC_ERR_USE_WRONG_RANDOM_CALLBACK                     WSEC_ERROR_CODE(158) /* The callback and instance type do not match. */

/* System operation error. */
#define WSEC_ERR_GET_CURRENT_TIME_FAIL                         WSEC_ERROR_CODE(201) /* Failed to obtain the current time. */
#define WSEC_ERR_CALC_DIFF_DAY_FAIL                            WSEC_ERROR_CODE(202) /* An error occurred when calculating the time difference. */

/* KMC error */
#define WSEC_ERR_KMC_CALLBACK_KMCCFG_FAIL                      WSEC_ERROR_CODE(251) /* Failed to obtain KMC configuration data. */
#define WSEC_ERR_KMC_KMCCFG_INVALID                            WSEC_ERROR_CODE(252) /* Invalid KMC configuration data. */
#define WSEC_ERR_KMC_KSF_DATA_INVALID                          WSEC_ERROR_CODE(253) /* Invalid configuration data exists in the Keystore. */
#define WSEC_ERR_KMC_INI_MUL_CALL                              WSEC_ERROR_CODE(254) /* Initialization is invoked for multiple times. */
#define WSEC_ERR_KMC_NOT_KSF_FORMAT                            WSEC_ERROR_CODE(255) /* The file is not in the Keystore format. */
#define WSEC_ERR_KMC_READ_DIFF_VER_KSF_FAIL                    WSEC_ERROR_CODE(256) /* Failed to read the key store file of another version. */
#define WSEC_ERR_KMC_READ_MK_FAIL                              WSEC_ERROR_CODE(257) /* Failed to read the MK. */
#define WSEC_ERR_KMC_MK_LEN_TOO_LONG                           WSEC_ERROR_CODE(258) /* The MK key is too long. */
#define WSEC_ERR_KMC_REG_REPEAT_MK                             WSEC_ERROR_CODE(259) /* The MK to be registered is duplicate. */
#define WSEC_ERR_KMC_ADD_REPEAT_DOMAIN                         WSEC_ERROR_CODE(260) /* You are trying to add a duplicate domain (with the same ID). */
#define WSEC_ERR_KMC_ADD_REPEAT_KEY_TYPE                       WSEC_ERROR_CODE(261) /* Duplicate key type (in the same domain). */
#define WSEC_ERR_KMC_ADD_REPEAT_MK                             WSEC_ERROR_CODE(262) /* Duplicate KeyId (KeyId in the same domain) */
#define WSEC_ERR_KMC_DOMAIN_MISS                               WSEC_ERROR_CODE(263) /* The domain does not exist. */
#define WSEC_ERR_KMC_DOMAIN_KEYTYPE_MISS                       WSEC_ERROR_CODE(264) /* The DOMAIN KeyType does not exist. */
#define WSEC_ERR_KMC_DOMAIN_NUM_OVERFLOW                       WSEC_ERROR_CODE(265) /* The number of configured domains exceeds the upper limit. */
#define WSEC_ERR_KMC_KEYTYPE_NUM_OVERFLOW                      WSEC_ERROR_CODE(266) /* The number of configured key types exceeds the upper limit. */
#define WSEC_ERR_KMC_MK_NUM_OVERFLOW                           WSEC_ERROR_CODE(267) /* The number of MKs exceeds the threshold. */
#define WSEC_ERR_KMC_MK_MISS                                   WSEC_ERROR_CODE(268) /* The MK does not exist. */
#define WSEC_ERR_KMC_RECREATE_MK                               WSEC_ERROR_CODE(269) /* Failed to re-create the MK. */
#define WSEC_ERR_KMC_CBB_NOT_INIT                              WSEC_ERROR_CODE(270) /* The CBB has not been initialized. */
#define WSEC_ERR_KMC_CANNOT_REG_AUTO_KEY                       WSEC_ERROR_CODE(271) /* The key automatically generated by the system cannot be registered. */
#define WSEC_ERR_KMC_CANNOT_RMV_ACTIVE_MK                      WSEC_ERROR_CODE(272) /* The MK in the active state cannot be deleted. */
#define WSEC_ERR_KMC_CANNOT_SET_EXPIRETIME_FOR_INACTIVE_MK     WSEC_ERROR_CODE(273) /* The inactive MK cannot be set to expire. */
#define WSEC_ERR_KMC_RK_GENTYPE_REJECT_THE_OPER                WSEC_ERROR_CODE(274) /* The RK generation mode does not support this operation. */
#define WSEC_ERR_KMC_MK_GENTYPE_REJECT_THE_OPER                WSEC_ERROR_CODE(275) /* The MK generation mode does not support this operation. */
#define WSEC_ERR_KMC_ADD_DOMAIN_DISCREPANCY_MK                 WSEC_ERROR_CODE(276) /* The domain to be added conflicts with the residual MK. */
#define WSEC_ERR_KMC_IMPORT_MK_CONFLICT_DOMAIN                 WSEC_ERROR_CODE(277) /* The imported MK conflicts with the domain configuration. */
#define WSEC_ERR_KMC_CANNOT_ACCESS_PRI_DOMAIN                  WSEC_ERROR_CODE(278) /* The private domain of the CBB cannot be accessed. */
#define WSEC_ERR_KMC_INVALID_ROLETYPE                          WSEC_ERROR_CODE(279) /* Invalid identity information. */
#define WSEC_ERR_KMC_ROLLBACK_FAIL                             WSEC_ERROR_CODE(280) /* Rollback failed. */
#define WSEC_ERR_KMC_INVALID_KEYHASH_LEN                       WSEC_ERROR_CODE(281) /* The hash length of the key entered for query is incorrect. */
#define WSEC_ERR_KMC_CANNOT_FIND_ACTIVEKEY                     WSEC_ERROR_CODE(282) /* No valid key is found. */
#define WSEC_ERR_KMC_KEYSTOREMEM_NOTEXIST                      WSEC_ERROR_CODE(283) /* The keystore does not exist. */
#define WSEC_ERR_KMC_KEYCFGMEM_NOTEXIST                        WSEC_ERROR_CODE(284) /* keycfg does not exist. */
#define WSEC_ERR_KMC_MKID_OVERFLOW                             WSEC_ERROR_CODE(285) /* The key used for intra-domain registration is reversed. */
#define WSEC_ERR_KMC_READMK_NOTCOMPLETE                        WSEC_ERROR_CODE(286) /* The MK is not completely read. */
#define WSEC_ERR_KMC_KSF_CORRUPT                               WSEC_ERROR_CODE(287) /* The KMC Ksf is damaged. */
#define WSEC_ERR_LARGER_THAN_MAX_MK_RECORD_LEN                 WSEC_ERROR_CODE(288) /* The number of MK records exceeds the maximum. */
#define WSEC_ERR_KMC_KSF_VERSION_INVALID                       WSEC_ERROR_CODE(289) /* The KSF version number is incorrect. */
#define WSEC_ERR_KMC_HARDWARE_RK_NOT_FOUND                     WSEC_ERROR_CODE(290) /* The hardware root key does not exist. */
#define WSEC_ERR_KMC_SYNC_MK_FAILED                            WSEC_ERROR_CODE(291) /* Sync MK failed */
#define WSEC_ERR_KMC_DOMAIN_TYPE_ERROR                         WSEC_ERROR_CODE(292) /* The MK domain type is incorrect. */
#define WSEC_ERR_KMC_FILTER_MK_COUNT_ZERO                      WSEC_ERROR_CODE(293) /* The MKs that meet the search criteria (domainId and domainType) are not filtered during import and export. */
#define WSEC_ERR_KMC_IMPORT_MK_NUM_OVERFLOW                    WSEC_ERROR_CODE(294) /* The number of MKs imported to the KSF exceeds the threshold. */
#define WSEC_ERR_KMC_INVALID_IMPORT_TYPE                       WSEC_ERROR_CODE(295) /* Import mk type is invalid, only support ADD and REPLACE mode currently. */
#define WSEC_ERR_KMC_MK_NOT_SUPPORT_EXP_IMP                    WSEC_ERROR_CODE(296) /* The MK not support export or import. */
#define WSEC_ERR_KMC_BATCH_DOAMIN_COUNT_EXCEED                 WSEC_ERROR_CODE(297) /* Batch domainId count is exceed */
#define WSEC_ERR_KMC_SYNC_CNF_COUNT_FAILED                     WSEC_ERROR_CODE(298) /* Sync (send/recv) msg count failed */
#define WSEC_ERR_KMC_SYNC_CNF_COUNT_INVALID                    WSEC_ERROR_CODE(299) /* Sync (send/recv) msg count invalid */
#define WSEC_ERR_KMC_SYNC_CNF_SEND_FAILED                      WSEC_ERROR_CODE(300) /* Sync msg count - send failed */
#define WSEC_ERR_KMC_SYNC_CNF_RECV_FAILED                      WSEC_ERROR_CODE(301) /* Sync msg count - recv failed */
#define WSEC_ERR_KMC_SYNC_HEADER_SEND_FAILED                   WSEC_ERROR_CODE(302) /* Sync msg header failed - send/recv failed */
#define WSEC_ERR_KMC_SYNC_HEADER_RECV_FAILED                   WSEC_ERROR_CODE(303) /* Sync msg header failed - send/recv failed */
#define WSEC_ERR_KMC_SYNC_HEADER_MSGTYPE_INVALID               WSEC_ERROR_CODE(304) /* Sync msg header failed - msg type invalid */
#define WSEC_ERR_KMC_SYNC_HEADER_VER_INVALID                   WSEC_ERROR_CODE(305) /* Sync msg header failed - header version invalid */
#define WSEC_ERR_KMC_SYNC_HEADER_MSGLEN_INVALID                WSEC_ERROR_CODE(306) /* Sync msg header failed - msg length invalid */
#define WSEC_ERR_KMC_SYNC_AGENT_RESPONSE                       WSEC_ERROR_CODE(307) /* Sync agent response failed */
#define WSEC_ERR_KMC_PRI_ALG_NOT_SUPPORTED                     WSEC_ERROR_CODE(320) /* The algorithm does not exist or is not supported. */

/* SDP error. */
#define WSEC_ERR_SDP_PWD_VERIFY_FAIL                           WSEC_ERROR_CODE(351) /* Failed to verify the password ciphertext. */
#define WSEC_ERR_SDP_CONFIG_INCONSISTENT_WITH_USE              WSEC_ERROR_CODE(352) /* The configured data is inconsistent with the used data. */
#define WSEC_ERR_SDP_INVALID_CIPHER_TEXT                       WSEC_ERROR_CODE(353) /* The ciphertext format is incorrectly parsed. */
#define WSEC_ERR_SDP_VERSION_INCOMPATIBLE                      WSEC_ERROR_CODE(354) /* The ciphertext version is incompatible with the current version. */
#define WSEC_ERR_SDP_ALG_NOT_SUPPORTED                         WSEC_ERROR_CODE(355) /* The algorithm does not exist or is not supported. */
#define WSEC_ERR_SDP_DOMAIN_UNEXPECTED                         WSEC_ERROR_CODE(356) /* The ciphertext is from an unexpected domain. */
#define WSEC_ERR_SDP_CIPHER_LENGTH_NOT_ENOUGH                  WSEC_ERROR_CODE(357) /* The length of the entered ciphertext is insufficient. */
#define WSEC_ERR_SDP_ZERO_CIPHER_LENGTH                        WSEC_ERROR_CODE(358) /* During decryption, the ciphertext length in the ciphertext header is 0. */
#define WSEC_ERR_SDP_RAND_INIT_FAILED                          WSEC_ERROR_CODE(359) /* Failed to initialize the random number RNG (DRBG). */
#define WSEC_ERR_SDP_ITER_UNEXPECTED                           WSEC_ERROR_CODE(360) /* The number of iterations does not meet the expectation. */
#define WSEC_ERR_SDP_HEADER_CORRUPT                            WSEC_ERROR_CODE(361) /* The sdp header corrupt. */
#define WSEC_ERR_SDP_HEADER_TYPE                               WSEC_ERROR_CODE(362) /* The sdp header type err. */
#define WSEC_ERR_SDP_ARG_CIPHER_LEN                            WSEC_ERROR_CODE(363)
#define WSEC_ERR_SDP_ARG_PLAIN_LEN                             WSEC_ERROR_CODE(364)
#define WSEC_ERR_SDP_ARG_SALT_LEN                              WSEC_ERROR_CODE(365)

/* TPM */
#define WSEC_ERR_TPM_CAPABILITY_NOT_GOT                        WSEC_ERROR_CODE(500)
#define WSEC_ERR_TPM_RESOURCE_NOT_ENOUGH                       WSEC_ERROR_CODE(501)

/* Memory information protection */
#define WSEC_ERR_MEMINFO_PROTECT_FAIL                          WSEC_ERROR_CODE(600)
#define WSEC_ERR_MASK_INIT_FAIL                                WSEC_ERROR_CODE(601) /* Failed to initialize memory information protection. */
#define WSEC_ERR_KEYRING_REQKEY                                WSEC_ERROR_CODE(602)
#define WSEC_ERR_KEYRING_READKEY                               WSEC_ERROR_CODE(603)
#define WSEC_ERR_KEYRING_COMPAREKEY                            WSEC_ERROR_CODE(604)

/* common tlv error */
#define WSEC_ERR_TLV_CORRUPT                                   WSEC_ERROR_CODE(800) /* The sdp header tlv corrupt. */
#define WSEC_ERR_MATCH_TLV_TAG                                 WSEC_ERROR_CODE(801) /* Not find match tlv tag. */
#define WSEC_ERR_MATCH_TLV_LEN                                 WSEC_ERROR_CODE(802) /* the tlv value len err. */
#define WSEC_ERR_FILL_TLV                                      WSEC_ERROR_CODE(803) /* fill tlv err. */

#define WSEC_ERR_MAX                                           WSEC_ERROR_CODE(5000) /* Maximum CBB Error Code */

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* KMC_INCLUDE_WSECV2_ERRORCODE_H */

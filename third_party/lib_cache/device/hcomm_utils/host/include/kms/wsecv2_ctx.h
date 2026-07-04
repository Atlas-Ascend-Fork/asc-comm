/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2021. All rights reserved.
 * Description: KMC MultiInstance context header file define the members of context.
 * Author: y00585410
 * Create: 2021-08-06
 * History: None
 */

#ifndef KMC_SRC_COMMON_WSECV2_CTX_H
#define KMC_SRC_COMMON_WSECV2_CTX_H

#include "wsecv2_type.h"
#include "wsecv2_multitype.h"
#include "wsecv2_array.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

typedef enum {
    KMC_SINGEL_INSTANCE = 0,
    KMC_MULTI_INSTANCE
} KmcMultiInstanceFlag;

/* lock */
typedef enum {
    WSEC_FUNC_UNREG = 0,
    WSEC_FUNC_REG
} WsecFuncRegState;

typedef enum {
    LOCK4KEYSTORE = 0, /* Keystore data and corresponding files in the memory */
    LOCK4KMC_CFG  = 1, /* KMC Memory Configuration */
    LOCK4KMC_RAND = 2, /* Random number generation lock */
    LOCK4KEYSTORE_READ = 3, /* Read keystore data and corresponding files in the memory */
    WSEC_LOCK_NUM = 4,
} WsecThreadLockFor;

typedef enum {
    PROCLOCK4KEYSTORE  = 0,
    WSEC_PROC_LOCK_NUM = 1
} WsecProclockFor;

/* 4. Other data types and structures */
typedef struct TagWsecLockRegStatus {
    WsecUint32 state;
} WsecLockRegStatus;

/* mip */
#define KMC_MASKCODE_LENGTH 128
/*
 * 2: Only the first part is used, and the last part is used for verification.
 */
#define KMC_MASKCODE_KEY_LENGTH (2 * KMC_MASKCODE_LENGTH)

typedef struct TagWsecFuncRegStatus {
    WsecUint32 state;
} WsecFuncRegStatus;

/* ksm */
#define WSEC_HASH_LEN_MAX   64 /* Maximum HMAC result length */
#define KMC_KSF_NUM 2
#define KMC_MATERIAL_SIZE 32
#define KMC_MASKED_KEY_LEN 128

/* Root Key (RK) data structure */
#pragma pack(1)
    typedef struct TagKmcRkParameters {
    unsigned char rkMaterialA[KMC_MATERIAL_SIZE]; /* Root key material 1, which is fixed to 32 bytes. */
    unsigned char rkMaterialB[KMC_MATERIAL_SIZE]; /* Root key material 2. The value is fixed to 32 bytes. */
    unsigned char reserve[32]; /* 32 bytes are reserved. */
    unsigned char rmkSalt[32]; /* Derived RMK salt value, which is fixed at 32 bytes. */
} KmcRkParameters; /* Basic parameters for exporting the root key */
#pragma pack()

#pragma pack(1)
typedef struct TagKmcKsfRk {
    KmcRkAttributes rkAttributes;  /* RK basic attributes */
    KmcRkParameters rkParameters;  /* RootKey construction parameters */
    WsecUint32      mkNum;         /* Total number of master keys */
    WsecUint32      updateCounter;
    WsecUint32      mkRecordLen; /* Maximum length of each MK ciphertext. This parameter is extended in KSFV3. */
    /*
     * Number of MK updates in the shared domain,
     * which is used to determine the host identity (new master) during startup.
     */
    WsecUint32      sharedMkUpdateCounter;
    unsigned char   hashAlg;
    unsigned char   hmacAlg;
    unsigned char   kdfAlg;
    unsigned char   paddingMode;
    unsigned char   reserve[20];   /* Reserved 20 bytes */
    unsigned char   aboveHash[32]; /* SHA256 result, 32 bytes */
} KmcKsfRk; /* RootKey information in Keystore */
#pragma pack()

typedef struct TagKmcKsfHardRk {
    WsecBool hasHardRk;
    WsecBuff hrkInfo;    /* Hardware root key access information */
    WsecBuff srkInfo;    /* Masked key encryption result by the hardware root key */
} KmcKsfHardRk;

typedef union TagKmcMaskedKey {
    /*
     * Plaintext key at the software layer, encrypted and decrypted by the hardware key,
     * and masked plaintext stored in the memory
     */
    unsigned char maskedKey[KMC_MASKED_KEY_LEN];
    struct {
        /* 32-byte software layer root key. This parameter is valid when HASSOFTLEVELRK is set to TRUE. */
        unsigned char softLevelRk[32];
        unsigned char ksfHmacKey[32];  /* KSF integrity verification key (32 bytes) */
        unsigned char reserve[64];     /* Reserved 64 bytes are used to fill in random numbers. */
    } maskedKeyInfo;
} KmcMaskedKey;

typedef struct TagKmcHardRkMem {
    WsecHandle   hwRkHandle; /* Hardware root key handle */
    KmcKsfHardRk hardRk;
    WsecUint32   refCount;
    KmcMaskedKey key;
} KmcHardRkMem;

typedef struct TagKmcKsfMem {
    char         *fromFile;      /* Keystore file from which the data comes */
    /*
     * MK array. Its elements store addresses of the KmcMemMk type.
     * Domain IDs and key types can be sorted in ascending order.
     * The domain ID and key ID must be unique.
     */
    WsecArray     mkArray;
    WsecUint32    updateCounter; /* Number of keystore file update rounds. */
    /*
     * Number of times that the master key in the shared domain is updated, which is used to determine
     * the host identity (the new one is the master one) during startup.
     */
    WsecUint32    sharedMkUpdateCounter;

    /* The following fields are extended in V3: */
    unsigned char ksfHash[WSEC_HASH_LEN_MAX];
    KmcKsfRk      rk;         /* KmcKsfRk */
    KmcKsfHardRk  hardRk;     /* Read HRKINFO and SRKINFO from KSF and load them to the hardware key handle. */
} KmcKsfMem; /* KSF file in the memory */


typedef struct TagKmcCfg {
    /* RK configuration information (valid only when the RK is generated in the CBB) */
    KmcCfgRootKey       rkCfg;
    KmcCfgKeyManagement keyManagementCfg;  /* Key management parameters */
    KmcCfgDataProtect   dataProtectCfg[3]; /* Data protection configuration, including 3 types */
    WsecArray           domainCfgArray;    /* Domain array. The type of the element is KmcDomainCfg. */
} KmcCfg;

typedef struct TagKmcSys {
    char        *keystoreFile[KMC_KSF_NUM];             /* Keystore file name, which is backed up in two copies. */
    WsecUint32  role;                        /* Identity information */
    WsecUint32  state;                       /* KMC Status */
    /* The following fields are extended in V3: */
    WsecBool    isHardware;                  /* Hardware-Protected Root Key */
    WsecBool    hasSoftLevelRk;              /* Include Software-Layer Root Key */
    WsecBool    enableThirdBackup;           /* enable the third backup path, default false; */
    WsecBool    deleteKsfOnInitFailed;       /* delete ksf when init failed */
    char        *keystoreBackupFile;
} KmcSys;

/* All callback functions */
typedef struct TagWsecCallbacksInternal {
    WsecMemCallbacks            memCallbacks;
    WsecFileCallbacks           fileCallbacks;
    WsecLockCallbacks           lockCallbacks;
    WsecProcLockCallbacks       procLockCallbacks;
    WsecBasicRelyCallbacks      basicRelyCallbacks;
    WsecRngCallbacks            rngCallbacks;
    WsecTimeCallbacks           timeCallbacks;
    WsecHardwareCallbacks       hardwareCallbacks;

    WsecProcLockCallbacksMulti  procLockCallbacksMulti;
    WsecBasicRelyCallbacksMulti basicRelyCallbacksMulti;
    WsecHardwareCallbacksMulti  hardwareCallbacksMulti;
} WsecCallbacksInternal;

typedef struct TagKmcCbbCtx {
    WsecCallbacksInternal regCallbacks; // 回调函数
    /* g_cbbFuncRegState */
    WsecFuncRegStatus cbbFuncRegState;  // 回调函数注册标志
    /* g_keyStore */
    KmcKsfMem *keystore;                // ksf文件信息：mk信息+硬件根密钥信息
    /* g_kmcCfg */
    KmcCfg *kmcCfg;                     // kmc配置信息 domain信息 rootkey配置信息（有效时间等）
    /* g_kmcDefaultRkCfg */
    KmcCfgRootKey kmcDefaultRkCfg;      // rootkey的默认配制项
    /* g_kmcAlgCfg */
    KmcAlgCnfParam algCnfParam;         // 内部算法配置信息
    /* g_maskCode */
    unsigned char maskCode[KMC_MASKCODE_KEY_LENGTH];
    /* g_xorCheck */
    unsigned char xorCheck[KMC_MASKCODE_LENGTH];
    /* g_keyName */
    char keyName[48]; /* The value is a string of up to 48 digits. */   // keyring
    /* g_hasName */
    WsecBool hasName;
    /* g_hardRkMem */
    WsecArray hardRkMem;                // 存储所有加载或创建的硬件根密钥内存数据
    /* g_kmcSys */
    KmcSys kmcSys;                      // kmc系统信息 对应ksf文件+角色+状态+是否硬件等
    /* g_maxMkCount */
    int maxMkCount;                     // mk最大个数
    /* g_lockEx */
    WsecHandle lockEx[WSEC_LOCK_NUM];
    /* g_kmcProcLock */
    WsecHandle kmcProcLock[WSEC_PROC_LOCK_NUM];
    /* g_cbbSysEx */
    WsecLockRegStatus cbbSysEx;   /* Check whether the lock function is registered. */

    void *userData;                     // 对接KMS需要透传的用户数据
    WsecUint32 multiFlag;               // 是否为多实例
} KmcCbbCtx;

KmcCbbCtx *GetKmcCtx(void);

unsigned long KmcCheckKmcCtx(WsecHandle kmcCtx);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* KMC_SRC_COMMON_WSECV2_CTX_H */

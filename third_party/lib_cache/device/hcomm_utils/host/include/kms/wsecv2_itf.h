/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2014-2020. All rights reserved.
 * Description: external interface
 * Author: Luan Shipeng l00171031
 * Create: 2014-06-16
 * History: 2018-10-08 Zhang Jie (employee ID: 00316590) Rectification by UK
 */

#ifndef KMC_INCLUDE_WSECV2_ITF_H
#define KMC_INCLUDE_WSECV2_ITF_H

#include "wsecv2_type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WSEC_FILEPATH_MAX_LEN 260 /* Maximum length of a file path. */
#define KMC_VERSION "KMC 21.1.1"

#pragma pack(1)
typedef struct KmcInitHardWareParamTag {
    WsecBool hasSoftLevelRk;
    const WsecVoid *hardwareParam;
    WsecUint32 hardwareParamLen;
    unsigned char reserve[12];
} KmcInitHardWareParam;
#pragma pack()

#pragma pack(1)
typedef struct KmcRecoverParamTag {
    /* enableThirdBackup is set to WSEC_TRUE KmcKsfName.keyStoreBackupFile
     * must provided */
    WsecBool enableThirdBackup;
    /* deleted KSF when init failed : if custom backup ksf exists
     * do not set this flag KMC will keep ksf for manual recover */
    WsecBool deleteKsfOnInitFailed;
    unsigned char reserve[8];
} KmcRecoverParam;
#pragma pack()

#pragma pack(1)
typedef struct KmcInitParamTag {
    WsecUint32 roleType;
    const KmcKsfName *filePathName;
    WsecBool enableHw;
    KmcInitHardWareParam *hwParam;
    KmcRecoverParam *recoverParam;
    KmcAlgCnfParam *algCnfParam;
    unsigned char reserve[20];
    WsecUint32 extLen;
    unsigned char extParam[0];
} KmcInitParam;
#pragma pack()

/*
 * API Function Prototype Description
 * When the system is started or shut down, the application needs to call the following functions.
 */
/* Callback function registration */
unsigned long WsecRegFuncEx(const WsecCallbacks *allCallbacks);

/*
 * When no security hardware is available,
 * the initialization function is used to specify the master or agent.
 * The paths of the active and standby files of the keystore must be specified.
 * The variant parameter is reserved.
  */
unsigned long WsecInitializeEx(WsecUint32 roleType,
    const KmcKsfName *filePathName,
    WsecBool useImportKey,
    WsecVoid *exParam);

/*
 * When security hardware is used, the initialization function is used to specify the master or agent.
 * The paths of the active and standby files of the keystore must be specified. The variant parameter is reserved.
 * hasSoftLevelRk: indicates whether to use the software-layer root key for acceleration.
 * The software-layer root key is encrypted by the hardware root key and stored in the keystore file in ciphertext.
 * Once this parameter is specified, it cannot be changed.
 * This parameter has low security risks due to performance considerations.
 */
unsigned long WsecInitializeHw(WsecUint32 roleType,
    const KmcKsfName *filePathName,
    WsecBool hasSoftLevelRk,
    const WsecVoid *hardwareParam, WsecUint32 hardwareParamLen,
    WsecVoid *exParam);

/*
 * This is a new version API for kmc initial, which you can used for both hardware procted root key and software
 * protected. param is compatible with WsecInitializeEx and WsecInitializeHw,
 * roleType: type of kmc role, you can set KMC_ROLE_MASTER or KMC_ROLE_AGENT
 * filePathName: The keystore file name;
 * enableHw: Whther enable hareware to protect root key. if set to WSEC_TRUE means use hardware protect root key, also
 * use software protect root key.
 * hwParam:  It's only works when enableHw equal to WSEC_TRUE; Its member hasSoftLevelRk determine whther use one
 * soft level root key.
 * recoverParam: used set third backup and recover ksf. optional Param, set NULL if not used, Ref. WsecExtendInitParam
 * algCnfParam: used to set crypto algorithm. optional parameter, set NULL if not used.
 * algCnfParam->symmAlg.algId values in (WSEC_ALGID_AES128_CBC, WSEC_ALGID_AES256_CBC, WSEC_ALGID_AES128_GCM
 * WSEC_ALGID_AES256_GCM, WSEC_ALGID_SM4_CBC, WSEC_ALGID_SM4_CTR)
 * algCnfParam->hashAlg values in (WSEC_ALGID_SHA256, WSEC_ALGID_SM3)
 * algCnfParam->hmacAlg values in (WSEC_ALGID_HMAC_SHA256, WSEC_ALGID_HMAC_SM3)
 * algCnfParam->kdfAlg values in (WSEC_ALGID_PBKDF2_HMAC_SHA256, WSEC_ALGID_PBKDF2_HMAC_SM3)
 * we strongly recommend you memset all KmcInitParam's member to 0 before assign value.
 */
unsigned long WsecInitializeKmc(const KmcInitParam *initParam);

/*
 * Function for reloading the keystore file.
 * This function is used to reload the keystore file from the keystore file in the process.
 * Note: After this function is called, the configuration is restored
 * to the default configuration. If the default configuration is not used,
 * the app needs to reconfigure the configuration.
 */
unsigned long WsecResetEx(void);

unsigned long WsecResetHw(const WsecVoid *hardwareParam, WsecUint32 hardwareParamLen);

/* Specified master or agent */
unsigned long WsecSetRole(WsecUint32 roleType);

/* Deinitializes a function. */
unsigned long WsecFinalizeEx(void);

/* Obtains the current version number. */
const char *WsecGetVersion(void);
#ifdef WSEC_DEBUG
/* Size of the callback Wsec data structure */
WsecVoid WsecShowStructSize(CallbackShowStructSize showStructures);
#endif

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* KMC_INCLUDE_WSECV2_ITF_H */

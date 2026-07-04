/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * Description: network common function realization
 * Create: 2025-9-24
 */
#ifndef TLS_COMMON_H
#define TLS_COMMON_H

#define TLS_ATTRI_VISI_DEF __attribute__ ((visibility ("default")))

int set_tls_enable_to_hcdev(unsigned int chip_id, unsigned int tls_enable);
void tls_get_enable_info(unsigned int save_mode, unsigned int chip_id, unsigned char *buf, unsigned int buf_size);
int tls_get_user_config(unsigned int save_mode, unsigned int chip_id, const char *name, unsigned char *buf,
    unsigned int *buf_size);
int tls_set_user_config(unsigned int save_mode, unsigned int chip_id, const char *name, unsigned char *buf,
    unsigned int buf_size);
int tls_clear_user_config(unsigned int save_mode, unsigned int chip_id, const char *name);
int tls_get_lock_file_path(char *lock_file_path, unsigned int file_path_len);

#endif
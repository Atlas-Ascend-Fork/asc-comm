/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * Description: rdma service tls interface
 * Create: 2025-9-24
 */

#ifndef RS_SSL_H
#define RS_SSL_H
#include "rs_inner.h"
#include "tls_common.h"

TLS_ATTRI_VISI_DEF void rs_ssl_err_string(int fd, int err);

TLS_ATTRI_VISI_DEF int rs_tls_peer_cert_verify(SSL *ssl, struct rs_cb *rscb);

TLS_ATTRI_VISI_DEF int rs_ssl_init(struct rs_cb *rscb);

TLS_ATTRI_VISI_DEF void rs_ssl_deinit(struct rs_cb *rscb);
#endif // RS_SSL_H

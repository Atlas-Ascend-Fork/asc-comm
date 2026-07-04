/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * Description: network ssl adaptation function declaration
 * Create: 2025-10-23
 */
#ifndef SSL_ADP_H
#define SSL_ADP_H

#if defined (CA_CONFIG_LLT) || defined (CONFIG_LLT)
#else
#include <openssl/err.h>
#endif
#include "tls.h"
#include "tls_common.h"

TLS_ATTRI_VISI_DEF long ssl_adp_set_mode(SSL *s, long op);
TLS_ATTRI_VISI_DEF SSL *ssl_adp_new(SSL_CTX *ctx);
TLS_ATTRI_VISI_DEF void ssl_adp_free(SSL *s);
TLS_ATTRI_VISI_DEF int ssl_adp_read(SSL *s, void *buf, int num);
TLS_ATTRI_VISI_DEF int ssl_adp_write(SSL *s, const void *buf, int num);
TLS_ATTRI_VISI_DEF int ssl_adp_set_fd(SSL *s, int fd);
TLS_ATTRI_VISI_DEF void ssl_adp_ctx_free(SSL_CTX *a);
TLS_ATTRI_VISI_DEF int ssl_adp_shutdown(SSL *s);
TLS_ATTRI_VISI_DEF int ssl_adp_get_error(const SSL *s, int i);
TLS_ATTRI_VISI_DEF int ssl_adp_do_handshake(SSL *s);
TLS_ATTRI_VISI_DEF void ssl_adp_set_accept_state(SSL *s);
TLS_ATTRI_VISI_DEF void ssl_adp_set_connect_state(SSL *s);
TLS_ATTRI_VISI_DEF void ssl_adp_clear_error(void);

#endif
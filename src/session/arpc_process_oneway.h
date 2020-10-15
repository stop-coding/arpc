/*
 * Copyright(C) 2020 Ruijie Network. All rights reserved.
 */

/*!
* \file xxx.x
* \brief xxx
* 
* 包含..
*
* \copyright 2020 Ruijie Network. All rights reserved.
* \author hongchunhua@ruijie.com.cn
* \version v1.0.0
* \date 2020.08.05
* \note none 
*/

#ifndef _ARPC_PROCESS_ONEWAY_H
#define _ARPC_PROCESS_ONEWAY_H

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/time.h>

#include "base_log.h"
#include "arpc_com.h"
#include "arpc_session.h"

#ifdef __cplusplus
extern "C" {
#endif

int process_oneway_header(struct xio_msg *msg, struct oneway_ops *ops, uint64_t iov_max_len, void *usr_ctx);
int process_oneway_data(struct xio_msg *req, struct oneway_ops *ops, int last_in_rxq, void *usr_ctx);

#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */

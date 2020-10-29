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

#ifndef _ARPC_RESPONSE_H
#define _ARPC_RESPONSE_H

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

struct arpc_rsp_handle {
	struct xio_msg				x_req_msg;
    struct xio_msg				*x_rsp_msg;
	struct arpc_vmsg 			*rsp_usr_iov;
	struct arpc_msg_attr		attr;
	int (*release_rsp_cb)(struct arpc_vmsg *rsp_iov, void* rsp_usr_ctx);
	void *rsp_usr_ctx;
};

int arpc_init_response(struct arpc_common_msg *rsp_fd);
int arpc_send_response_complete(struct arpc_common_msg *rsp_fd);

#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */

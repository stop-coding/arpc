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

struct arpc_rsp_handle {
	struct xio_msg				x_req_msg;
    struct xio_msg				*x_rsp_msg;
	struct arpc_vmsg 		*rsp_usr_iov;
	int (*release_rsp_cb)(struct arpc_vmsg *rsp_iov, void* rsp_usr_ctx);
	void *rsp_usr_ctx;
};

int arpc_do_response_complete(struct arpc_common_msg *rsp_fd, struct arpc_connection *con);
int arpc_send_response(struct arpc_common_msg *rsp_fd);

struct arpc_rsp_handle *arpc_create_response();
int arpc_destroy_response(struct arpc_rsp_handle* rsp);

#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */

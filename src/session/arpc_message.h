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

#ifndef _ARPC_MESSAGE_H
#define _ARPC_MESSAGE_H

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

struct arpc_msg_ex {
    struct xio_msg				x_req_msg;
    struct xio_msg				*x_rsp_msg;
    struct arpc_msg             *msg;
	void* (*alloc_cb)(uint32_t size, void* usr_context);	/*! @brief 用于接收rsp消息时分配内存，可选 */
	int   (*free_cb)(void* buf_ptr, void* usr_context);		/*! @brief 内存释放 可选*/
	void 		*usr_context;								/*! @brief 用户上下文 */
    uint32_t                    flags;
    uint64_t                    iov_max_len;
};

int conver_msg_arpc_to_xio(const struct arpc_vmsg *usr_msg, struct xio_vmsg *xio_msg);
void release_arpc2xio_msg(struct xio_vmsg *xio_msg);

int conver_msg_xio_to_arpc(const struct xio_vmsg *xio_msg, struct arpc_vmsg *msg);
void release_xio2arpc_msg(struct arpc_vmsg *msg);

int alloc_xio_msg_usr_buf(struct xio_msg *msg, struct arpc_msg *arpc_msg);
int free_receive_msg_buf(struct arpc_msg *msg);

#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */

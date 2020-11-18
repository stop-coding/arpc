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
    struct xio_msg				*x_rsp_msg;
    struct arpc_msg             *msg;
    struct arpc_msg_attr        attr;
	void* (*alloc_cb)(uint32_t size, void* usr_context);	/*! @brief 用于接收rsp消息时分配内存，可选 */
	int   (*free_cb)(void* buf_ptr, void* usr_context);		/*! @brief 内存释放 可选*/
	void 		                *usr_context;				/*! @brief 用户上下文 */
    uint32_t                    flags;
    uint64_t                    iov_max_len;
};

#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */

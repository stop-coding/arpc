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

#ifndef _ARPC_REQUEST_H
#define _ARPC_REQUEST_H

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

#define REQUEST_CTX(_usr_msg, _usr_msg_ex, _com_msg)\
struct arpc_msg_ex *_usr_msg_ex;\
struct arpc_msg *_usr_msg;\
_usr_msg = ((struct arpc_request_handle *)_com_msg->ex_data)->msg;\
_usr_msg_ex = ((struct arpc_request_handle *)_com_msg->ex_data)->msg_ex;

#define REQUEST_USR_EX_CTX(_usr_msg_ex, _com_msg)\
struct arpc_msg_ex *_usr_msg_ex;\
_usr_msg_ex = ((struct arpc_request_handle *)_com_msg->ex_data)->msg_ex;

int arpc_request_rsp_complete(struct arpc_common_msg *req_msg);
int arpc_oneway_send_complete(struct arpc_common_msg *ow_msg);

#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */

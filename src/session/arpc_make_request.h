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

#ifndef _ARPC_MAKE_REQUEST_H
#define _ARPC_MAKE_REQUEST_H

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/time.h>

#include "base_log.h"
#include "arpc_com.h"
#include "arpc_session.h"

// make request
int _arpc_rev_request_head(struct xio_msg *in_rsp);
int _arpc_rev_request_rsp(struct xio_msg *in_rsp);
int _release_rsp_msg(struct arpc_msg *msg);
int _request_send_complete(struct arpc_msg *msg);
int _oneway_send_complete(struct arpc_conn_ow_msg *oneway_msg, void *con_usr_ctx);

#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */

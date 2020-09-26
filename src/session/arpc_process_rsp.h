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

#ifndef _ARPC_PROCESS_RSP_H
#define _ARPC_PROCESS_RSP_H

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/time.h>

#include "base_log.h"
#include "arpc_com.h"
#include "arpc_session.h"

int process_rsp_header(struct xio_msg *rsp, struct arpc_connection *con);
int process_rsp_data(struct xio_msg *rsp, int last_in_rxq, struct arpc_connection *con);

#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */

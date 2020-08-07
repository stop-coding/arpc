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

#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

#include "arpc_com.h"


int _process_rsp_header(struct xio_msg *rsp, void *usr_ctx)
{
	return _arpc_rev_request_head(rsp);
}

int _process_rsp_data(struct xio_msg *rsp, int last_in_rxq)
{
	return _arpc_rev_request_rsp(rsp);
}


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

#include "arpc_process_rsp.h"
#include "arpc_request.h"
#include "arpc_message.h"

int process_rsp_header(struct xio_msg *rsp, struct arpc_connection *con)
{
	struct proc_header_func head_ops;
	int ret;
	struct arpc_common_msg *req_msg = (struct arpc_common_msg *)rsp->user_context;
	REQUEST_USR_EX_CTX(ex_msg, req_msg);

	LOG_THEN_RETURN_VAL_IF_TRUE((!req_msg), ARPC_ERROR, "req_msg null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!con), ARPC_ERROR, "con null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((req_msg->magic != ARPC_COM_MSG_MAGIC), ARPC_ERROR, "magic[%x] not match.", req_msg->magic);
	ex_msg->x_rsp_msg = rsp;
	head_ops.alloc_cb = ex_msg->alloc_cb;
	head_ops.free_cb = ex_msg->free_cb;
	head_ops.proc_head_cb = NULL;
	ret = create_xio_msg_usr_buf(ex_msg->x_rsp_msg, &head_ops, ex_msg->iov_max_len, ex_msg);//申请资源
	if (!ret){
		SET_FLAG(ex_msg->flags, XIO_RSP_IOV_ALLOC_BUF);
	}
	return ret;
}

int process_rsp_data(struct xio_msg *rsp, int last_in_rxq, struct arpc_connection *con)
{
	int ret;
	struct arpc_msg_attr attr = {0};
	struct arpc_common_msg *req_msg = (struct arpc_common_msg *)rsp->user_context;
	REQUEST_USR_EX_CTX(ex_msg, req_msg);

	LOG_THEN_RETURN_VAL_IF_TRUE((!req_msg), ARPC_ERROR, "req_msg null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!con), ARPC_ERROR, "con null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((req_msg->magic != ARPC_COM_MSG_MAGIC), ARPC_ERROR, "magic[%x] not match.", req_msg->magic);

	//通知数据发送完成
	ret = arpc_connection_send_comp_notify(con, req_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_connection_send_comp_notify fail.");

	ret = move_msg_xio2arpc(&rsp->in, &ex_msg->msg->receive, &attr);
	LOG_ERROR_IF_VAL_TRUE(ret, "conver_msg_xio_to_arpc fail");

	ret = destroy_xio_msg_usr_buf(rsp, ex_msg->free_cb, ex_msg->usr_context);
	LOG_ERROR_IF_VAL_TRUE((ret), "destroy_xio_msg_usr_buf fail.");

	ret = xio_release_response(rsp);
	LOG_ERROR_IF_VAL_TRUE(ret, "xio_release_response fail");

	if (attr.rsp_crc && ex_msg->attr.req_crc && attr.rsp_crc != ex_msg->attr.req_crc) {
		SET_FLAG(ex_msg->flags, XIO_MSG_ERROR_DISCARD_DATA);
		ARPC_ASSERT(attr.rsp_crc == ex_msg->attr.req_crc, "request crc:0x%lx, but rsp it:0x%lx.", 
						ex_msg->attr.req_crc,
						attr.rsp_crc);
		ARPC_LOG_ERROR("crc fail fail, request crc:0x%lx, but rsp it:0x%lx.",
						ex_msg->attr.req_crc,
						attr.rsp_crc);
		free_msg_xio2arpc(&ex_msg->msg->receive, ex_msg->free_cb, ex_msg->usr_context);
	}

	ret =  arpc_request_rsp_complete(req_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_request_rsp_complete fail");
	
	return 0;
}


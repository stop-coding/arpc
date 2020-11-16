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

#include "arpc_com.h"
#include "arpc_session.h"
#include "arpc_request.h"
#include "arpc_response.h"
#include "arpc_process_rsp.h"
#include "arpc_process_request.h"
#include "arpc_process_oneway.h"

int msg_head_process(struct xio_session *session, struct xio_msg *msg, void *conn_context)
{
	int ret = 0;
	ARPC_CONN_OPS_CTX(conn, conn_ops, conn_context);
	ARPC_LOG_DEBUG("header message type:%d", msg->type);
	switch(msg->type) {
		case XIO_MSG_TYPE_REQ:
			ret = process_request_header(conn, msg, &conn_ops->req_ops, arpc_get_max_iov_len(conn), arpc_get_ops_ctx(conn));
			break;
		case XIO_MSG_TYPE_RSP:
			ret = process_rsp_header(msg, conn);
			break;
		case XIO_MSG_TYPE_ONE_WAY:
			ret = process_oneway_header(msg, &conn_ops->oneway_ops, arpc_get_max_iov_len(conn), arpc_get_ops_ctx(conn));
			break;  
		default:
			break;
	}

	return ret;
}

int msg_data_process(struct xio_session *session,struct xio_msg *msg, int last_in_rxq, void *conn_context)
{
	int ret = 0;
	ARPC_CONN_OPS_CTX(conn, conn_ops, conn_context);

	ARPC_LOG_DEBUG("msg_data_dispatch, msg type:%d", msg->type);

	ret = check_xio_msg_valid(conn, &msg->in);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "check_xio_msg_valid fail.");

	switch(msg->type) {
		case XIO_MSG_TYPE_REQ:
			ret = process_request_data(conn, msg, &conn_ops->req_ops, last_in_rxq, arpc_get_ops_ctx(conn));
			break;
		case XIO_MSG_TYPE_RSP:
			ret = process_rsp_data(msg, last_in_rxq, conn);
			break;
		case XIO_MSG_TYPE_ONE_WAY:
			ret = set_connection_io_type(conn, ARPC_IO_TYPE_IN);
			LOG_ERROR_IF_VAL_TRUE(ret, "set_connection_rx_mode fail.");
			ret = process_oneway_data(msg, &conn_ops->oneway_ops, last_in_rxq, arpc_get_ops_ctx(conn));
			break;
		default:
			break;
	}

	return ret;
}

int msg_delivered(struct xio_session *session,struct xio_msg *msg, int last_in_rxq, void *conn_context)
{
	return 0;
}

int response_send_complete(struct xio_session *session,struct xio_msg *rsp, void *conn_context)
{
	int ret;
	struct arpc_common_msg *com_msg;
	ARPC_CONN_CTX(conn, conn_context);

	com_msg = (struct arpc_common_msg *)(rsp->user_context);

	ret = arpc_connection_send_comp_notify(conn, com_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_connection_send_comp_notify fail.");

	ret = arpc_send_response_complete(com_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_send_response clean up fail.");

	return ret;
}

static int on_msg_delivered(struct xio_session *session,
				struct xio_msg *msg,
				int last_in_rxq,
				void *conn_context)
{
	int ret;
	struct arpc_common_msg *com_msg;
	ARPC_CONN_CTX(conn, conn_context);

	com_msg = (struct arpc_common_msg *)(msg->user_context);

	ret = arpc_connection_send_comp_notify(conn, com_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_connection_send_comp_notify fail.");

	if(msg->type == XIO_MSG_TYPE_ONE_WAY){
		ret = arpc_oneway_send_complete(com_msg);
		LOG_ERROR_IF_VAL_TRUE(ret, "arpc_send_response clean up fail.");
	}
	
	return ret;
}

int oneway_send_complete(struct xio_session *session, struct xio_msg *msg, void *conn_context)
{
	int ret;
	struct arpc_common_msg *com_msg;
	ARPC_CONN_CTX(conn, conn_context);

	com_msg = (struct arpc_common_msg *)(msg->user_context);

	ret = arpc_connection_send_comp_notify(conn, com_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_connection_send_comp_notify fail.");

	ret = arpc_oneway_send_complete(com_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_send_response clean up fail.");

	return ret;
}

int message_error(struct xio_session *session, enum xio_status error, enum xio_msg_direction dir, struct xio_msg  *rsp, void *conn_context)
{
	int ret;
	struct arpc_common_msg *com_msg;
	ARPC_CONN_CTX(conn, conn_context);

	com_msg = (struct arpc_common_msg *)(rsp->user_context);
	ARPC_LOG_ERROR("msg_error, dir:%d, err:%d, error:%s. ", dir, error, xio_strerror(error));
	switch(dir) {
		case XIO_MSG_DIRECTION_OUT:
			ret = arpc_connection_send_comp_notify(conn, com_msg);
			LOG_ERROR_IF_VAL_TRUE(ret, "arpc_connection_send_comp_notify fail.");
			if(rsp->type == XIO_MSG_TYPE_ONE_WAY){
				arpc_oneway_send_complete(com_msg);
			}else if(rsp->type == XIO_MSG_TYPE_RSP) {
				arpc_send_response_complete(com_msg);
			}
			break;
		case XIO_MSG_DIRECTION_IN:
			if(rsp->type == XIO_MSG_TYPE_RSP){
				xio_release_response(rsp);
			}
			break; 
		default:
			break;
	}
	return 0;
}
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
	ARPC_LOG_TRACE("rx header, message type:%d, head len:%u, data len:%lu", msg->type, (uint32_t)msg->in.header.iov_len, msg->in.total_data_len);
	gettimeofday(&conn->rx_now, NULL);	// 线程安全
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
	ARPC_LOG_TRACE("rx data, msg type:%d, head len:%u, data len:%lu", msg->type, (uint32_t)msg->in.header.iov_len, msg->in.total_data_len);
	ret = keep_conn_heartbeat(conn);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "keep_conn_heartbeat fail.");
	ret = check_xio_msg_valid(conn, &msg->in);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "check_xio_msg_valid fail.");

	switch(msg->type) {
		case XIO_MSG_TYPE_REQ:
			conn->rx_req_count++;
			ret = process_request_data(conn, msg, &conn_ops->req_ops, last_in_rxq, arpc_get_ops_ctx(conn));
			statistics_per_time(&conn->rx_now, &conn->rx_req, 1);
			break;
		case XIO_MSG_TYPE_RSP:
			conn->rx_rsp_count++;
			ret = process_rsp_data(msg, last_in_rxq, conn);
			statistics_per_time(&conn->rx_now, &conn->rx_rsp, 1);
			break;
		case XIO_MSG_TYPE_ONE_WAY:
			conn->rx_ow_count++;
			ret = set_connection_io_type(conn, ARPC_IO_TYPE_IN);
			LOG_ERROR_IF_VAL_TRUE(ret, "set_connection_rx_mode fail.");
			ret = process_oneway_data(msg, &conn_ops->oneway_ops, last_in_rxq, arpc_get_ops_ctx(conn));
			statistics_per_time(&conn->rx_now, &conn->rx_ow, 1);
			break;
		default:
			break;
	}
	if (conn->rx_interval.tv_sec + STATISTICS_PRINT_INTERVAL_S <= conn->rx_now.tv_sec) {
		conn->rx_interval = conn->rx_now;
		ARPC_LOG_NOTICE("### receive status ###:\n  # session[%p],type[%d],conid[%u],\n  # rx req cnt:%lu|ave:%lu.%06ld s,\n  # rx rsp cnt:%lu|ave:%lu.%06ld s,\n  # rx ow cnt:%lu|ave:%lu.%06ld s.\n######\n", 
						arpc_get_conn_session(conn),
						arpc_get_conn_type(conn),
						conn->id,
						conn->rx_req_count, conn->rx_req.ave.tv_sec, conn->rx_req.ave.tv_usec,
						conn->rx_rsp_count, conn->rx_rsp.ave.tv_sec, conn->rx_rsp.ave.tv_usec,
						conn->rx_ow_count, conn->rx_ow.ave.tv_sec, conn->rx_ow.ave.tv_usec);
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

	ARPC_LOG_TRACE("send rsp msg complete.");

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

	ARPC_LOG_TRACE("send rsp msg delivered.");
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

	ARPC_LOG_TRACE("send oneway msg complete.");
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
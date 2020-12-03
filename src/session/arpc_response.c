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

#include "queue.h"
#include "arpc_com.h"
#include "arpc_request.h"
#include "arpc_response.h"

static const int32_t SEND_RSP_END_MAX_TIME_MS = 2*1000;
/*! 
 * @brief 回复消息个请求方
 * 
 * 回复一个消息个请求方（只能是请求的消息才能回复）
 * 
 * @param[in] rsp_ctx ,a rsp msg handle, 由接收到消息回复时获取
 * @param[in] rsp_iov ,回复的消息
 * @param[in] release_rsp_cb ,回复结束后回调函数，用于释放调用者的回复消息资源
 * @param[in] rsp_cb_ctx , 回调函数调用者入参
 * @return int .0,表示发送成功，小于0则失败
 */
int arpc_do_response(arpc_rsp_handle_t *rsp_fd, struct arpc_vmsg *rsp_iov, rsp_cb_t release_rsp_cb, void* rsp_cb_ctx)
{
	struct arpc_common_msg *rsp_msg;
	struct arpc_rsp_handle *rsp_fd_ex;
	int ret;
	struct timeval now;
	LOG_THEN_RETURN_VAL_IF_TRUE((!rsp_fd), ARPC_ERROR, "rsp_fd is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!rsp_iov), ARPC_ERROR, "rsp_iov is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!release_rsp_cb), ARPC_ERROR, "rsp_iov is null.");

	gettimeofday(&now, NULL);	// 线程安全
	rsp_msg = (struct arpc_common_msg *)(*rsp_fd);
	
	ret = arpc_cond_lock(&rsp_msg->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock msg fail, maybe free...");
	rsp_msg->now = now;
	rsp_msg->attr.tx_sec= now.tv_sec;
	rsp_msg->attr.tx_usec= now.tv_usec;
	rsp_msg->attr.conn_id = rsp_msg->conn->id;
	rsp_fd_ex = (struct arpc_rsp_handle*)rsp_msg->ex_data;
	rsp_fd_ex->release_rsp_cb = release_rsp_cb;
	rsp_fd_ex->rsp_usr_iov = rsp_iov;
	rsp_fd_ex->rsp_usr_ctx = rsp_cb_ctx;
	ret = arpc_init_response(rsp_msg);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, unlock, "arpc_init_response fail.");

	ret = arpc_connection_async_send(rsp_msg->conn, rsp_msg);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, unlock, "arpc_session_send_response fail.");

	ret = arpc_cond_wait_timeout(&rsp_msg->cond, SEND_RSP_END_MAX_TIME_MS); // 默认等待
	arpc_cond_unlock(&rsp_msg->cond);
	*rsp_fd = NULL;
	return ret;
unlock:
	arpc_cond_unlock(&rsp_msg->cond);
	return -1;
}

int arpc_init_response(struct arpc_common_msg *rsp_msg)
{
	struct xio_msg  	*xio_rsp_msg;
	uint32_t			i;
	struct arpc_rsp_handle *rsp_fd_ex;
	struct  arpc_vmsg  *rsp_iov;
	int ret;

	LOG_THEN_RETURN_VAL_IF_TRUE(!rsp_msg, -1, "release_rsp_cb is null ,can't send user rsp data.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!rsp_msg->conn, -1, "conn is null ,can't send user rsp data.");

	rsp_fd_ex = (struct arpc_rsp_handle*)rsp_msg->ex_data;
	xio_rsp_msg = &rsp_msg->xio_msg;
	rsp_iov = rsp_fd_ex->rsp_usr_iov;
	rsp_msg->tx_msg = xio_rsp_msg;
	
	if(rsp_iov && rsp_iov->head && rsp_iov->head_len){
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!rsp_fd_ex->release_rsp_cb, rsp_default, "release_rsp_cb is null ,can't send user rsp data.");
		ret = convert_msg_arpc2xio(rsp_iov, &xio_rsp_msg->out, &rsp_msg->attr);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, rsp_default, "convert_msg_arpc2xio fail.");
		SET_FLAG(xio_rsp_msg->usr_flags, XIO_MSG_FLAG_ALLOC_IOV_MEM);
		goto rsp;
	}else{
		ARPC_LOG_ERROR("response is empty, no head and data.");
	}
rsp_default:
	xio_rsp_msg->out.header.iov_base = NULL;
	xio_rsp_msg->out.header.iov_len  = 0;
	xio_rsp_msg->out.sgl_type = XIO_SGL_TYPE_IOV;
	vmsg_sglist_set_nents(&xio_rsp_msg->out, 0);
	CLR_FLAG(xio_rsp_msg->usr_flags, XIO_MSG_FLAG_ALLOC_IOV_MEM);
rsp:	
	xio_rsp_msg->request = rsp_fd_ex->x_rsp_msg;
	xio_rsp_msg->user_context = (void*)rsp_msg;
	return 0;
}

// 注意必须在同步回调线程里执行
int arpc_send_response_complete(struct arpc_common_msg *rsp_msg)
{
	int ret;
	struct arpc_rsp_handle *rsp_fd_ex;
	struct xio_msg  	*xio_rsp_msg;
	LOG_THEN_RETURN_VAL_IF_TRUE(!rsp_msg, ARPC_ERROR, "rsp_msg IS NULL");

	ret = arpc_cond_lock(&rsp_msg->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock fail.");

	rsp_fd_ex = (struct arpc_rsp_handle*)rsp_msg->ex_data;
	if (rsp_fd_ex->release_rsp_cb && rsp_fd_ex->rsp_usr_iov) {
		rsp_fd_ex->release_rsp_cb(rsp_fd_ex->rsp_usr_iov, rsp_fd_ex->rsp_usr_ctx);
	}
	xio_rsp_msg = &rsp_msg->xio_msg;
	if(IS_SET(xio_rsp_msg->usr_flags, XIO_MSG_FLAG_ALLOC_IOV_MEM) && xio_rsp_msg->out.pdata_iov.sglist){
		free_msg_arpc2xio(&xio_rsp_msg->out);
	}
	ret = arpc_cond_unlock(&rsp_msg->cond);
	put_common_msg(rsp_msg);

	return 0;
}
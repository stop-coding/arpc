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
	struct arpc_common_msg *arpc_rsp_fd;
	struct arpc_rsp_handle *rsp_fd_ex;
	int ret;
	LOG_THEN_RETURN_VAL_IF_TRUE((!rsp_fd), ARPC_ERROR, "rsp_fd is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!rsp_iov), ARPC_ERROR, "rsp_iov is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!release_rsp_cb), ARPC_ERROR, "rsp_iov is null.");

	arpc_rsp_fd = (struct arpc_common_msg *)(*rsp_fd);
	
	ret = arpc_cond_lock(&arpc_rsp_fd->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock msg fail, maybe free...");

	rsp_fd_ex = (struct arpc_rsp_handle*)arpc_rsp_fd->ex_data;
	rsp_fd_ex->release_rsp_cb = release_rsp_cb;
	rsp_fd_ex->rsp_usr_iov = rsp_iov;
	rsp_fd_ex->rsp_usr_ctx = rsp_cb_ctx;
	ret = arpc_init_response(arpc_rsp_fd);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, unlock, "arpc_init_response fail.");
	arpc_cond_unlock(&arpc_rsp_fd->cond);

	ret = arpc_session_async_send(arpc_rsp_fd->conn, arpc_rsp_fd);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_session_send_response fail.");

	*rsp_fd = NULL;
	return 0;
unlock:
	return -1;
}

int arpc_init_response(struct arpc_common_msg *rsp_fd)
{
	struct xio_msg  	*rsp_msg;
	uint32_t			i;
	struct arpc_rsp_handle *rsp_fd_ex;
	struct  arpc_vmsg  *rsp_iov;

	LOG_THEN_RETURN_VAL_IF_TRUE(!rsp_fd, -1, "release_rsp_cb is null ,can't send user rsp data.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!rsp_fd->conn, -1, "conn is null ,can't send user rsp data.");

	rsp_fd_ex = (struct arpc_rsp_handle*)rsp_fd->ex_data;
	rsp_msg = &rsp_fd_ex->x_req_msg;
	rsp_iov = rsp_fd_ex->rsp_usr_iov;
	rsp_fd->tx_msg = rsp_msg;
	
	LOG_THEN_RETURN_VAL_IF_TRUE(!rsp_fd_ex->x_rsp_msg, -1, "x_rsp_msg is null ,can't send user rsp data.");

	if(rsp_iov && rsp_iov->head && rsp_iov->head_len){
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!rsp_fd_ex->release_rsp_cb, rsp_default, "release_rsp_cb is null ,can't send user rsp data.");

		rsp_msg->out.header.iov_base = rsp_iov->head;
		rsp_msg->out.header.iov_len  = rsp_iov->head_len;
		//data
		if (rsp_iov->vec && rsp_iov->vec_num) {
			rsp_msg->out.total_data_len  = rsp_iov->total_data;
			rsp_msg->out.sgl_type = XIO_SGL_TYPE_IOV_PTR;
			rsp_msg->out.pdata_iov.sglist = ARPC_MEM_ALLOC(rsp_iov->vec_num * sizeof(struct xio_iovec_ex), NULL);
			for (i = 0; i < rsp_iov->vec_num; i++){
				rsp_msg->out.pdata_iov.sglist[i].iov_base = rsp_iov->vec[i].data;
				rsp_msg->out.pdata_iov.sglist[i].iov_len = rsp_iov->vec[i].len;
				rsp_msg->out.pdata_iov.sglist[i].mr = NULL;
				rsp_msg->out.pdata_iov.sglist[i].user_context = NULL;
			}
			rsp_msg->out.pdata_iov.max_nents = rsp_iov->vec_num;
			vmsg_sglist_set_nents(&rsp_msg->out, rsp_iov->vec_num);
		}else{
			rsp_msg->out.pdata_iov.max_nents = 0;
			vmsg_sglist_set_nents(&rsp_msg->out, 0);
		}
		SET_FLAG(rsp_msg->usr_flags, FLAG_RSP_USER_DATA);
		goto rsp;
	}else{
		ARPC_LOG_ERROR("response is empty, no head and data.");
	}
rsp_default:
	rsp_msg->out.header.iov_base = NULL;
	rsp_msg->out.header.iov_len  = 0;
	rsp_msg->out.sgl_type = XIO_SGL_TYPE_IOV;
	vmsg_sglist_set_nents(&rsp_msg->out, 0);
	CLR_FLAG(rsp_msg->usr_flags, FLAG_RSP_USER_DATA);
rsp:	
	rsp_msg->request = rsp_fd_ex->x_rsp_msg;
	rsp_msg->user_context = (void*)rsp_fd;
	return 0;
}

// 注意必须在同步回调线程里执行
int arpc_send_response_complete(struct arpc_common_msg *rsp_fd)
{
	int ret;
	struct arpc_rsp_handle *rsp_fd_ex;
	LOG_THEN_RETURN_VAL_IF_TRUE(!rsp_fd, ARPC_ERROR, "rsp_fd IS NULL");

	ret = arpc_cond_lock(&rsp_fd->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock fail.");

	rsp_fd_ex = (struct arpc_rsp_handle*)rsp_fd->ex_data;
	if (rsp_fd_ex->release_rsp_cb && rsp_fd_ex->rsp_usr_iov) {
		rsp_fd_ex->release_rsp_cb(rsp_fd_ex->rsp_usr_iov, rsp_fd_ex->rsp_usr_ctx);
	}
	ret = arpc_cond_unlock(&rsp_fd->cond);
	arpc_destroy_common_msg(rsp_fd);

	return 0;
}
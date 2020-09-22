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
#include "arpc_make_request.h"


#ifdef 	_DEF_SESSION_CLIENT

#define MAX_SEND_ONEWAY_END_TIME 10*1000

typedef int (*func_xio_send_msg)(struct xio_connection *conn, struct xio_msg *msg);

static struct xio_msg *_arpc_create_xio_msg(uint32_t *flag, struct arpc_vmsg *send, struct xio_msg *req);
static void _arpc_destroy_xio_msg(struct arpc_msg *msg);

static struct arpc_msg *_xio_create_arpc_msg(struct xio_msg *rsp_msg);
static void _xio_destroy_arpc_msg(struct xio_msg *rsp_msg);

static int _alloc_buf_to_rsp_msg(struct xio_msg *rsp);
static int _free_buf_on_rsp_msg(struct xio_msg *rsp);

/**
 * 发送一个请求消息
 * @param[in] fd ,a session handle
 * @param[in] msg ,a data that will send
 * @return receive .0,表示发送成功，小于0则失败
 */
int arpc_do_request(const arpc_session_handle_t fd, struct arpc_msg *msg, int32_t timeout_ms)
{
	struct arpc_session_handle *session_ctx = (struct arpc_session_handle *)fd;
	struct xio_msg 	*req = NULL;
	int ret = ARPC_ERROR;
	struct arpc_msg_data *pri_msg = NULL;
	struct arpc_msg *rev_msg;
	struct arpc_connection *con;
	uint8_t is_crl;
	LOG_THEN_RETURN_VAL_IF_TRUE((!session_ctx || !msg ), ARPC_ERROR, "arpc_session_handle_t fd null, exit.");

	is_crl = (msg->send.total_data > IOV_DEFAULT_MAX_LEN)?1:0;
	ret = get_connection(session_ctx, &con, is_crl, 10*1000);
	LOG_THEN_RETURN_VAL_IF_TRUE(!con, ARPC_ERROR, "not idle connection fail.");

	pri_msg = (struct arpc_msg_data*)msg->handle;
	pri_msg->send = &msg->send;
	pthread_mutex_lock(&pri_msg->lock);	// to lock

	// get msg
	req = _arpc_create_xio_msg(&pri_msg->flag, pri_msg->send, &pri_msg->x_msg);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((req == NULL), error, "_arpc_convert_xio_msg fail.");
	req->user_context = msg;

	ret = xio_send_request(con->xio_con, req);

	MSG_SET_REQ(pri_msg->flag);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, error, "xio_send_msg fail, ret:%d.", ret);
	
	// 全部等待回复
	if (timeout_ms > 0)
		ret = _arpc_wait_request_rsp(pri_msg, timeout_ms);
	else
		ret = pthread_cond_wait(&pri_msg->cond, &pri_msg->lock);
	LOG_ERROR_IF_VAL_TRUE(ret, "receive rsp msg fail for time out or system fail.");
	MSG_CLR_REQ(pri_msg->flag);

	//取回复对的数据
	rev_msg = _xio_create_arpc_msg(req);
	if (!rev_msg || rev_msg != msg){
		ARPC_LOG_ERROR("receive msg invalid.");
		ret = ARPC_ERROR;
	}
	_arpc_destroy_xio_msg(msg); // 释放发送资源

	pthread_mutex_unlock(&pri_msg->lock);	//un lock
	ret = put_connection(session_ctx, con);
	LOG_ERROR_IF_VAL_TRUE(ret, "put_connection fail.");
	return ret;

error:
	_arpc_destroy_xio_msg(msg);
	pthread_mutex_unlock(&pri_msg->lock);	//un lock
	ret = put_connection(session_ctx, con);
	LOG_ERROR_IF_VAL_TRUE(ret, "put_connection fail.");
	return ARPC_ERROR;	
}

// 发送消息完成后处理方式
int _request_send_complete(struct arpc_msg *msg)
{
	struct arpc_msg_data *pri_msg =NULL;
	struct xio_msg 	*req = NULL;
	LOG_THEN_RETURN_VAL_IF_TRUE((!msg), ARPC_ERROR, "msg null ,fail.");
	pri_msg = (struct arpc_msg_data*)msg->handle;
	pthread_mutex_lock(&pri_msg->lock);
	req = &pri_msg->x_msg;
	if (msg->clean_send_cb){
		msg->clean_send_cb(pri_msg->send, pri_msg->usr_ctx);
	}
	if (req->type == XIO_MSG_TYPE_ONE_WAY) {
		CLR_FLAG(pri_msg->flag, XIO_MSG_REQ);
		CLR_FLAG(pri_msg->flag, XIO_MSG_RSP);
	}
	_arpc_destroy_xio_msg(msg);
	if (IS_SET(pri_msg->flag, XIO_SEND_END_TO_NOTIFY)){
		pthread_cond_signal(&pri_msg->cond);
		CLR_FLAG(pri_msg->flag, XIO_SEND_END_TO_NOTIFY);
	}
	pthread_mutex_unlock(&pri_msg->lock);

	if (IS_SET(pri_msg->flag, XIO_RELEASE_ARPC_MSG)){
		pthread_mutex_lock(&pri_msg->lock);
		pthread_mutex_unlock(&pri_msg->lock);
		arpc_delete_msg(&msg);
	}

	return ARPC_SUCCESS;
}

/**
 * 发送一个单向消息（接收方无需回复）
 * @param[in] fd ,a session handle
 * @param[in] msg ,a data that will send
 * @return receive .0,表示发送成功，小于0则失败
 */
int arpc_send_oneway_msg(const arpc_session_handle_t fd, struct arpc_vmsg *send, clean_send_cb_t clean_send, void *send_ctx)
{
	struct arpc_session_handle *session_ctx;
	int ret = ARPC_ERROR;
	struct xio_msg 	*req;
	struct arpc_conn_ow_msg *poneway_msg;
	uint32_t flags;
	struct arpc_connection *con;
	uint8_t is_crl;
	uint32_t send_cnt;

	session_ctx = (struct arpc_session_handle *)fd;

	LOG_THEN_RETURN_VAL_IF_TRUE((!session_ctx || !send ), ARPC_ERROR, "handle null, fail.");

	is_crl = (send->total_data > 4*IOV_DEFAULT_MAX_LEN)?1:0;
	ret = get_connection(session_ctx, &con, is_crl, MAX_SEND_ONEWAY_END_TIME);
	LOG_THEN_RETURN_VAL_IF_TRUE(!con, ARPC_ERROR, "not idle connection fail.");

	ret = conn_get_owmsg(con, &poneway_msg, MAX_SEND_ONEWAY_END_TIME);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, put_con, "conn_get_owmsg fail.");

	ret = arpc_cond_lock(&poneway_msg->cond);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, put_owmsg, "lock poneway_msg cond fail.");

	poneway_msg->clean_send = clean_send;
	poneway_msg->send_ctx = send_ctx;
	poneway_msg->send = send;

	// get msg
	flags = 0;
	req = _arpc_create_xio_msg(&flags, poneway_msg->send, &poneway_msg->x_msg);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((req == NULL), unlock_cond, "arpc convert xio msg fail.");
	req->user_context = poneway_msg;
	req->flags = 0;
	req->flags |= XIO_MSG_FLAG_IMM_SEND_COMP; // 立马回复

send_retry:
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(send_cnt > 3, unlock_cond, "send_cnt[%u] is over max3 fail.", send_cnt);
	send_cnt++;
	ret = xio_send_msg(con->xio_con, req);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, unlock_cond, "xio_send_msg fail.");

	ret = put_connection(session_ctx, con);
	LOG_ERROR_IF_VAL_TRUE(ret, "put_connection fail.");

	if(!poneway_msg->clean_send){
		ret = arpc_cond_wait_timeout(&poneway_msg->cond, MAX_SEND_ONEWAY_END_TIME);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, send_retry, "wait one way msg send complete timeout, try send cnt[%u].", send_cnt);
	}
	ret = arpc_cond_unlock(&poneway_msg->cond);
	LOG_ERROR_IF_VAL_TRUE(ret, "unlock poneway_msg cond fail.");

	ret = put_connection(session_ctx, con);
	LOG_ERROR_IF_VAL_TRUE(ret, "put_connection fail.");

	return 0;
unlock_cond:
	ret = arpc_cond_unlock(&poneway_msg->cond);
	LOG_ERROR_IF_VAL_TRUE(ret, "unlock poneway_msg cond fail.");
put_owmsg:
	ret = conn_put_owmsg(con, poneway_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "conn_put_owmsg fail.");
put_con:
	ret = put_connection(session_ctx, con);
	LOG_ERROR_IF_VAL_TRUE(ret, "put_connection fail.");

	return ARPC_ERROR;
}

/**
 * 释放发送一个单向消息
 * @param[in] oneway_msg ,a session handle
 * @return receive .0,表示发送成功，小于0则失败
 */
int _oneway_send_complete(struct arpc_conn_ow_msg *oneway_msg, void *con_usr_ctx)
{
	struct arpc_connection *con = (struct arpc_connection *)con_usr_ctx;
	int ret;
	LOG_THEN_RETURN_VAL_IF_TRUE(!oneway_msg, ARPC_ERROR, "oneway_msg null, fail.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!con, ARPC_ERROR, "con null, fail.");

	ret = conn_put_owmsg(con, oneway_msg);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "conn_put_owmsg fail.");
	ARPC_LOG_DEBUG("send end complete xio_con:[%u][%p].", con->id, con);
	return 0;
}
/*! 
 * @brief 回复消息个请求方
 * 
 * 回复一个消息个请求方（只能是请求的消息才能回复）
 * 
 * @param[in] rspsession_ctx ,a rsp msg handle, 由接收到消息回复时获取
 * @param[in] rsp_iov ,回复的消息
 * @param[in] release_rsp_cb ,回复结束后回调函数，用于释放调用者的回复消息资源
 * @param[in] rsp_cb_ctx , 回调函数调用者入参
 * @return int .0,表示发送成功，小于0则失败
 */
int arpc_do_response(arpc_rsp_handle_t *rspsession_ctx, struct arpc_vmsg *rsp_iov, rsp_cb_t release_rsp_cb, void* rsp_cb_ctx)
{
	LOG_THEN_RETURN_VAL_IF_TRUE((!rspsession_ctx || !*rspsession_ctx), ARPC_ERROR, "rspsession_ctx is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!rsp_iov), ARPC_ERROR, "rsp_iov is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!release_rsp_cb), ARPC_ERROR, "rsp_iov is null.");
	_do_respone(rsp_iov, (struct xio_msg *)*rspsession_ctx, release_rsp_cb, rsp_cb_ctx);
	*rspsession_ctx = NULL;
	return 0;
}
// 已加锁
int _arpc_rev_request_head(struct xio_msg *in_rsp)
{
	struct arpc_msg  *msg = NULL;
	struct arpc_msg_data *pri_msg = NULL;
	int ret = ARPC_ERROR;
	struct arpc_msg *rev_msg;

	LOG_THEN_RETURN_VAL_IF_TRUE((!in_rsp), ARPC_ERROR, "in_rsp null.");
	msg = (struct arpc_msg *)in_rsp->user_context;
	LOG_THEN_RETURN_VAL_IF_TRUE((!msg), ARPC_ERROR, "msg invalid.");
	pri_msg = (struct arpc_msg_data *)msg->handle;
	pthread_mutex_lock(&pri_msg->lock);	// to lock
	ret = _alloc_buf_to_rsp_msg(in_rsp);
	pthread_mutex_unlock(&pri_msg->lock);	//un lock	
	return ret;
}

// 已加锁
int _arpc_rev_request_rsp(struct xio_msg *in_rsp)
{
	struct arpc_msg  *msg = NULL;
	struct arpc_msg_data *pri_msg = NULL;
	int ret = ARPC_ERROR;
	struct arpc_msg *rev_msg;

	LOG_THEN_RETURN_VAL_IF_TRUE((!in_rsp), ARPC_ERROR, "in_rsp null.");
	msg = (struct arpc_msg *)in_rsp->user_context;
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!msg), end, "msg invalid.");
	pri_msg = (struct arpc_msg_data *)msg->handle;
	pthread_mutex_lock(&pri_msg->lock);	// to lock
	if (msg->proc_rsp_cb){
		rev_msg = _xio_create_arpc_msg(in_rsp);
		if (!rev_msg || rev_msg != msg){
			ARPC_LOG_ERROR( "receive msg invalid.");
			ret = ARPC_ERROR;
		}
		ret = msg->proc_rsp_cb(&rev_msg->receive, pri_msg->usr_ctx);
		LOG_ERROR_IF_VAL_TRUE(ret, "proc_rsp_cb fail.");
		_release_rsp_msg(msg);
		if (in_rsp->type == XIO_MSG_TYPE_RSP) {
			pthread_cond_signal(&pri_msg->cond);// 通知
		}
	}else{
		pthread_cond_signal(&pri_msg->cond);// 通知
	}
end:
	pthread_mutex_unlock(&pri_msg->lock);	//un lock
	return ret;	
}

// 无锁
int _release_rsp_msg(struct arpc_msg *msg)
{
	struct xio_msg 	*rsp = NULL;
	struct arpc_msg_data *pri_msg = NULL;
	LOG_THEN_RETURN_VAL_IF_TRUE((!msg), ARPC_ERROR, "msg null ,fail.");
	pri_msg = (struct arpc_msg_data*)msg->handle;
	rsp = &(pri_msg->x_msg);
	_arpc_destroy_xio_msg(msg); // 释放发送资源
	_free_buf_on_rsp_msg(rsp); // 释放自定义内存
	_xio_destroy_arpc_msg(rsp);// 释放自定义IOV指针
	xio_release_response(rsp); // 释放内部资源
	return 0;
}

// private funciton
static struct xio_msg *_arpc_create_xio_msg(uint32_t *flag, struct arpc_vmsg *send, struct xio_msg *req)
{
	uint32_t i;

	/* header */
	LOG_THEN_RETURN_VAL_IF_TRUE((!send->head || !send->head_len 
								|| send->head_len > MAX_HEADER_DATA_LEN), 
								NULL, "msg head is invalid, header:%p, len:%u.", 
								send->head, 
								send->head_len);
	
	(void)memset(req, 0, sizeof(struct xio_msg));
	req->out.header.iov_base = send->head;
	req->out.header.iov_len = send->head_len;
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(req->out.header.iov_len > MAX_HEADER_DATA_LEN, 
								data_null, 
								"header len[%lu] is over max limit[%u].",
								req->out.header.iov_len,
								MAX_HEADER_DATA_LEN);
	/* data */
	req->out.sgl_type	   = XIO_SGL_TYPE_IOV_PTR;
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((send->total_data > DATA_DEFAULT_MAX_LEN), data_null, 
									"send total_data[%lu] is over max size[%lu].",
									send->total_data,
									(uint64_t)DATA_DEFAULT_MAX_LEN);

	req->out.pdata_iov.max_nents = send->vec_num;
	req->out.pdata_iov.nents = send->vec_num;
	if (req->out.pdata_iov.nents) {
		req->out.pdata_iov.sglist = (struct xio_iovec_ex *)ARPC_MEM_ALLOC( send->vec_num * sizeof(struct xio_iovec_ex), NULL);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!req->out.pdata_iov.sglist, data_null, "ARPC_MEM_ALLOC fail.");

		SET_FLAG(*flag, XIO_SEND_MSG_ALLOC_BUF);// 标识分配内存
		for (i =0; i < send->vec_num; i++){
			LOG_THEN_GOTO_TAG_IF_VAL_TRUE(send->vec[i].len > IOV_DEFAULT_MAX_LEN, 
											data_null, 
											"vec len[%lu] is over max limit[%u].",
											send->vec[i].len,
											IOV_DEFAULT_MAX_LEN);
			req->out.pdata_iov.sglist[i].iov_base = send->vec[i].data;
			req->out.pdata_iov.sglist[i].iov_len = send->vec[i].len;
		}
	}else{
		req->out.sgl_type = XIO_SGL_TYPE_IOV;
		req->out.pdata_iov.nents = 0;
	}
	
	/* receive 默认方式*/
	req->in.sgl_type	   		= XIO_SGL_TYPE_IOV;
	req->in.data_iov.max_nents  = XIO_IOVLEN;

	return req;
data_null:
	req->out.pdata_iov.max_nents =0;
	req->out.pdata_iov.nents = 0;
	req->out.pdata_iov.sglist = NULL;
	return NULL;
}
// 释放发送锁自动分配的资源
static void _arpc_destroy_xio_msg(struct arpc_msg *msg)
{
	struct xio_msg 	*req = NULL;
	struct arpc_msg_data *pri_msg = (struct arpc_msg_data*)msg->handle;
	req = &(pri_msg->x_msg);
	if (IS_SET(pri_msg->flag, XIO_SEND_MSG_ALLOC_BUF) && req->out.pdata_iov.sglist){
		ARPC_MEM_FREE(req->out.pdata_iov.sglist, NULL);
		req->out.pdata_iov.sglist = NULL;
	}
	CLR_FLAG(pri_msg->flag, XIO_SEND_MSG_ALLOC_BUF);
	memset(&req->out, 0, sizeof(struct xio_vmsg));
	return;
}

static struct arpc_msg *_xio_create_arpc_msg(struct xio_msg *rsp_msg)
{
	struct arpc_msg  *msg = NULL;
	struct arpc_msg_data  *pri_msg = NULL;
	struct xio_iovec_ex	*sglist = NULL;
	uint32_t			nents = 0;
	uint32_t 			i;
	int 			ret = -1;

	LOG_THEN_RETURN_VAL_IF_TRUE((!rsp_msg), NULL, "rsp_msg null, exit.");
	nents = vmsg_sglist_nents(&rsp_msg->in);

	msg = (struct arpc_msg *)rsp_msg->user_context;
	LOG_THEN_RETURN_VAL_IF_TRUE((!msg), NULL, "msg null, exit.");
	msg->receive.head = rsp_msg->in.header.iov_base;
	msg->receive.head_len = rsp_msg->in.header.iov_len;
	msg->receive.total_data = rsp_msg->in.total_data_len;

	pri_msg = (struct arpc_msg_data *)msg->handle;
	if (!IS_SET(pri_msg->flag, XIO_MSG_ALLOC_BUF) && nents) {
		// 内部buf, 结构体需要转换，复制指针，不做copy
		sglist = vmsg_sglist(&rsp_msg->in);
		msg->receive.vec = (struct arpc_iov *)ARPC_MEM_ALLOC(nents * sizeof(struct arpc_iov), NULL);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!msg->receive.vec), end,"vec alloc is empty.");
		msg->receive.vec_num = nents;
		msg->receive.total_data = 0;
		SET_FLAG(pri_msg->flag, XIO_RSP_IOV_ALLOC_BUF); // 标识分配内存
		for(i = 0; i < nents; i++){
			msg->receive.vec[i].data = sglist[i].iov_base;
			msg->receive.vec[i].len	= sglist[i].iov_len;
			msg->receive.total_data +=msg->receive.vec[i].len;
		}
	}else if (nents){
		// 自定义buf，结构体可以强制转换
		sglist = vmsg_base_sglist(&rsp_msg->in);
		msg->receive.vec = (struct arpc_iov *)sglist;
		msg->receive.vec_num = nents;
	}else{
		msg->receive.vec = 0;
		msg->receive.vec_num = 0;
		msg->receive.total_data = 0;
	}

end:
	SET_FLAG(pri_msg->flag, XIO_MSG_RSP);
	return msg;
}

// 释放内部拷贝的指针数组
static void _xio_destroy_arpc_msg(struct xio_msg *rsp_msg)
{
	struct arpc_msg  *msg = NULL;
	struct arpc_msg_data  *pri_msg = NULL;
	struct xio_iovec_ex	*sglist = NULL;
	uint32_t			nents = 0;
	uint32_t 			i;
	int 			ret = -1;

	LOG_THEN_RETURN_IF_VAL_TRUE((!rsp_msg), "rsp_msg null, exit.");
	msg = (struct arpc_msg *)rsp_msg->user_context;
	LOG_THEN_RETURN_IF_VAL_TRUE((!msg), "msg null, exit.");
	pri_msg = (struct arpc_msg_data *)msg->handle;
	if (IS_SET(pri_msg->flag, XIO_RSP_IOV_ALLOC_BUF)){
		ARPC_MEM_FREE(msg->receive.vec, NULL);
		msg->receive.vec =NULL;
		msg->receive.vec_num =0;
		msg->receive.total_data =0;
		CLR_FLAG(pri_msg->flag, XIO_RSP_IOV_ALLOC_BUF);
	}
	CLR_FLAG(pri_msg->flag, XIO_MSG_RSP);
	return;
}

static int _alloc_buf_to_rsp_msg(struct xio_msg *rsp)
{
	struct arpc_msg  *msg = (struct arpc_msg *)rsp->user_context;
	struct arpc_msg_data  *pri_msg = NULL;
	struct xio_iovec	*sglist = NULL;
	uint32_t			nents = 0;
	uint32_t 			i;
	int 			ret = -1;

	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!msg), error, "msg invalid.");
	pri_msg = (struct arpc_msg_data *)msg->handle;

	// 分配内存
	if (!pri_msg->alloc_cb || !pri_msg->free_cb || IS_SET(pri_msg->flag ,XIO_MSG_CANCEL)){
		goto error;
	}
	nents = ((rsp->in.total_data_len / pri_msg->iov_max_len) + 1);
	if (nents) {
		sglist = (struct xio_iovec* )ARPC_MEM_ALLOC(nents * sizeof(struct xio_iovec), NULL);
		if (!sglist) {
			goto error;
		}

		// 分配资源
		for (i = 0; i < nents -1; i++) {
			sglist[i].iov_len = pri_msg->iov_max_len;
			sglist[i].iov_base = pri_msg->alloc_cb(sglist[i].iov_len, pri_msg->usr_ctx);
			LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!sglist[i].iov_base), error_1, "calloc fail.");
		};
		sglist[i].iov_len = (rsp->in.total_data_len % pri_msg->iov_max_len);
		sglist[i].iov_base = pri_msg->alloc_cb(sglist[i].iov_len, pri_msg->usr_ctx);
		
		rsp->in.sgl_type = XIO_SGL_TYPE_IOV_PTR;
		rsp->in.data_tbl.sglist = sglist;
		SET_FLAG(pri_msg->flag, XIO_MSG_ALLOC_BUF);
	}else{
		rsp->in.sgl_type = XIO_SGL_TYPE_IOV;
	}
	vmsg_sglist_set_nents(&rsp->in, nents);
	rsp->in.data_tbl.max_nents = nents;

	return 0;
error_1:
	for (i = 0; i < nents; i++) {
		if (sglist[i].iov_base)
			pri_msg->free_cb(sglist[i].iov_base, pri_msg->usr_ctx);
		sglist[i].iov_base =NULL;
	}
error:
	if (sglist) {
		ARPC_MEM_FREE(sglist, NULL);
		sglist = NULL;
	}
	/* receive 默认方式*/
	CLR_FLAG(pri_msg->flag ,XIO_MSG_ALLOC_BUF);
	return -1;
}

static int _free_buf_on_rsp_msg(struct xio_msg *rsp)
{
	struct arpc_msg  *msg = (struct arpc_msg *)rsp->user_context;
	struct arpc_msg_data  *pri_msg = NULL;
	struct xio_iovec	*sglist = NULL;
	uint32_t			nents = 0;
	uint32_t 			i;

	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!rsp), end, "msg null.");
	pri_msg = (struct arpc_msg_data *)msg->handle;
	if (!IS_SET(pri_msg->flag, XIO_MSG_ALLOC_BUF)) {
		goto end;
	}
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!pri_msg->free_cb), end, "alloc free is null.");

	// 释放内存
	nents = vmsg_sglist_nents(&rsp->in);
	sglist = vmsg_base_sglist(&rsp->in);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!sglist), end, "rsp[%p],sglist is null.", rsp);
	for (i = 0; i < nents; i++) {
		if (sglist[i].iov_base)
			pri_msg->free_cb(sglist[i].iov_base, pri_msg->usr_ctx);
	}
	if (sglist)
		ARPC_MEM_FREE(sglist, NULL);
	
	// 出参
	rsp->in.sgl_type = XIO_SGL_TYPE_IOV;
	vmsg_sglist_set_nents(&rsp->in, 0);
	rsp->in.data_iov.max_nents = XIO_IOVLEN;
	CLR_FLAG(pri_msg->flag ,XIO_MSG_ALLOC_BUF);
end:	
	return 0;
}


#endif

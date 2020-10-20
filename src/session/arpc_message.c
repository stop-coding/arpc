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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <unistd.h>

#include "arpc_com.h"
#include "arpc_request.h"
#include "arpc_message.h"

static int release_rsp_msg_buf(struct arpc_msg *msg);

static void *arpc_msg_alloc(uint32_t size, void *usr_context)
{
	void *mem = malloc(size);
	return mem;
}
static int arpc_msg_free(void *buf_ptr, void *usr_context)
{
	if(buf_ptr)
		free(buf_ptr);
	return 0;
}

struct arpc_msg *arpc_new_msg(const struct arpc_msg_param *p)
{
	struct arpc_msg *ret_msg = NULL;
	struct arpc_msg_ex *ex_msg = NULL;
	int ret;

	ret_msg = (struct arpc_msg*)arpc_mem_alloc(sizeof(struct arpc_msg) + sizeof(struct arpc_msg_ex),NULL);
	LOG_THEN_RETURN_VAL_IF_TRUE(!ret_msg, NULL, "arpc_mem_alloc arpc_msg fail.");

	memset(ret_msg, 0, sizeof(struct arpc_msg) + sizeof(struct arpc_msg_ex));
	ex_msg = (struct arpc_msg_ex *)ret_msg->handle;
	ex_msg->flags = 0;	// 暂时不使用用户的flag
	if (p){
		ex_msg->alloc_cb = p->alloc_cb;
		ex_msg->free_cb = p->free_cb;
		ex_msg->usr_context = p->usr_context;
	}else{
		ex_msg->alloc_cb = &arpc_msg_alloc;
		ex_msg->free_cb = &arpc_msg_free;
		ex_msg->usr_context =NULL;
	}
	ex_msg->iov_max_len = IOV_DEFAULT_MAX_LEN;
	ex_msg->msg = ret_msg;
	return ret_msg;
}


int arpc_delete_msg(struct arpc_msg **msg)
{
	struct arpc_msg_ex *ex_msg;
	struct arpc_msg *free_msg;
	LOG_THEN_RETURN_VAL_IF_TRUE(!msg, ARPC_ERROR, "msg is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!*msg, ARPC_ERROR, "*msg is null.");
	
	free_msg = *msg;
	ex_msg = (struct arpc_msg_ex*)free_msg->handle;
	if (IS_SET(ex_msg->flags, XIO_MSG_REQ)) {
		ARPC_LOG_ERROR("can't delete msg that be do request.");
		return -1;
	}

	if (IS_SET(ex_msg->flags, XIO_MSG_RSP)) {
		release_rsp_msg_buf(free_msg);
	}
	if(free_msg->clean_send_cb){
		free_msg->clean_send_cb(&free_msg->send, free_msg->send_ctx);
		free_msg->clean_send_cb = NULL;
	}
	release_arpc2xio_msg(&ex_msg->x_req_msg.out);
	SAFE_FREE_MEM(*msg);

	return 0;
}

int arpc_reset_msg(struct arpc_msg *msg)
{
	struct arpc_msg_ex *ex_msg;
	LOG_THEN_RETURN_VAL_IF_TRUE(!msg, ARPC_ERROR, "msg is null.");

	ex_msg = (struct arpc_msg_ex*)msg->handle;
	if (IS_SET(ex_msg->flags, XIO_MSG_REQ)) {
		ARPC_LOG_ERROR("can't reset msg that be do request.");
		return -1;
	}

	if (IS_SET(ex_msg->flags, XIO_MSG_RSP)) {
		release_rsp_msg_buf(msg);
	}
	ex_msg->flags = 0;
	if(msg->clean_send_cb){
		msg->clean_send_cb(&msg->send, msg->send_ctx);
		msg->clean_send_cb = NULL;
	}
	release_arpc2xio_msg(&ex_msg->x_req_msg.out);
	return 0;
}

int conver_msg_arpc_to_xio(const struct arpc_vmsg *usr_msg, struct xio_vmsg	*xio_msg)
{
	uint32_t i;
	/* header */
	xio_msg->header.iov_base = usr_msg->head;
	xio_msg->header.iov_len = usr_msg->head_len;
	xio_msg->pdata_iov.max_nents = usr_msg->vec_num;
	xio_msg->pdata_iov.nents = usr_msg->vec_num;
	xio_msg->total_data_len = 0;
	if (xio_msg->pdata_iov.nents) {
		xio_msg->pdata_iov.sglist = (struct xio_iovec_ex *)arpc_mem_alloc( usr_msg->vec_num * sizeof(struct xio_iovec_ex), NULL);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!xio_msg->pdata_iov.sglist, data_null, "arpc_mem_alloc fail.");
		memset(xio_msg->pdata_iov.sglist, 0, usr_msg->vec_num * sizeof(struct xio_iovec_ex));
		xio_msg->pad = 1;
		for (i =0; i < usr_msg->vec_num; i++){
			ARPC_ASSERT(usr_msg->vec[i].data, "data is null.");
			ARPC_ASSERT(usr_msg->vec[i].len, "data len is 0.");
			xio_msg->pdata_iov.sglist[i].iov_base = usr_msg->vec[i].data;
			xio_msg->pdata_iov.sglist[i].iov_len = usr_msg->vec[i].len;
			xio_msg->total_data_len += xio_msg->pdata_iov.sglist[i].iov_len;
		}
		xio_msg->sgl_type = XIO_SGL_TYPE_IOV_PTR;
		ARPC_ASSERT(xio_msg->total_data_len <= DATA_DEFAULT_MAX_LEN, "total_data_len is over.");
	}else{
		xio_msg->sgl_type = XIO_SGL_TYPE_IOV;
		xio_msg->data_iov.nents = 0;
		xio_msg->data_iov.max_nents = XIO_IOVLEN;
		xio_msg->pad = 0;
	}
	
	return 0;
data_null:
	xio_msg->header.iov_base = 0;
	xio_msg->header.iov_len = 0;
	xio_msg->pdata_iov.max_nents =0;
	xio_msg->pdata_iov.nents = 0;
	xio_msg->pdata_iov.sglist = NULL;
	return -1;
}
// 释放发送锁自动分配的资源
void release_arpc2xio_msg(struct xio_vmsg *xio_msg)
{
	struct xio_msg 	*req = NULL;
	if ((xio_msg->pad) && xio_msg->pdata_iov.sglist){
		SAFE_FREE_MEM(xio_msg->pdata_iov.sglist);
		xio_msg->pad = 0;
	}
	
	return;
}

int conver_msg_xio_to_arpc(const struct xio_vmsg *xio_msg, struct arpc_vmsg *msg)
{
	const struct xio_iovec_ex  *sglist = NULL;
	uint32_t			nents = 0;
	uint32_t 			i;
	int 				ret = -1;

	LOG_THEN_RETURN_VAL_IF_TRUE((!xio_msg), ARPC_ERROR, "xio_msg null, exit.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!msg), ARPC_ERROR, "msg null, exit.");

	nents = vmsg_sglist_nents(xio_msg);
	//head deepcopy
	msg->head_len = xio_msg->header.iov_len;
	if(msg->head_len){
		msg->head = arpc_mem_alloc(xio_msg->header.iov_len, NULL);
		memcpy(msg->head, xio_msg->header.iov_base, msg->head_len);
	}else{
		ARPC_LOG_ERROR("head len is 0.");
		msg->head =NULL;
	}
	msg->head_len = xio_msg->header.iov_len;
	msg->total_data = xio_msg->total_data_len;
	if ((xio_msg->sgl_type == XIO_SGL_TYPE_IOV)&& nents) {
		sglist = vmsg_sglist(xio_msg);
		msg->vec = (struct arpc_iov *)arpc_mem_alloc(nents * sizeof(struct arpc_iov), NULL);
		msg->vec_type = ARPC_VEC_TYPE_INTER;
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!msg->vec), end,"vec alloc is empty.");
		msg->vec_num = nents;
		msg->total_data = 0;
		for(i = 0; i < nents; i++){
			msg->vec[i].data = sglist[i].iov_base;
			msg->vec[i].len	= sglist[i].iov_len;
			msg->total_data +=msg->vec[i].len;
		}
	}else if (nents && (xio_msg->sgl_type == XIO_SGL_TYPE_IOV_PTR)){
		// 自定义buf，结构体可以强制转换
		sglist = vmsg_sglist(xio_msg);
		msg->vec = (struct arpc_iov *)arpc_mem_alloc(nents * sizeof(struct arpc_iov), NULL);
		for(i = 0; i < nents; i++){
			msg->vec[i].data = sglist[i].iov_base;
			msg->vec[i].len	= sglist[i].iov_len;
			msg->total_data +=msg->vec[i].len;
		}
		msg->vec_num = nents;
		msg->vec_type = ARPC_VEC_TYPE_PRT;
	}else{
		msg->vec = 0;
		msg->vec_num = 0;
		msg->total_data = 0;
		if (!msg->head_len) {
			ARPC_LOG_ERROR("no header and data in msg.");
			msg->vec_type = ARPC_VEC_TYPE_NONE;
		}
	}
end:
	return 0;
}

// 释放内部拷贝的指针数组
void release_xio2arpc_msg(struct arpc_vmsg *msg)
{
	LOG_THEN_RETURN_IF_VAL_TRUE((!msg), "msg null, exit.");

	if (msg->vec_type == ARPC_VEC_TYPE_INTER){
		SAFE_FREE_MEM(msg->vec);
		msg->vec =NULL;
		msg->vec_num =0;
		msg->total_data =0;
	}
	SAFE_FREE_MEM(msg->head);
	return;
}

int free_receive_msg_buf(struct arpc_msg *msg)
{
	struct arpc_msg_ex *ex_msg;
	uint32_t i;

	LOG_THEN_RETURN_VAL_IF_TRUE(!msg, ARPC_ERROR, "msg is null.");
	ex_msg = (struct arpc_msg_ex*)msg->handle;
	if(IS_SET(ex_msg->flags, XIO_RSP_IOV_ALLOC_BUF)){
		LOG_THEN_RETURN_VAL_IF_TRUE(!msg->receive.vec, ARPC_ERROR, "receive vec null.");
		LOG_THEN_RETURN_VAL_IF_TRUE(!msg->receive.vec_num, ARPC_ERROR, "receive vec null.");
		LOG_THEN_RETURN_VAL_IF_TRUE(msg->receive.vec_type != ARPC_VEC_TYPE_PRT, ARPC_ERROR, "vec_type invalid.");
		for (i = 0; i < msg->receive.vec_num; i++) {
			if (msg->receive.vec[i].data)
				ex_msg->free_cb(msg->receive.vec[i].data, ex_msg->usr_context);
		}
		SAFE_FREE_MEM(msg->receive.vec);
	}
	
	return 0;
}
int alloc_xio_msg_usr_buf(struct xio_msg *msg, struct arpc_msg *arpc_msg)
{
	struct xio_iovec_ex	*sglist;
	uint32_t			nents = 0;
	uint32_t flags = 0;
	uint32_t i;
	int ret;
	uint64_t last_size;
	struct arpc_msg_ex *ex_msg;

	LOG_THEN_RETURN_VAL_IF_TRUE((!msg->in.header.iov_base || !msg->in.header.iov_len), -1, "header null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!msg->in.header.iov_len), -1, "header len is 0.");

	ex_msg = (struct arpc_msg_ex*)arpc_msg->handle;
	
	// 分配内存
	last_size = msg->in.total_data_len%ex_msg->iov_max_len;
	nents = (last_size)? 1: 0;
	nents += (msg->in.total_data_len / ex_msg->iov_max_len);
	sglist = (struct xio_iovec_ex* )arpc_mem_alloc(nents * sizeof(struct xio_iovec_ex), NULL);
	last_size = (last_size)? last_size :ex_msg->iov_max_len;
	if(!nents){
		goto error;
	}
	ARPC_LOG_DEBUG("get msg, nent:%u, iov_max_len:%lu, total_size:%lu, sglist:%p", nents, ex_msg->iov_max_len, msg->in.total_data_len, sglist);
	for (i = 0; i < nents - 1; i++) {
		sglist[i].iov_len = ex_msg->iov_max_len;
		sglist[i].iov_base = ex_msg->alloc_cb(sglist[i].iov_len, ex_msg->usr_context);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!sglist[i].iov_base), error_1, "calloc fail.");
	}
	sglist[i].iov_len = last_size;
	sglist[i].iov_base = ex_msg->alloc_cb(sglist[i].iov_len, ex_msg->usr_context);
	ARPC_LOG_DEBUG("i:%u ,data:%p, len:%lu.",i, sglist[i].iov_base, sglist[i].iov_len);
	// 出参
	msg->in.pdata_iov.sglist = sglist;
	vmsg_sglist_set_nents(&msg->in, nents);
	msg->in.sgl_type		= XIO_SGL_TYPE_IOV_PTR;
	arpc_msg->receive.vec_type = ARPC_VEC_TYPE_PRT;
	SET_FLAG(ex_msg->flags, XIO_RSP_IOV_ALLOC_BUF);
	return 0;

error_1:
	for (i = 0; i < nents; i++) {
		if (sglist[i].iov_base)
			ex_msg->free_cb(sglist[i].iov_base, ex_msg->usr_context);
		sglist[i].iov_base =NULL;
	}
error:
	if (sglist) {
		arpc_mem_free(sglist, NULL);
		sglist = NULL;
	}
	msg->in.sgl_type		= XIO_SGL_TYPE_IOV;
	CLR_FLAG(ex_msg->flags, XIO_RSP_IOV_ALLOC_BUF);
	return -1;
}

static int release_rsp_msg_buf(struct arpc_msg *msg)
{
	LOG_THEN_RETURN_VAL_IF_TRUE(!msg, ARPC_ERROR, "msg is null.");
	free_receive_msg_buf(msg);
	release_xio2arpc_msg(&msg->receive);
	return 0;
}
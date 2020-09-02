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
#include <arpa/inet.h>
#include <sys/time.h>

#include "arpc_com.h"
#include "threadpool.h"

struct aprc_paramter{
	int thread_max_num;
	tp_handle thread_pool;
};

static struct aprc_paramter g_param= {
	.thread_max_num = 5,
	.thread_pool	= NULL,
};

static const char SERVER_DEFAULT[] = "rsp-header:undefine";

int get_uri(const struct arpc_con_info *param, char *uri, uint32_t uri_len)
{
	const char *type = NULL, *ip=NULL;
	uint32_t port = 0;
	if (!param || !uri) {
		return -1;
	}
	switch(param->type){
		case ARPC_E_TRANS_TCP:
			type = "tcp";
			ip = param->ipv4.ip;
			port = param->ipv4.port;
			break;
		default:
			ARPC_LOG_ERROR("unkown type:[%d].", param->type);
			return -1;
	}
	(void)sprintf(uri, "%s://%s:%u", type, ip, port);
	ARPC_LOG_NOTICE("uri:[%s].", uri);
	return 0;
}

int _arpc_get_ipv4_addr(struct sockaddr_storage *src_addr, char *ip, uint32_t len, uint32_t *port)
{
	struct sockaddr_in *s4;
	LOG_THEN_RETURN_VAL_IF_TRUE((!src_addr || !ip || !port), ARPC_ERROR, "input null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((src_addr->ss_family != AF_INET || len < INET_ADDRSTRLEN), ARPC_ERROR, "input invalid.");

	s4 = (struct sockaddr_in *)src_addr;
	*port = s4->sin_port;
	inet_ntop(AF_INET, s4, ip, len);
	return ARPC_SUCCESS;
}

int arpc_init()
{
	struct tp_param p = {0};
	ARPC_LOG_DEBUG( "arpc_init.");
	xio_init();
	p.thread_max_num = g_param.thread_max_num;
	g_param.thread_pool = tp_create_thread_pool(&p);
	return 0;
}

void arpc_finish()
{
	ARPC_LOG_DEBUG( "arpc_finish.");
	tp_destroy_thread_pool(&g_param.thread_pool);
	xio_shutdown();
}

tp_handle _arpc_get_threadpool()
{
	return g_param.thread_pool;
}

void _debug_printf_msg(struct xio_msg *rsp)
{
	struct xio_iovec	*sglist = vmsg_base_sglist(&rsp->in);
	char			*str;
	uint32_t		nents = vmsg_sglist_nents(&rsp->in);
	uint32_t		len, i;
	char 			tmp;
	str = (char *)rsp->in.header.iov_base;
	len = rsp->in.header.iov_len;
	if (str) {
		tmp = str[len -1];
		str[len -1] = '\0';
		ARPC_LOG_DEBUG("message header : [%llu] - %s.",(unsigned long long)(rsp->sn + 1), str);
		str[len -1] = tmp;
	}
	for (i = 0; i < nents; i++) {
		str = (char *)sglist[i].iov_base;
		len = sglist[i].iov_len;
		if (str) {
			tmp = str[len -1];
			str[len -1] = '\0';
			ARPC_LOG_DEBUG("message data: [%llu][%d][%d] - %s\n",
					(unsigned long long)(rsp->sn + 1),
					i, len, str);
			str[len -1] = tmp;
		}
	}
	return;
}

int
_arpc_wait_request_rsp(struct arpc_msg_data* pri_msg, int32_t timeout_ms)
{
	int ret;
	struct timespec abstime;
	struct timeval now;
	uint64_t nsec;
	gettimeofday(&now, NULL);	// 线程安全
	nsec = now.tv_usec * 1000 + (timeout_ms % 1000) * 1000000;
	abstime.tv_sec=now.tv_sec + nsec / 1000000000 + timeout_ms / 1000;
	abstime.tv_nsec=nsec % 1000000000;
	ret = pthread_cond_timedwait(&pri_msg->cond, &pri_msg->lock, &abstime);
	return ret;
}


struct _async_deal_msg_param{
	struct xio_msg 			*msg;
	struct arpc_vmsg 		rev_iov;
	struct _async_proc_ops	ops;
	void					*usr_ctx;
};

static int _msg_async_deal(void *usr_ctx)
{
	struct _async_deal_msg_param *async = (struct _async_deal_msg_param *)usr_ctx;
	uint32_t i;
	struct arpc_rsp rsp;
	int ret;
	uint64_t flags;

	ARPC_LOG_DEBUG("Note: msg deal on thread[%lu]...", pthread_self());// to do
	if (!async){
		ARPC_LOG_ERROR("usr_ctx null, exit.");// to do
		return 0;
	}
	LOG_THEN_RETURN_VAL_IF_TRUE((!async->ops.proc_async_cb && !async->ops.proc_oneway_async_cb), 
								ARPC_ERROR, "proc_async_cb null.");
	memset(&rsp, 0, sizeof (struct arpc_rsp));
	rsp.rsp_fd = (void*)async->msg;							
	if (async->ops.proc_async_cb && async->ops.release_rsp_cb){
		ret = async->ops.proc_async_cb(&async->rev_iov, &rsp, async->usr_ctx);
		if (!IS_SET(rsp.flags, METHOD_CALLER_ASYNC)) {
			ret = _do_respone(rsp.rsp_iov, async->msg, async->ops.release_rsp_cb, async->usr_ctx);
			LOG_ERROR_IF_VAL_TRUE(ret, "_do_respone fail.");
		}
	}else if (async->ops.proc_oneway_async_cb) {
		ret = async->ops.proc_oneway_async_cb(&async->rev_iov, &rsp.flags, async->usr_ctx);
		xio_release_msg(async->msg);
	}else{
		ARPC_LOG_ERROR("proc_async_cb invalid, exit.");// to do
	}

	if (!IS_SET(rsp.flags, METHOD_CALLER_HIJACK_RX_DATA)) {
		// 释放资源
		TO_FREE_USER_DATA_BUF(async->ops.free_cb, async->usr_ctx, async->rev_iov.vec, async->rev_iov.vec_num, i);
	}
	// free
	if (async->rev_iov.head)
		ARPC_MEM_FREE(async->rev_iov.head, NULL);
	async->rev_iov.head = NULL;	
	if (async->rev_iov.vec)
		ARPC_MEM_FREE(async->rev_iov.vec, NULL);
	async->rev_iov.vec = NULL;

	ARPC_MEM_FREE(async, NULL);
	return 0;
}

int _post_iov_to_async_thread(struct arpc_vmsg *iov, struct xio_msg *msg, struct _async_proc_ops *ops, void *usr_ctx)
{
	struct _async_deal_msg_param *async;
	struct tp_thread_work thread;
	LOG_THEN_RETURN_VAL_IF_TRUE((!iov || !ops), ARPC_ERROR, "rev_iov or ops null.");

	LOG_THEN_RETURN_VAL_IF_TRUE((!iov->head_len && !iov->vec_num), ARPC_ERROR, "invalid in of iov.");

	LOG_THEN_RETURN_VAL_IF_TRUE((!ops->free_cb), ARPC_ERROR, "ops free invalid.");

	LOG_THEN_RETURN_VAL_IF_TRUE((!ops->proc_async_cb && !ops->proc_oneway_async_cb), 
								ARPC_ERROR, "ops proc_data invalid.");
	/* 线程池申请资源*/
	async = (struct _async_deal_msg_param*)ARPC_MEM_ALLOC(sizeof(struct _async_deal_msg_param), NULL);
	if (!async){
		ARPC_LOG_ERROR( "ARPC_MEM_ALLOC fail, exit ");
		return ARPC_ERROR;
	}
	memset(async, 0, sizeof(struct _async_deal_msg_param));
	// data buff not copy;
	async->ops = *ops;
	async->rev_iov.total_data = iov->total_data;
	async->rev_iov.vec_num = iov->vec_num;
	async->rev_iov.vec = iov->vec;
	async->msg = msg;
	// deep copy;
	if (iov->head_len) {
		async->rev_iov.head = (void*)ARPC_MEM_ALLOC(iov->head_len, NULL);
		if (!async->rev_iov.head)
			goto error;
		async->rev_iov.head_len = iov->head_len;
		memcpy(async->rev_iov.head, iov->head, iov->head_len);
	}
	async->usr_ctx = usr_ctx;
	
	thread.loop = &_msg_async_deal;
	thread.stop = NULL;
	thread.usr_ctx = (void*)async;
	if(!tp_post_one_work(_arpc_get_threadpool(), &thread, WORK_DONE_AUTO_FREE)){
		ARPC_LOG_ERROR( "tp_post_one_work error.");
		goto error;
	}
	iov->vec =NULL;
	iov->vec_num = 0;
	return 0;
error:
	ARPC_LOG_ERROR( "_post_iov_to_async_thread error, exit ");
	if (async->rev_iov.head)
		ARPC_MEM_FREE(async->rev_iov.head, NULL);
	if (async->rev_iov.vec)
		ARPC_MEM_FREE(async->rev_iov.vec, NULL);
	if (async)
		ARPC_MEM_FREE(async, NULL);
	async = NULL;
	return ARPC_ERROR;
}

int _create_header_source(struct xio_msg *msg, struct _proc_header_func *ops, uint64_t iov_max_len, void *usr_ctx)
{
	struct xio_iovec	*sglist;
	uint32_t			nents = 0;
	struct arpc_header_msg header;
	uint32_t flag = 0;
	uint32_t i;
	int ret;

	LOG_THEN_RETURN_VAL_IF_TRUE((!msg->in.header.iov_base || !msg->in.header.iov_len), -1, "header null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!msg->in.header.iov_len), -1, "header len is 0.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!ops->proc_head_cb), -1, "proc_head_cb null.");

	memset(&header, 0, sizeof(struct arpc_header_msg));
	header.head = msg->in.header.iov_base;
	header.head_len = msg->in.header.iov_len;
	header.data_len = msg->in.total_data_len;
	// header process
	msg->usr_flags = 0;
	ret = ops->proc_head_cb(&header, usr_ctx, &flag);
	if (ret != ARPC_SUCCESS || !msg->in.total_data_len){
		SET_FLAG(msg->usr_flags, FLAG_MSG_ERROR_DISCARD_DATA); // data数据不做处理
		ARPC_LOG_DEBUG("discard data, total_data_len[%lu].", msg->in.total_data_len);
		return ARPC_ERROR;
	}
	msg->usr_flags = flag;
	// alloc data buf form user define call back
	if (!IS_SET(msg->usr_flags, METHOD_ALLOC_DATA_BUF)) {
		ARPC_LOG_DEBUG("not need alloc data buf.");
		return ARPC_ERROR;
	}

	if (!ops->alloc_cb || !ops->free_cb) {
		CLR_FLAG(msg->usr_flags, METHOD_ALLOC_DATA_BUF);
		ARPC_LOG_DEBUG("func malloc or free is null.");
		return ARPC_ERROR;
	}

	// 分配内存
	nents = (msg->in.total_data_len / iov_max_len + 1);
	sglist = (struct xio_iovec* )ARPC_MEM_ALLOC(nents * sizeof(struct xio_iovec), NULL);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!msg->in.data_tbl.sglist), error, "calloc fail.");

	for (i = 0; i < nents -1; i++) {
		sglist[i].iov_len = iov_max_len;
		sglist[i].iov_base = ops->alloc_cb(sglist[i].iov_len, usr_ctx);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!sglist[i].iov_base), error_1, "calloc fail.");
	}
	sglist[i].iov_len = (msg->in.total_data_len % iov_max_len);
	sglist[i].iov_base = ops->alloc_cb(sglist[i].iov_len, usr_ctx);
	
	// 出参
	msg->in.data_tbl.sglist = (void*)sglist;
	vmsg_sglist_set_nents(&msg->in, nents);
	msg->in.sgl_type		= XIO_SGL_TYPE_IOV_PTR;
	return 0;

error_1:
	for (i = 0; i < nents; i++) {
		if (sglist[i].iov_base)
			ops->free_cb(sglist[i].iov_base, usr_ctx);
		sglist[i].iov_base =NULL;
	}
error:
	if (sglist) {
		ARPC_MEM_FREE(sglist, NULL);
		sglist = NULL;
	}
	CLR_FLAG(msg->usr_flags, METHOD_ALLOC_DATA_BUF);
	return -1;
}

int _clean_header_source(struct xio_msg *msg, mem_free_cb_t free_cb, void *usr_ctx)
{
	struct xio_iovec	*sglist;
	uint32_t			nents;
	uint32_t i;

	LOG_THEN_RETURN_VAL_IF_TRUE((!msg), ARPC_ERROR, "msg null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!free_cb), ARPC_ERROR, "alloc free is null.");

	if (!IS_SET(msg->usr_flags, METHOD_ALLOC_DATA_BUF)) {
		ARPC_LOG_DEBUG("not need free data buf.");
		return ARPC_ERROR;
	}
	// 释放内存
	nents = vmsg_sglist_nents(&msg->in);
	sglist = vmsg_base_sglist(&msg->in);
	if (!sglist || !nents){
		ARPC_LOG_ERROR("msg buf is null, nents:%u.", nents);
		return ARPC_ERROR;
	}

	if (!IS_SET(msg->usr_flags, METHOD_CALLER_HIJACK_RX_DATA)){
		for (i = 0; i < nents; i++) {
			if (sglist[i].iov_base)
				free_cb(sglist[i].iov_base, usr_ctx);
		}
	}
	
	if (sglist)
		ARPC_MEM_FREE(sglist, NULL);
	
	// 出参
	msg->in.data_tbl.sglist = NULL;
	vmsg_sglist_set_nents(&msg->in, 0);
	msg->in.sgl_type	= XIO_SGL_TYPE_IOV_PTR;
	CLR_FLAG(msg->usr_flags, METHOD_ALLOC_DATA_BUF);
	return 0;
}

int _do_respone(struct arpc_vmsg *rsp_iov, struct xio_msg  *req, rsp_cb_t release_rsp_cb, void *rsp_ctx)
{
	struct xio_msg  	*rsp_msg;
	struct _rsp_complete_ctx *rsp_com_ctx;
	uint32_t			i;

	rsp_msg = (struct xio_msg*)ARPC_MEM_ALLOC(sizeof(struct xio_msg), NULL); // todo queue
	memset(rsp_msg, 0, sizeof(struct xio_msg));

	rsp_com_ctx = (struct _rsp_complete_ctx*)ARPC_MEM_ALLOC(sizeof(struct _rsp_complete_ctx), NULL);
	memset(rsp_com_ctx, 0, sizeof(struct _rsp_complete_ctx));
	rsp_com_ctx->release_rsp_cb = release_rsp_cb;
	rsp_com_ctx->rsp_usr_ctx = rsp_ctx;
	
	if(rsp_iov && rsp_iov->head && rsp_iov->head_len){
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!release_rsp_cb, rsp_default, "release_rsp_cb is null ,can't send user rsp data.");
		rsp_com_ctx->rsp_iov = rsp_iov;
		rsp_msg->out.header.iov_base = rsp_iov->head;
		rsp_msg->out.header.iov_len  = rsp_iov->head_len;
		if (rsp_iov->vec && rsp_iov->vec_num) {
			ARPC_LOG_DEBUG("rsp  data.");
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
	}
rsp_default:
	rsp_msg->out.header.iov_base = NULL;
	rsp_msg->out.header.iov_len  = 0;
	rsp_msg->out.sgl_type = XIO_SGL_TYPE_IOV_PTR;
	vmsg_sglist_set_nents(&rsp_msg->out, 0);
	CLR_FLAG(rsp_msg->usr_flags, FLAG_RSP_USER_DATA);
rsp:	
	rsp_msg->request = req;
	rsp_msg->user_context = (void*)rsp_com_ctx;
	xio_send_response(rsp_msg);
	return 0;
}
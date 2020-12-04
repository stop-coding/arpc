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
#include <sys/sysinfo.h>

#include "arpc_com.h"
#include "threadpool.h"
#include "arpc_response.h"
#include "crc64.h"

static const char *version = "v1.0.0";
struct aprc_paramter{
	uint16_t is_init;
	uint16_t pad;
	tp_handle thread_pool;
	struct arpc_mutex mutex;
	struct aprc_option opt;
};

const char *arpc_version()
{
	return xio_version();
}

static struct aprc_paramter g_param= {
	.is_init        = 0,
	.thread_pool	= NULL,
	.mutex			= {.lock = PTHREAD_MUTEX_INITIALIZER},
	.opt			= {
						.thread_max_num = 12,
						.cpu_max_num    = 16,
						.msg_head_max_len = MAX_HEADER_DATA_LEN,
						.msg_data_max_len = DATA_DEFAULT_MAX_LEN,
						.msg_iov_max_len  = IOV_DEFAULT_MAX_LEN,
						.tx_queue_max_depth = 1024,
						.tx_queue_max_size  = (128*1024*1024),
						.rx_queue_max_depth = 1024,
						.rx_queue_max_size  = (128*1024*1024),
					}
	
};

static int unpack_msg_head(uint8_t *rx_head, uint32_t rx_head_len, struct arpc_msg_attr *attr, void **usr_head, uint32_t *usr_head_len);

static void set_user_option(const struct aprc_option *opt, struct aprc_option *out_opt)
{
	 out_opt->thread_max_num = (opt->thread_max_num && opt->thread_max_num < 256)?
	 							opt->thread_max_num:out_opt->thread_max_num;
	 out_opt->cpu_max_num = (opt->cpu_max_num && opt->cpu_max_num < 256)?
	 						opt->cpu_max_num:out_opt->cpu_max_num;
	 out_opt->msg_head_max_len = (opt->msg_head_max_len >= 128 && opt->msg_head_max_len <= 2048)?
	 							opt->msg_head_max_len:out_opt->msg_head_max_len;
	 out_opt->msg_data_max_len = (opt->msg_data_max_len >= 1024 && opt->msg_data_max_len <= (4*1024*1024))?
	 							opt->msg_data_max_len:out_opt->msg_data_max_len;
	 out_opt->msg_iov_max_len = (opt->msg_iov_max_len >= 1024 && opt->msg_iov_max_len <= (out_opt->msg_data_max_len))?
	 							opt->msg_iov_max_len:out_opt->msg_iov_max_len;
	 out_opt->tx_queue_max_depth = (opt->tx_queue_max_depth >= 128 && opt->tx_queue_max_depth <= (4*1024))?
	 							opt->tx_queue_max_depth:out_opt->tx_queue_max_depth;
	 out_opt->tx_queue_max_size = (opt->tx_queue_max_size >= (8*1024*1024) && opt->tx_queue_max_size <= (1024*1024*1024))?
	 							opt->tx_queue_max_size:out_opt->tx_queue_max_size;
	 out_opt->rx_queue_max_depth = (opt->rx_queue_max_depth >= 128 && opt->rx_queue_max_depth <= (4*1024))?
	 							opt->rx_queue_max_depth:out_opt->rx_queue_max_depth;
	 out_opt->rx_queue_max_size = (opt->rx_queue_max_size >= (8*1024*1024) && opt->rx_queue_max_size <= (1024*1024*1024))?
	 							opt->rx_queue_max_size:out_opt->rx_queue_max_size;
	 out_opt->control = opt->control;
}

const struct aprc_option *get_option()
{
	return &g_param.opt;
}
static void set_xio_option(const struct aprc_option *opt)
{
	uint32_t val = 0;
	// head len
	xio_set_opt(NULL,
		    XIO_OPTLEVEL_ACCELIO, XIO_OPTNAME_MAX_INLINE_XIO_HEADER,
		    &opt->msg_head_max_len, sizeof(uint32_t));
	// data len
	xio_set_opt(NULL,
		    XIO_OPTLEVEL_ACCELIO, XIO_OPTNAME_MAX_INLINE_XIO_DATA,
		    &opt->msg_data_max_len, sizeof(uint64_t));

	// tx queue len
	xio_set_opt(NULL,
		    XIO_OPTLEVEL_ACCELIO, XIO_OPTNAME_SND_QUEUE_DEPTH_MSGS,
		    &opt->tx_queue_max_depth, sizeof(uint32_t));
	
	// tx queue len
	xio_set_opt(NULL,
		    XIO_OPTLEVEL_ACCELIO, XIO_OPTNAME_SND_QUEUE_DEPTH_BYTES,
		    &opt->tx_queue_max_size, sizeof(uint64_t));

	// tx iov depth
	val = (opt->tx_queue_max_size/opt->msg_iov_max_len);
	val = (val)?val:4;
	xio_set_opt(NULL,
		    XIO_OPTLEVEL_ACCELIO, XIO_OPTNAME_MAX_OUT_IOVLEN,
		    &val, sizeof(uint32_t));

	// rx queue len
	xio_set_opt(NULL,
		    XIO_OPTLEVEL_ACCELIO, XIO_OPTNAME_RCV_QUEUE_DEPTH_MSGS,
		    &opt->rx_queue_max_depth, sizeof(uint32_t));
	
	// rx queue len
	xio_set_opt(NULL,
		    XIO_OPTLEVEL_ACCELIO, XIO_OPTNAME_RCV_QUEUE_DEPTH_BYTES,
		    &opt->rx_queue_max_size, sizeof(uint64_t));

	val = (opt->rx_queue_max_size/opt->msg_iov_max_len);
	val = (val)?val:4;
	xio_set_opt(NULL,
		    XIO_OPTLEVEL_ACCELIO, XIO_OPTNAME_MAX_IN_IOVLEN,
		    &val, sizeof(uint32_t));

}

static inline void arpc_crc_init();
static inline uint64_t arpc_cal_crc64(uint64_t crc, const unsigned char *data, uint64_t len);

int arpc_init_r(struct aprc_option *opt)
{
	struct tp_param p = {0};
	ARPC_LOG_DEBUG( "arpc_init.");
	arpc_mutex_lock(&g_param.mutex);
	if (g_param.is_init){
		ARPC_LOG_NOTICE( "arpc global init have done.");
		arpc_mutex_unlock(&g_param.mutex);
		return 0;
	}
	g_param.is_init = 1;
	g_param.opt.cpu_max_num = get_nprocs();
	if (opt) {
		set_user_option(opt, &g_param.opt);
	}
	g_param.thread_pool = NULL;
	arpc_mutex_unlock(&g_param.mutex);
	xio_init();
	set_xio_option(&g_param.opt);
	arpc_crc_init();
	
	return 0;
}
int arpc_init()
{
	//struct tp_param p = {0};
	ARPC_LOG_DEBUG( "arpc_init.");
	arpc_mutex_lock(&g_param.mutex);
	if (g_param.is_init){
		ARPC_LOG_NOTICE( "arpc global init have done.");
		arpc_mutex_unlock(&g_param.mutex);
		return 0;
	}
	g_param.is_init = 1;
	g_param.opt.cpu_max_num = get_nprocs();
	g_param.thread_pool = NULL;
	arpc_mutex_unlock(&g_param.mutex);
	xio_init();
	set_xio_option(&g_param.opt);
	arpc_crc_init();
	return 0;
}

void arpc_finish()
{
	int retry = 3;
	int ret;
	ARPC_LOG_DEBUG( "arpc_finish.");
	if (!g_param.is_init){
		ARPC_LOG_NOTICE( "arpc global init have not done.");
		return;
	}
	g_param.is_init = 0;
	xio_shutdown();
}

uint32_t arpc_thread_max_num()
{
	return g_param.opt.thread_max_num;
}

uint32_t arpc_cpu_max_num()
{
	return g_param.opt.cpu_max_num;
}

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
	ARPC_LOG_NOTICE("get uri:[%s].", uri);
	return 0;
}

int arpc_get_ipv4_addr(struct sockaddr_storage *src_addr, char *ip, uint32_t len, uint32_t *port)
{
	struct sockaddr_in *s4;
	LOG_THEN_RETURN_VAL_IF_TRUE((!src_addr || !ip || !port), ARPC_ERROR, "input null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((src_addr->ss_family != AF_INET || len < INET_ADDRSTRLEN), ARPC_ERROR, "input invalid.");

	s4 = (struct sockaddr_in *)src_addr;
	*port = s4->sin_port;
	inet_ntop(AF_INET, s4, ip, len);
	return ARPC_SUCCESS;
}

void debug_printf_msg(struct xio_msg *rsp)
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

int post_to_async_thread(struct arpc_thread_param *param)
{
	struct tp_thread_work thread;
	LOG_THEN_RETURN_VAL_IF_TRUE((!param), ARPC_ERROR, "param null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!param->loop), ARPC_ERROR, "loop null.");
	thread.loop = param->loop;
	thread.stop = NULL;
	thread.usr_ctx = (void*)param;
	if(!tp_post_one_work(param->threadpool, &thread, WORK_DONE_AUTO_FREE)){
		ARPC_LOG_ERROR( "tp_post_one_work error.");
		return -1;
	}
	return 0;
}

int create_xio_msg_usr_buf(struct xio_msg *msg, struct proc_header_func *ops, uint64_t iov_max_len, void *usr_ctx, struct arpc_msg_attr *attr)
{
	struct xio_iovec_ex	*sglist =NULL;
	uint32_t			nents = 0;
	struct arpc_header_msg header;
	uint32_t flag = 0;
	uint32_t i;
	int ret;
	uint64_t last_size;
	void *usr_addr = NULL;
	struct arpc_msg_attr proto = {0};

	msg->in.sgl_type = XIO_SGL_TYPE_IOV;
	LOG_THEN_RETURN_VAL_IF_TRUE((!msg->in.header.iov_base || !msg->in.header.iov_len), -1, "header null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!msg->in.header.iov_len), -1, "header len is 0.");

	msg->usr_flags = 0;
	memset(&header, 0, sizeof(struct arpc_header_msg));
	header.head_len = msg->in.header.iov_len;
	header.data_len = msg->in.total_data_len;
	ret = unpack_msg_head((uint8_t *)msg->in.header.iov_base, msg->in.header.iov_len, &proto, &usr_addr, &header.head_len);
	if (usr_addr && !ret){
		header.head = usr_addr;
		if(attr){
			*attr = proto;
		}
	}else{
		header.head = msg->in.header.iov_base;
		header.head_len = msg->in.header.iov_len;
	}
	if(ops->proc_head_cb){
		ret = ops->proc_head_cb(&header, usr_ctx, &flag);
		if (ret != ARPC_SUCCESS){
			ARPC_LOG_DEBUG("discard data, total_data_len[%lu].", msg->in.total_data_len);
			return ARPC_ERROR;
		}
	}
	msg->usr_flags = flag;
	ARPC_LOG_DEBUG("flag:0x%x, data len:%lu", flag, msg->in.total_data_len);
	if (!msg->in.total_data_len){
		CLR_FLAG(msg->usr_flags, METHOD_ALLOC_DATA_BUF);
		ARPC_LOG_DEBUG("discard data, total_data_len[%lu].", msg->in.total_data_len);
		return ARPC_ERROR;
	}
	// alloc data buf form user define call back
	/*if (!IS_SET(msg->usr_flags, METHOD_ALLOC_DATA_BUF)) {
		ARPC_LOG_ERROR("not need alloc data buf.");
		return ARPC_ERROR;
	}*/
	if (!ops->alloc_cb || !ops->free_cb) {
		CLR_FLAG(msg->usr_flags, METHOD_ALLOC_DATA_BUF);
		ARPC_LOG_ERROR("func malloc or free is null.");
		return ARPC_ERROR;
	}
	iov_max_len = (proto.iovec_num > 0)?(msg->in.total_data_len/proto.iovec_num):iov_max_len;//根据请求消息设置vec长度
	// 分配内存
	last_size = msg->in.total_data_len%iov_max_len;
	nents = (last_size)? 1: 0;
	nents += (msg->in.total_data_len / iov_max_len);
	sglist = (struct xio_iovec_ex* )arpc_mem_alloc(nents * sizeof(struct xio_iovec_ex), NULL);
	last_size = (last_size)? last_size :iov_max_len;

	ARPC_LOG_DEBUG("get msg, nent:%u, iov_max_len:%lu, total_size:%lu, sglist:%p", nents, iov_max_len, msg->in.total_data_len, sglist);
	for (i = 0; i < nents - 1; i++) {
		sglist[i].iov_len = iov_max_len;
		sglist[i].iov_base = ops->alloc_cb(iov_max_len, usr_ctx);
		sglist[i].mr = NULL;
		sglist[i].user_context = NULL;
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!sglist[i].iov_base), error_1, "calloc fail.");
	}
	sglist[i].iov_len = last_size;
	sglist[i].iov_base = ops->alloc_cb(iov_max_len, usr_ctx);
	sglist[i].mr = NULL;
	sglist[i].user_context = NULL;
	ARPC_LOG_DEBUG("i:%u ,data:%p, len:%lu.",i, sglist[i].iov_base, sglist[i].iov_len);
	// 出参
	msg->in.pdata_iov.sglist = (void*)sglist;
	vmsg_sglist_set_nents(&msg->in, nents);
	msg->in.sgl_type		= XIO_SGL_TYPE_IOV_PTR;
	SET_FLAG(msg->usr_flags, METHOD_ALLOC_DATA_BUF);
	return 0;

error_1:
	for (i = 0; i < nents; i++) {
		if (sglist[i].iov_base)
			ops->free_cb(sglist[i].iov_base, usr_ctx);
		sglist[i].iov_base =NULL;
	}
	if (sglist) {
		arpc_mem_free(sglist, NULL);
		sglist = NULL;
	}
	msg->in.sgl_type = XIO_SGL_TYPE_IOV;
	msg->in.total_data_len = 0;
	CLR_FLAG(msg->usr_flags, METHOD_ALLOC_DATA_BUF);
	return -1;
}

int destroy_xio_msg_usr_buf(struct xio_msg *msg, mem_free_cb_t free_cb, void *usr_ctx)
{
	uint32_t i;
	uint32_t nents;
	struct xio_iovec_ex	*sglist =NULL;

	LOG_THEN_RETURN_VAL_IF_TRUE((!msg), ARPC_ERROR, "msg null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!free_cb), ARPC_ERROR, "alloc free is null.");

	if (IS_SET(msg->usr_flags, METHOD_ALLOC_DATA_BUF)) {
		nents = vmsg_sglist_nents(&msg->in);
		sglist = vmsg_sglist(&msg->in);
		for (i = 0; i < nents; i++) {
			if (sglist[i].iov_base)
				free_cb(sglist[i].iov_base, usr_ctx);
			sglist[i].iov_base = NULL;
			sglist[i].iov_len = 0;
		}
		if (sglist){
			arpc_mem_free(sglist, NULL);
		}
		vmsg_sglist_set_nents(&msg->in, 0);
		msg->in.pdata_iov.sglist = NULL;
		msg->in.total_data_len = 0;
		msg->in.sgl_type = XIO_SGL_TYPE_IOV;
	}
	
	return 0;
}

static int unpack_msg_head(uint8_t *rx_head, uint32_t rx_head_len, struct arpc_msg_attr *attr, void **usr_head, uint32_t *usr_head_len)
{
	void *req_addr;
	uint8_t *ptr;
	uint64_t req_len = 0;
	arpc_proto_t tlv_type = 0;
	int32_t index = 0;
	int32_t ret;

	if (rx_head_len < 2 * sizeof(struct arpc_tlv) + sizeof(struct arpc_msg_attr)) {
		return -1;
	}
	index = arpc_read_tlv(&tlv_type, &req_len, &req_addr, rx_head);
	if(index < 0 || tlv_type != ARPC_PROTO_MSG_INTER_HEAD){
		return -1;
	}
	ret = unpack_msg_attr(req_addr, req_len, attr);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret < 0, ARPC_ERROR, "unpack_msg_attr fail.");
	req_addr = NULL;
	req_len = 0;

	index = arpc_read_tlv(&tlv_type, &req_len, &req_addr, rx_head + index);
	if(index < 0 || tlv_type != ARPC_PROTO_MSG_USER_HEAD || req_len > 64*1024 || !req_addr){
		return -1;
	}
	*usr_head_len = req_len;
	*usr_head = req_addr;
	return 0;
}

static void hexdump(char *addr, uint64_t len)
{
	int i;
	fprintf(stderr,"\n-------------------len:%lu------------------------\n", len);
	for (i = 0; i < len; i++) {
		fprintf(stderr,"0x%02x ", addr[i]);
	}
	fprintf(stderr,"\n-------------------------------------------\n");
	return;
}

int move_msg_xio2arpc(struct xio_vmsg *xio_msg, struct arpc_vmsg *msg, struct arpc_msg_attr *attr)
{
	struct xio_iovec_ex  *sglist = NULL;
	uint32_t			nents = 0;
	uint32_t 			i;
	int 				ret;
	uint64_t 			crc = 0;
	void 				*usr_addr;
	struct arpc_msg_attr proto = {0};
	
	LOG_THEN_RETURN_VAL_IF_TRUE((!xio_msg), ARPC_ERROR, "xio_msg null, exit.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!msg), ARPC_ERROR, "msg null, exit.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!xio_msg->header.iov_base), ARPC_ERROR, "head null, exit.");
	ret = unpack_msg_head((uint8_t *)xio_msg->header.iov_base, xio_msg->header.iov_len, &proto, &usr_addr, &msg->head_len);
	if (ret){
		msg->head_len = xio_msg->header.iov_len;
		usr_addr = xio_msg->header.iov_base;
	}

	if (msg->head_len) {
		msg->head = arpc_mem_alloc(msg->head_len, NULL);
		memcpy(msg->head, usr_addr, msg->head_len);
		crc = arpc_cal_crc64(crc, (const unsigned char *)msg->head, msg->head_len);
	}
	nents = vmsg_sglist_nents(xio_msg);
	msg->total_data = 0;
	msg->vec =NULL;
	msg->vec_num = 0;
	if (nents && (xio_msg->sgl_type == XIO_SGL_TYPE_IOV_PTR)){
		sglist = vmsg_sglist(xio_msg);
		msg->vec = (struct arpc_iov *)arpc_mem_alloc(nents * sizeof(struct arpc_iov), NULL);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!msg->vec), fail_out,"vec alloc is empty.");
		for(i = 0; i < nents; i++){
			msg->vec[i].data = sglist[i].iov_base;
			msg->vec[i].len	= sglist[i].iov_len;
			msg->total_data +=msg->vec[i].len;
			crc = arpc_cal_crc64(crc, (const unsigned char *)msg->vec[i].data, msg->vec[i].len);
			sglist[i].iov_base = NULL;
			sglist[i].iov_len = 0; 
			ARPC_ASSERT(msg->vec[i].data, "vec[%u].data null, but nents:%u", i, nents);
			ARPC_ASSERT(msg->vec[i].len, "vec[%u].len 0, but nents:%u", i, nents);
		}
		msg->vec_num = nents;
		vmsg_sglist_set_nents(xio_msg, 0);
		msg->vec_type = ARPC_VEC_TYPE_PRT;
	}else{
		if (!msg->head_len) {
			ARPC_LOG_ERROR("no header and data in msg.");
		}
		msg->vec_type = ARPC_VEC_TYPE_NONE;
	}
	if (attr){
		*attr = proto;
	}
	ARPC_LOG_DEBUG("rx crc:0x%lx, cal crc:0x%lx", proto.req_crc, crc);
	if (crc && proto.req_crc){
		// 确保两边都开启crc才有意义,0 默认不开启
		ARPC_ASSERT(proto.req_crc == crc, "crc check fail, rx crc:0x%lx, but cal crc:0x%lx.",
											proto.req_crc,
											crc);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE((proto.req_crc != crc), fail_out, 
									"crc check fail, rx crc:0x%lx, but cal crc:0x%lx.", 
									proto.req_crc, 
									crc);
	}
	return 0;
fail_out:
	SAFE_FREE_MEM(msg->head);
	msg->head_len = 0;
	SAFE_FREE_MEM(msg->vec);
	msg->vec_num = 0;
	msg->total_data = 0;
	return 0;
}

void free_msg_xio2arpc(struct arpc_vmsg *msg, mem_free_cb_t free_cb, void *usr_ctx)
{
	int i;
	LOG_THEN_RETURN_IF_VAL_TRUE((!msg), "msg null, exit.");
	LOG_THEN_RETURN_IF_VAL_TRUE((!free_cb), "free_cb null, exit.");

	if (msg->vec_type == ARPC_VEC_TYPE_PRT && msg->vec){
		for(i = 0; i < msg->vec_num; i++){
			if (msg->vec[i].data){
				free_cb(msg->vec[i].data, usr_ctx);
				msg->vec[i].data = NULL;
			}
			msg->vec[i].len = 0;
		}
		SAFE_FREE_MEM(msg->vec);
		msg->vec_num =0;
		msg->total_data =0;
		msg->vec_type = ARPC_VEC_TYPE_NONE;
	}
	SAFE_FREE_MEM(msg->head);
	return;
}

int convert_msg_arpc2xio(const struct arpc_vmsg *usr_msg, struct xio_vmsg *xio_msg, struct arpc_msg_attr *attr)
{
	uint32_t i;
	uint32_t index = 0;
	uint8_t  *ptr;
	uint64_t crc = 0;
	struct arpc_msg_attr proto = {0};
	void *read_add = NULL;
	uint32_t read_len = 0;

	LOG_THEN_RETURN_VAL_IF_TRUE((!attr), ARPC_ERROR, "attr null, exit.");
	/* header */
	crc = arpc_cal_crc64(crc, (const unsigned char *)usr_msg->head, usr_msg->head_len);
	xio_msg->pdata_iov.max_nents = usr_msg->vec_num;
	xio_msg->pdata_iov.nents = usr_msg->vec_num;
	xio_msg->total_data_len = 0;
	if (xio_msg->pdata_iov.nents) {
		xio_msg->pdata_iov.sglist = (struct xio_iovec_ex *)arpc_mem_alloc((xio_msg->pdata_iov.nents) * sizeof(struct xio_iovec_ex), NULL);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!xio_msg->pdata_iov.sglist, data_null, "arpc_mem_alloc fail.");
		memset(xio_msg->pdata_iov.sglist, 0, xio_msg->pdata_iov.nents * sizeof(struct xio_iovec_ex));
		xio_msg->pad = 1;
		for (i = 0; i < xio_msg->pdata_iov.nents; i++){
			ARPC_ASSERT(usr_msg->vec[i].data, "data is null.");
			ARPC_ASSERT(usr_msg->vec[i].len, "data len is 0.");
			xio_msg->pdata_iov.sglist[i].iov_base = usr_msg->vec[i].data;
			xio_msg->pdata_iov.sglist[i].iov_len = usr_msg->vec[i].len;
			crc = arpc_cal_crc64(crc, (const unsigned char *)usr_msg->vec[i].data, usr_msg->vec[i].len);
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

	attr->req_crc = crc;
	attr->iovec_num = usr_msg->vec_num;
	xio_msg->header.iov_len = 2 * sizeof(struct arpc_tlv) + sizeof(struct arpc_msg_attr) + usr_msg->head_len;
	xio_msg->header.iov_base = arpc_mem_alloc(xio_msg->header.iov_len, NULL);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!xio_msg->header.iov_base, data_null, "arpc_mem_alloc head buf fail.");

	ptr = xio_msg->header.iov_base;
	index = arpc_write_tlv(ARPC_PROTO_MSG_INTER_HEAD, sizeof(struct arpc_msg_attr), ptr);
	ptr += sizeof(struct arpc_tlv);
	pack_msg_attr(attr, ptr, sizeof(struct arpc_msg_attr));

	ptr = (uint8_t*)xio_msg->header.iov_base + index;
	index = arpc_write_tlv(ARPC_PROTO_MSG_USER_HEAD, usr_msg->head_len, ptr);
	ptr += sizeof(struct arpc_tlv);
	memcpy(ptr, usr_msg->head, usr_msg->head_len);
	ARPC_LOG_DEBUG("send crc:0x%lx", crc);
	return 0;
data_null:
	xio_msg->header.iov_base = 0;
	xio_msg->header.iov_len = 0;
	xio_msg->pdata_iov.max_nents =0;
	xio_msg->pdata_iov.nents = 0;
	xio_msg->pdata_iov.sglist = NULL;
	return -1;
}

void free_msg_arpc2xio(struct xio_vmsg *xio_msg)
{
	struct xio_msg 	*req = NULL;
	if ((xio_msg->pad) && xio_msg->pdata_iov.sglist){
		SAFE_FREE_MEM(xio_msg->pdata_iov.sglist);
		vmsg_sglist_set_nents(xio_msg, 0);
		xio_msg->pad = 0;
	}
	SAFE_FREE_MEM(xio_msg->header.iov_base);
	xio_msg->header.iov_len = 0;
	return;
}

const char *arpc_uri_get_resource_ptr(const char *uri)
{
	const char *start;
	const char *p1, *p2 = NULL;

	start = strstr(uri, "://");
	if (!start)
		return NULL;
	return start + 3;
}

const char *arpc_uri_get_port_ptr(const char *uri)
{
	const char *start;

	start = arpc_uri_get_resource_ptr(uri);
	if (!start)
		return NULL;
	start = strstr(start, ":");
	return start + 1;
}


int arpc_uri_get_portal(const char *uri, char *portal, int portal_len)
{
	const char *res = arpc_uri_get_port_ptr(uri);
	int len = (res) ? strlen(res) : 0;

	if (len < portal_len && len > 0) {
		strncpy(portal, res, len);
		portal[len] = 0;
		return 0;
	}

	return -1;
}

/*---------------------------------------------------------------------------*/
/* arpc_uri_get_resource							     */
/*---------------------------------------------------------------------------*/
int arpc_uri_get_resource(const char *uri, char *resource, int resource_len)
{
	const char *res ;
	const char *port;
	int  len, port_len;

	res = arpc_uri_get_resource_ptr(uri);
	if (res) {
		int  len = strlen(res);
		port = arpc_uri_get_port_ptr(uri);
		if (port) {
			port_len = strlen(port);
			if (port_len) {
				len -= (port_len + 1);
			}
		}
		if (len < resource_len) {
			strncpy(resource, res, len);
			resource[len] = 0;
			return 0;
		}
	}
	return -1;
}

int arpc_uri_get_proto(const char *uri, char *proto, int proto_len)
{
	char *start = (char *)uri;
	const char *end;
	char *p;
	int  i;

	end = strstr(uri, "://");
	if (!end)
		return -1;

	p = start;
	for (i = 0; i < proto_len; i++) {
		if (p == end) {
			proto[i] = 0;
			return 0;
		}
		proto[i] = *p;
		p++;
	}

	return -1;
}

static inline void arpc_crc_init()
{
	if(IS_SET(g_param.opt.control, ARPC_E_CTRL_CRC)){
		crc64_init();
	}
}

static inline uint64_t arpc_cal_crc64(uint64_t crc, const unsigned char *data, uint64_t len)
{
	if(IS_SET(g_param.opt.control, ARPC_E_CTRL_CRC)){
		return crc64(crc, data, len);
	}
	return 0;
}


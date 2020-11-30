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
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <mcheck.h>

#include "arpc_com.h"
#include "arpc_api.h"
#include "base_log.h"
#include "sha256.h"

static char rsp_header[] = "<file:rsp>";
static char rsp_iov_data[] = "<iov:rsp>.....................";
static char buf_str[] = "test body data.....";
#define SERVER_LOG(format, arg...) //fprintf(stderr, "[ SERVER ]"format"\n",##arg)

void conver_hex_to_str(BYTE *hex, int hex_len, BYTE *str, int str_len)
{
	int i;
	if(hex_len*2 >= str_len){
		SERVER_LOG("buf invalid.");
		return;
	}
	for(i = 0; i < hex_len; i++){
		snprintf((char*)(str + i*2),  str_len, "%02x", hex[i]);
	}
	str[str_len -1] = '\0';
	return;
}

static void *mem_alloc(uint32_t size, void *usr_context)
{
	void *mem = malloc(size);
	return mem;
}
static int mem_free(void *buf_ptr, void *usr_context)
{
	if(buf_ptr)
		free(buf_ptr);
	return 0;
}

static int process_rx_header(struct arpc_header_msg *header, void* usr_context, uint32_t *flag)
{
	SET_METHOD(*flag, METHOD_ALLOC_DATA_BUF);// vec数据释放
	return 0;
}

static int process_rx_data(const struct arpc_vmsg *req_iov, struct arpc_rsp *rsp, void *usr_context)
{
	uint32_t i;
	struct arpc_vmsg *rsp_data;

	if (!req_iov){
		SERVER_LOG("null inputn");
		return 0;
	}

	SERVER_LOG("---sync---data sha256:%s, receive len:%lu.\n", (char*)req_iov->head, req_iov->total_data);

	rsp_data = mem_alloc(sizeof(struct arpc_vmsg), NULL);
	memset(rsp_data, 0, sizeof(struct arpc_vmsg));

	rsp_data->head_len = req_iov->head_len;
	rsp_data->head = mem_alloc(rsp_data->head_len, NULL);
	memcpy(rsp_data->head, req_iov->head, req_iov->head_len);

	rsp_data->vec_num = 1;
	rsp_data->vec = mem_alloc(rsp_data->vec_num * sizeof(struct arpc_iov), NULL);
	for(i = 0; i < rsp_data->vec_num; i++){
		rsp_data->vec[i].data = mem_alloc(sizeof(buf_str), NULL);
		memcpy(rsp_data->vec[i].data, buf_str, sizeof(buf_str));
		rsp_data->vec[i].len = sizeof(buf_str);
		rsp_data->total_data +=rsp_data->vec[i].len;
	}
	rsp->rsp_iov = rsp_data;
	rsp->flags = 0;
	return 0;
}
static int release_rsp(struct arpc_vmsg *rsp_iov, void *usr_context)
{
	int i;
	if(!rsp_iov){
		SERVER_LOG("null rsp fail.\n");
		return -1;
	}
	if (rsp_iov->head) {
		mem_free(rsp_iov->head, NULL);
		rsp_iov->head =NULL;
	}

	for(i = 0; i < rsp_iov->vec_num; i++){
		if(rsp_iov->vec[i].data){
			mem_free(rsp_iov->vec[i].data, NULL);
		}
	}
	if(rsp_iov->vec){
		mem_free(rsp_iov->vec, NULL);
	}
	if(rsp_iov){
		mem_free(rsp_iov, NULL);
	}
	return 0;
}

int new_session_start(const struct arpc_new_session_req *client, struct arpc_new_session_rsp *param, void* usr_context)
{
	
	return 0;
}

static arpc_session_handle_t g_session_fd = NULL;
int new_session_end(arpc_session_handle_t fd, struct arpc_new_session_rsp *param, void* usr_context)
{
	g_session_fd = fd;
	return 0;
}


static int process_async(const struct arpc_vmsg *req_iov, struct arpc_rsp *rsp, void* usr_context)
{
	char file_path[512] = {0};
	uint32_t i;
	struct arpc_vmsg *rsp_data;
	BYTE buf[SHA256_BLOCK_SIZE];
	BYTE buf_str[SHA256_BLOCK_SIZE*2 +1];
	SHA256_CTX ctx;
	if (!req_iov){
		SERVER_LOG("null inputn");
		return 0;
	}

	SERVER_LOG("--async--data sha256:%s, receive len:%lu, vec_num:%u.\n", (char*)req_iov->head, req_iov->total_data, req_iov->vec_num);

	rsp_data = mem_alloc(sizeof(struct arpc_vmsg), NULL);
	memset(rsp_data, 0, sizeof(struct arpc_vmsg));

	rsp_data->head_len = req_iov->head_len;
	rsp_data->head = mem_alloc(rsp_data->head_len, NULL);
	memcpy(rsp_data->head, req_iov->head, req_iov->head_len);

	rsp_data->vec_num = 1;
	rsp_data->vec = mem_alloc(rsp_data->vec_num * sizeof(struct arpc_iov), NULL);
	for(i = 0; i < rsp_data->vec_num; i++){
		rsp_data->vec[i].data = mem_alloc(sizeof(buf_str), NULL);
		memcpy(rsp_data->vec[i].data, buf_str, sizeof(buf_str));
		rsp_data->vec[i].len = sizeof(buf_str);
		rsp_data->total_data +=rsp_data->vec[i].len;
	}
	rsp->rsp_iov = rsp_data;
	rsp->flags = 0;
	//usleep(1000*((uint64_t)rsp_data %100));
	return 0;
}
static int process_cli_oneway_data(const struct arpc_vmsg *req_iov, uint32_t *flags, void *usr_context)
{
	uint32_t i;
	int ret;
	struct arpc_vmsg *rsp_data;
	arpc_session_handle_t server_seesion;
	if (!req_iov){
		SERVER_LOG("null inputn");
		return 0;
	}
	//arpc_usleep(1000);
	server_seesion = g_session_fd;
	SERVER_LOG("client-----head:%s, receive len:%lu.\n", (char*)req_iov->head, req_iov->total_data);
	if (!server_seesion) {
		SERVER_LOG("server_seesion null inputn");
		return 0;
	}
	rsp_data = mem_alloc(sizeof(struct arpc_vmsg), NULL);
	memset(rsp_data, 0, sizeof(struct arpc_vmsg));

	rsp_data->head_len = req_iov->head_len;
	rsp_data->head = mem_alloc(rsp_data->head_len, NULL);
	memcpy(rsp_data->head, req_iov->head, req_iov->head_len);

	rsp_data->vec_num = 1;
	rsp_data->vec = mem_alloc(rsp_data->vec_num * sizeof(struct arpc_iov), NULL);
	for(i = 0; i < rsp_data->vec_num; i++){
		rsp_data->vec[i].data = mem_alloc(sizeof(buf_str), NULL);
		memcpy(rsp_data->vec[i].data, buf_str, sizeof(buf_str));
		rsp_data->vec[i].len = sizeof(buf_str);
		rsp_data->total_data +=rsp_data->vec[i].len;
	}
	SERVER_LOG("arpc_send_oneway_msg:%s.", (char*)buf_str);
	ret = arpc_send_oneway_msg(server_seesion, rsp_data, release_rsp, NULL);
	if(ret){
		SERVER_LOG("arpc_send_oneway_msg:%s fail.", (char*)buf_str);
		release_rsp(rsp_data, NULL);
	}
	return 0;
}

static int process_oneway_async(const struct arpc_vmsg *req_iov, uint32_t *flags, void* usr_context)
{
	uint32_t i;
	int ret;
	static int32_t cnt = 0;
	struct arpc_vmsg *rsp_data;
	arpc_session_handle_t cli_seesion;
	struct arpc_msg *request =NULL;
	if (!req_iov){
		SERVER_LOG("null inputn");
		return 0;
	}

	SERVER_LOG("server-----head:%s, receive len:%lu.\n", (char*)req_iov->head, req_iov->total_data);
	if (usr_context) {
		cli_seesion = usr_context;
	}else{
		cli_seesion = g_session_fd;
	}

	rsp_data = mem_alloc(sizeof(struct arpc_vmsg), NULL);
	memset(rsp_data, 0, sizeof(struct arpc_vmsg));

	rsp_data->head_len = req_iov->head_len;
	rsp_data->head = mem_alloc(rsp_data->head_len, NULL);
	memcpy(rsp_data->head, req_iov->head, req_iov->head_len);

	rsp_data->vec_num = 1;
	rsp_data->vec = mem_alloc(rsp_data->vec_num * sizeof(struct arpc_iov), NULL);
	for(i = 0; i < rsp_data->vec_num; i++){
		rsp_data->vec[i].data = mem_alloc(sizeof(buf_str), NULL);
		memcpy(rsp_data->vec[i].data, buf_str, sizeof(buf_str));
		rsp_data->vec[i].len = sizeof(buf_str);
		rsp_data->total_data +=rsp_data->vec[i].len;
	}
	SERVER_LOG("arpc_send_oneway_msg:%s.", (char*)buf_str);
	ret = arpc_send_oneway_msg(cli_seesion, rsp_data, release_rsp, NULL);
	if(ret){
		SERVER_LOG("arpc_send_oneway_msg:%s fail.", (char*)buf_str);
		release_rsp(rsp_data, NULL);
	}

	if (usr_context) {
		request = arpc_new_msg(NULL);
		request->send.head_len = req_iov->head_len;
		request->send.head = mem_alloc(request->send.head_len, NULL);
		memcpy(request->send.head, req_iov->head, req_iov->head_len);
		request->send.vec_num = 1;
		request->send.vec  = mem_alloc(rsp_data->vec_num * sizeof(struct arpc_iov), NULL);
		request->send.vec[0].data = mem_alloc(sizeof(buf_str), NULL);
		request->send.vec[0].len = sizeof(buf_str);
		request->send.total_data =sizeof(buf_str);
		ret = arpc_do_request(cli_seesion, request, 30*1000);
		if (ret){
			printf("arpc_do_request fail\n");
		}
		mem_free(request->send.head, NULL);
		mem_free(request->send.vec[0].data, NULL);
		mem_free(request->send.vec, NULL);
	}
	return 0;
}
static struct arpc_session_ops server_ops ={
	.req_ops = {
		.alloc_cb = &mem_alloc,
		.free_cb = &mem_free,
		.proc_head_cb = &process_rx_header,
		.proc_data_cb = &process_async,
		.proc_async_cb = &process_async,
		.release_rsp_cb = &release_rsp,
	},
	.oneway_ops = {
		.alloc_cb = &mem_alloc,
		.free_cb = &mem_free,
		.proc_head_cb = &process_rx_header,
		.proc_data_cb = &process_oneway_async,
		.proc_async_cb = &process_oneway_async,
	}
};

static struct arpc_session_ops cli_ops ={
	.req_ops = {
		.alloc_cb = &mem_alloc,
		.free_cb = &mem_free,
		.proc_head_cb = &process_rx_header,
		.proc_data_cb = &process_rx_data,
		.proc_async_cb = &process_async,
		.release_rsp_cb = &release_rsp,
	},
	.oneway_ops = {
		.alloc_cb = &mem_alloc,
		.free_cb = &mem_free,
		.proc_head_cb = &process_rx_header,
		.proc_data_cb = &process_cli_oneway_data,
		.proc_async_cb = &process_cli_oneway_data,
	}
};

/*---------------------------------------------------------------------------*/
/* main									     */
/*---------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
	struct arpc_server_param param;
	struct arpc_client_session_param cli_param;
	arpc_session_handle_t target_fd = NULL;
	arpc_session_handle_t cli_fd = NULL;
	arpc_server_t server_fd = NULL;
	char file_name[256] = "file_rx";
	struct aprc_option opt = {0};
	
	SERVER_LOG("null inputn------------------");
	if (argc < 2) {
		SERVER_LOG("Usage: %s <server port> <host> <port>n", argv[0]);
		return 0;
	}
	//SET_FLAG(opt.control, ARPC_E_CTRL_CRC); //开启通信CRC检查
	opt.thread_max_num = 32;
	arpc_init_r(&opt);
	if (argc > 2) {
		memset(&cli_param, 0, sizeof(cli_param));
		cli_param.con.type = ARPC_E_TRANS_TCP;
		memcpy(cli_param.con.ipv4.ip, argv[2], IPV4_MAX_LEN);
		cli_param.con.ipv4.port = atoi(argv[3]);
		cli_param.req_data = NULL;
		cli_param.req_data_len = 0;
		cli_param.con_num = 4;
		cli_param.rx_con_num = 0;
		cli_param.ops = &cli_ops;
		cli_param.ops_usr_ctx = NULL;
		cli_fd = arpc_client_create_session(&cli_param);
		if (!cli_fd){
			printf("arpc_client_create_session fail\n");
			goto end;
		}
		target_fd = cli_fd;
	}
	

	memset(&param, 0, sizeof(param));
	param.con.type = ARPC_E_TRANS_TCP;
	memcpy(param.con.ipv4.ip, "0.0.0.0", sizeof("0.0.0.0"));
	param.con.ipv4.port = atoi(argv[1]);

	param.work_num = 6;
	param.default_ops = server_ops;
	param.new_session_start = &new_session_start;
	param.new_session_end = &new_session_end;
	param.default_ops_usr_ctx = target_fd;

	server_fd = arpc_server_create(&param);
	if(!server_fd){
		goto end;
	}
	arpc_server_loop(server_fd, -1);

end:
	if(cli_fd)
		arpc_client_destroy_session(&cli_fd);
	if(server_fd)
		arpc_server_destroy(&server_fd);
	arpc_finish();
	return 0;
}


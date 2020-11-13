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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <mcheck.h>
#include <pthread.h>

#include "arpc_api.h"
#include "sha256.h"
#include "base_log.h"
#include "arpc_com.h"

#define BUF_MAX_SIZE IOV_DEFAULT_MAX_LEN

static struct arpc_cond 		g_cond;
static BYTE g_buf_str[SHA256_BLOCK_SIZE*2 +1] = {0};

#define CLIENT_LOG(format, arg...) fprintf(stderr, "[ SERVER ]"format"\n",##arg)

void conver_hex_to_str(BYTE *hex, int hex_len, BYTE *str, int str_len)
{
	int i;
	if(hex_len*2 >= str_len){
		CLIENT_LOG("buf invalid.");
		return;
	}
	for(i = 0; i < hex_len; i++){
		snprintf((char*)(str + i*2),  str_len, "%02x", hex[i]);
	}
	str[str_len -1] = '\0';
	return;
}

static char rsp_header[] = "<file:rsp>";
static char rsp_iov_data[] = "<iov:rsp>.....................";

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
	return 0;
}
static int release_rsp(struct arpc_vmsg *rsp_iov, void *usr_context)
{
	return 0;
}

int new_session_start(const struct arpc_new_session_req *client, struct arpc_new_session_rsp *param, void* usr_context)
{
	char file_path[512] = {0};
	FILE *fp = NULL;
	if (client && client->client_data.data){
		memcpy(usr_context, client->client_data.data, client->client_data.len);
		sprintf(file_path, "./rev_%s", (char *)usr_context);
		fp = fopen(file_path, "w");
		if (!fp){
			CLIENT_LOG("fopen path:%s fail.\n", file_path);
			return 0;
		}
		fclose(fp);
	}
	return 0;
}

static arpc_session_handle_t session_fd = NULL;
int new_session_end(arpc_session_handle_t fd, struct arpc_new_session_rsp *param, void* usr_context)
{
	session_fd = fd;
	return 0;
}


static int process_async(const struct arpc_vmsg *req_iov, struct arpc_rsp *rsp, void* usr_context)
{
	return 0;
}
static int process_rx_oneway_data(const struct arpc_vmsg *req_iov, uint32_t *flags, void *usr_context)
{
	return 0;
}


static int process_oneway_async(const struct arpc_vmsg *req_iov, uint32_t *flags, void* usr_context)
{
	char file_path[512] = {0};
	uint32_t i;
	int ret;
	struct arpc_vmsg *rsp_data;
	BYTE buf[SHA256_BLOCK_SIZE];
	SHA256_CTX ctx;

	if (!req_iov){
		CLIENT_LOG("null inputn");
		return 0;
	}
	CLIENT_LOG("oneway async------head len:%u.\n", req_iov->head_len);
	CLIENT_LOG("rx sha256:%s.", (char *)req_iov->head);
	arpc_cond_lock(&g_cond);
	arpc_cond_notify(&g_cond);
	arpc_cond_unlock(&g_cond);

	return 0;
}

static struct arpc_session_ops ops ={
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
		.proc_data_cb = &process_rx_oneway_data,
		.proc_async_cb = &process_oneway_async,
	}
};

static const char *g_filepath = NULL;
static uint64_t g_file_size = 128*1024;
int session_send_msg(arpc_session_handle_t session_fd, int32_t loop_time)
{
	uint32_t				i = 0;
	int 					ret = 0;
	uint64_t				offset = 0,send_len = 0, file_len =0;
	struct arpc_msg *request =NULL;
	struct arpc_msg_param p;
	const char *file_path = NULL;
	FILE *fp = NULL;
	BYTE buf[SHA256_BLOCK_SIZE];

	char *buf_str = "hongchunhuatestett";
	SHA256_CTX ctx;
	struct timeval start_now;
	struct timeval end_now;

	file_len = g_file_size;
	request = arpc_new_msg(NULL);
	offset = 0;
	send_len = 0;
	while(offset < file_len){
		send_len = ((file_len - offset) > DATA_DEFAULT_MAX_LEN)? DATA_DEFAULT_MAX_LEN: (file_len - offset);
		request->send.total_data = send_len;
		request->send.vec_num = (request->send.total_data % BUF_MAX_SIZE)?1:0;
		request->send.vec_num += (request->send.total_data / BUF_MAX_SIZE);
		request->proc_rsp_cb = NULL;

		// 读取文件
		request->send.vec = malloc(request->send.vec_num * sizeof(struct arpc_iov));
		for (i = 0; i  < request->send.vec_num -1; i++) {
			request->send.vec[i].data = malloc(BUF_MAX_SIZE);
			request->send.vec[i].len = BUF_MAX_SIZE;
			if (request->send.vec[i].len < BUF_MAX_SIZE){
				if(feof(fp)){
					break;
				}
			}
		}
		offset += send_len;
		send_len = (send_len % BUF_MAX_SIZE);
		send_len = (send_len)?send_len:BUF_MAX_SIZE;
		request->send.vec[i].data = malloc(send_len);
		request->send.vec[i].len = send_len;
		if (request->send.vec[i].len < send_len){
			printf("fread len fail\n");
		}

		request->send.head_len = strlen((char*)buf_str) + 1;
		request->send.head = (char *)buf_str;

		while(loop_time--) {
			ret = arpc_send_oneway_msg(session_fd, &request->send, NULL, NULL);
			if (ret != 0){
				printf("arpc_send_oneway_msg fail\n");
			}
		}
		free(request->send.vec);
		request->send.vec = NULL;
		break;
	}
	arpc_delete_msg(&request);
	if(fp)
		fclose(fp);
	return 0;
}

#define MAX_THREADS 	4
#define MAX_LOOP_TIMES 	500000

static void *worker_thread(void *data)
{
	arpc_session_handle_t session_fd = (arpc_session_handle_t)data;
	int64_t i =0;
	int ret;

	ret = session_send_msg(session_fd, MAX_LOOP_TIMES);
	if (ret){
		printf("error: thread[%lu] fail.\n", pthread_self());
	}
	printf("thread[%lu] exit success.\n", pthread_self());
	return NULL;
}
/*---------------------------------------------------------------------------*/
/* main									     */
/*---------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
	uint32_t				i = 0;
	int 					ret = 0;
	uint64_t				offset = 0,send_len = 0, file_len =0;
	pthread_t		g_thread_id[MAX_THREADS] = {0};

	struct arpc_client_session_param param;
	struct arpc_msg *request =NULL;
	arpc_session_handle_t session_fd;
	struct arpc_msg_param p;
	FILE *fp = NULL;
	BYTE buf[SHA256_BLOCK_SIZE];
	BYTE buf_str[SHA256_BLOCK_SIZE*2 +1];
	SHA256_CTX ctx;
	struct aprc_option opt = {0};
	struct timeval start_now;
	struct timeval end_now;

	if (argc < 5) {
		printf("Usage: %s <host> <port> <file path> <req data>. \n", argv[0]);
		return 0;
	}
	arpc_cond_init(&g_cond);

	printf("input:<%s> <%s> <%s> <%s>\n", argv[1], argv[2], argv[3], argv[4]);
	
	g_filepath = argv[3];

	//SET_FLAG(opt.control, ARPC_E_CTRL_CRC); //开启通信CRC检查
	opt.msg_iov_max_len = 4*1024;
	opt.thread_max_num = 32;
	arpc_init_r(&opt);
	// 创建session
	memset(&param, 0, sizeof(param));
	param.con.type = ARPC_E_TRANS_TCP;
	memcpy(param.con.ipv4.ip, argv[1], IPV4_MAX_LEN);
	param.con.ipv4.port = atoi(argv[2]);
	param.req_data = argv[4];
	param.req_data_len = strlen(argv[4]);
	param.con_num = 4;
	param.rx_con_num = 0;
	param.ops = &ops;
	session_fd = arpc_client_create_session(&param);
	if (!session_fd){
		printf("arpc_client_create_session fail\n");
		goto end;
	}
	arpc_sleep(1);
	gettimeofday(&start_now, NULL);	//
	for (i = 0; i < MAX_THREADS; i++) {
		pthread_create(&g_thread_id[i], NULL, worker_thread, session_fd);
	}

	/* join the threads */
	for (i = 0; i < MAX_THREADS; i++)
		pthread_join(g_thread_id[i], NULL);
	gettimeofday(&end_now, NULL);	//
	end_now.tv_sec = end_now.tv_sec - start_now.tv_sec;
	end_now.tv_sec = (end_now.tv_sec)?end_now.tv_sec:1;
	end_now.tv_usec = (end_now.tv_usec >= start_now.tv_usec)?(end_now.tv_usec - start_now.tv_usec):
						(end_now.tv_usec +(1000*1000- start_now.tv_usec));
	printf("\n\n################################################\n");
	printf("##### task test end for targe[%s:%s] \n", argv[1], argv[2]);
	printf("##### send IO size:[%lu KB].\n", g_file_size/1024);
	printf("##### send connection num:[%u].\n", param.con_num);
	printf("##### send thread num:[%u].\n", MAX_THREADS);
	printf("##### send total QPS:[%lu q/s].\n", (MAX_THREADS*MAX_LOOP_TIMES)/end_now.tv_sec);
	printf("##### send total v:[%lu KB/s].\n", (g_file_size*MAX_LOOP_TIMES*MAX_THREADS)/1024/end_now.tv_sec);
	printf("##### send total times:[%lu.%05lu s] .\n\n", end_now.tv_sec, end_now.tv_usec);
	printf("\n\n################################################\n");
	arpc_client_destroy_session(&session_fd);
	printf("file send complete:%s.\n\n", g_filepath);
end:
	if (fp)
		fclose(fp);
	fp =NULL;
	arpc_finish();
	return 0;
}


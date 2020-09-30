/*
 * Copyright(C) 2020 Ruijie Network. All rights reserved.
 */

/*!
* \file async_rpc_api.h
* \brief 异步session接口
* 
* 包含异步session外部所用到的接口和结构体
*
* \copyright 2020 Ruijie Network. All rights reserved.
* \author hongchunhua@ruijie.com.cn
* \version v1.0.0
* \date 2020.08.05
* \note none 
*/

#ifndef _ARPC_API_H_
#define _ARPC_API_H_

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* arpc_session_handle_t;				/*! @brief session 句柄 */
typedef void* arpc_server_t;						/*! @brief server */

#define _DEF_SESSION_SERVER
#define _DEF_SESSION_CLIENT

struct aprc_option{
	uint32_t  thread_max_num;		/*! @brief 最大工作线程数，同一个线程池管理 */
	uint32_t  cpu_max_num;			/*! @brief cpu核心数，用于线程绑定 */
	uint32_t  msg_head_max_len; 	/*! @brief 每条消息的head最大长度，[128, 1024]，默认256 */
	uint64_t  msg_data_max_len; 	/*! @brief 每条消息的数据最大长度 ，[1k, 2*1024k] 默认1k，samba读写至少需要1M*/
	uint32_t  msg_iov_max_len;		/*! @brief IOV最大长度 ，[1024, 8*1024k] 默认1k，pfile需要设置为4k*/
	uint32_t  tx_queue_max_depth;  	/*! @brief  发送消息缓冲队列深度度[64, 1024]， 512*/
	uint64_t  tx_queue_max_size;    /*! @brief 发送消息缓冲队列buf大小，单位B, 默认64M*/
	uint32_t  rx_queue_max_depth;  	/*! @brief  接收消息缓冲队列深度度[64, 1024], 默认512*/
	uint64_t  rx_queue_max_size;    /*! @brief 接收消息缓冲队列buf大小，单位B， 默认64M, 消息长度越大，该值也越大，否则会经常发送失败*/
};

/*!
 *  @brief  arpc消息框架全局初始化，支持参数设置
 *
 *  @param[in] opt  aprc参数
 *  @return  arpc_session_handle_t; (<em>NULL</em>: fail ; ( <em>非NULL</em>: succeed
 *
 */
int arpc_init_r(struct aprc_option *opt);

/*! 
 * @brief arpc消息框架全局初始化
 * 
 * 无论是client还是server,必须先全局初始化后，才能使用session功能。
 * 这里暂未实现配置入参，后续支持// todo
 * 
 * @return  int; (<em>-1</em>: fail ; ( <em>0</em>: succeed
 */
int arpc_init();

/*! 
 * @brief arpc消息框架结束退出
 * 
 * 所有session都关闭和server资源都释放后，再调用这个，否则会有意想不到的错误
 * 
 * @return  void; 
 */
void arpc_finish();

enum arpc_trans_type {
	ARPC_E_TRANS_TCP = 0,
};

#define IPV4_MAX_LEN 16
struct arpc_ipv4_addr {
	char ip[IPV4_MAX_LEN];					
	uint32_t port;				
};

/**
 * @brief  链路参数
 *
 * @details
 *  	用于session的传输层的通信参数，如TCP/IP；这里暂时支持TCP模式，
 * 		后续可以拓展通信方式，如本地进程通信等
 */
struct arpc_con_info{
	enum arpc_trans_type type;
	union{
		struct arpc_ipv4_addr ipv4;
	};
};

/**
 * @brief  IOV结构
 *
 * @details
 *  	
 */
struct arpc_iov{
	void* 		data;
	size_t		len;
};

/**
 * @brief  消息头信息
 *
 * @details
 *  	收到请求消息时，先读取消息头，执行回调函数后，在继续接收消息体
 */
struct arpc_header_msg{
	uint32_t		head_len;								/*! @brief 头部长度 */
	void			*head;									/*! @brief 头部数据 */
	uint64_t		data_len;								/*! @brief 数据全部长度 */
};

/*! @brief 配置默认的VEC深度 */
#define 	ARPC_VEC_MAX_NUM 		4

enum arpc_vec_type {
	ARPC_VEC_TYPE_PRT = 0,									/*! @brief 自定义指针，用户分配vec结构体 */
	ARPC_VEC_TYPE_ARR,										/*! @brief 数组，默认为ARPC_VEC_MAX_NUM定义的数量*/
	ARPC_VEC_TYPE_INTER,									/*! @brief XIO内部vec指针*/
	ARPC_VEC_TYPE_NONE,										/*! @brief */
};
/**
 * @brief  arpc基础消息结构
 *
 * @details
 *  	arpc对外提供的数据结构
 */
struct arpc_vmsg{
	uint32_t		head_len;								/*! @brief 头部长度 */
	void			*head;									/*! @brief 头部数据 */
	uint64_t		total_data;								/*! @brief 数据全部长度 */
	uint32_t 		vec_num;								/*! @brief IO vector数量 */
	enum arpc_vec_type	vec_type;							/*! @brief 默认为用户自定义分配 */
	union{
		struct arpc_iov *vec;								/*! @brief vector ，vec结构体需要用户自行分配*/
		struct arpc_iov vec_arr[ARPC_VEC_MAX_NUM];			/*! @brief vector 默认分配一定的数据，解决每次需自行分配vec结构导致操作的麻烦*/
	};
	uint32_t 		vec_max_num;							/*! @brief vec结构体数量，默认为VEC_MAX_NUM*/
};

/*! @brief rsp消息的资源操作句柄，只有收到request请求时才会分配该句柄 */
typedef void *arpc_rsp_handle_t;

/**
 * @brief  arpc基础消息结构
 *
 * @details
 *  	arpc对外提供的数据结构
 */
struct arpc_rsp{
	uint32_t			flags;								/*! @brief 消息处理标记位,通过 SET_METHOD方法设置*/
	struct arpc_vmsg 	*rsp_iov;							/*! @brief 由调用者填充的回复的数据 */
	arpc_rsp_handle_t   rsp_fd;								/*! @brief 消息回复句柄，用于arpc_do_respone接口*/
	void				*rsp_ctx;
};

/*! 
 * @brief 内存释放函数
 * 
 * @param[in] buf_ptr ,内存指针
 * @param[in] usr_context ,用户上下文，初始化时由调用者入参，由调用者使用
 * @return mem buf
 */
typedef int (*mem_free_cb_t)(void* buf_ptr, void* usr_context);


#define METHOD_ALLOC_DATA_BUF			0  					/*! @brief 申请调用者自定义内存 */
#define METHOD_ARPC_PROC_SYNC			1  					/*! @brief ARPC接收消息同步处理 。默认情况下异步处理，需要线程池支持*/
#define METHOD_CALLER_ASYNC				2  					/*! @brief 转调用者异步处理，消息回复需要调用者自己显性调用 */
#define METHOD_CALLER_HIJACK_RX_DATA	3  					/*! @brief 调用者劫持接收数据,buf需要调用者释放，ARPC框架不释放。*/
															/*		   生效条件是必须用户分配内存 */

/**
 * @brief  标记位操作方法
 *
 * @details
 *  	用于请求处理后，设置session主框架参数
 */
#define SET_METHOD(flag, method) (flag=(flag|(1<<method)))	/*! @brief 设置方法 */
#define CLR_METHOD(flag, method) (flag=(flag&~(1<<tag)))	/*! @brief 清除方法 */
#define CLR_ALL_METHOD(flag) (flag=0)						/*! @brief 清除全部方法 */

/**
 * @brief  请求消息操作函数
 *
 * @details
 *  	session收到消息类型有三种，请求（request）/回复（respone）/单向消息（oneway）
 * 		每个消息体都执行不同的操作函数。请求消息框架必须自动回复消息给请求方，如果调用者
 * 		需要回复私有数据，则需要注册释放函数《release_rsp_cb》来释放私有数据资源
 */
struct request_ops {
	void* (*alloc_cb)(uint32_t size, void* usr_context);
	int (*free_cb)(void* buf_ptr, void* usr_context);

	/*! @brief step1, 处理消息头，获取数据体参数（如预分配内存，异步处理设置） */
	int (*proc_head_cb)(struct arpc_header_msg *header, void* usr_context, uint32_t *flag);

	/*! @brief step2, 同步处理用户的数据，如果是同步，则异步不执行。*/
	int (*proc_data_cb)(const struct arpc_vmsg *req_iov, struct arpc_rsp *rsp, void* usr_context);

	/*! @brief step4, 异步处理调用者buf数据。*/
	int (*proc_async_cb)(const struct arpc_vmsg *req_iov, struct arpc_rsp *rsp, void* usr_context);

	/*! @brief step3, 释放回复消息的资源。*/
	int (*release_rsp_cb)(struct arpc_vmsg *rsp_iov, void* usr_context);
};

/**
 * @brief  回复消息操作函数
 *
 * @details
 *  	回复的消息一般是发起request的一方自定义了回调处理，不需要在框架注册数据处理函数
 */
struct respone_ops {
	void* (*alloc_cb)(uint32_t size, void* usr_context);
	int (*free_cb)(void* buf_ptr, void* usr_context);
};

/**
 * @brief  单向消息操作函数
 *
 * @details
 *  	由于单向消息不需要框架自动回复消息给请求方，不需要注册释放函数《release_rsp_cb》
 */
struct oneway_ops {
	void* (*alloc_cb)(uint32_t size, void* usr_context);
	int (*free_cb)(void* buf_ptr, void* usr_context);
	int (*proc_head_cb)(struct arpc_header_msg *header, void* usr_context, uint32_t *flag);
	int (*proc_data_cb)(const struct arpc_vmsg *req_iov, uint32_t *flag, void* usr_context);
	int (*proc_async_cb)(const struct arpc_vmsg *req_iov, uint32_t *flag, void* usr_context);
};

/**
 * @brief  消息操作函数合集
 *
 * @details
 *  	只要建立起一个session,就需要注册三种消息体对应的操作函数
 */
struct arpc_session_ops {
	struct request_ops			req_ops;
	struct respone_ops			rsp_ops;
	struct oneway_ops			oneway_ops;	
};



#define PRIVATE_HANDLE	char handle[0]					/*! @brief 私用数据段声明 */

#define MAX_HEADER_DATA_LEN   (512)						/*! @brief 消息体头部最大长度 */
#define DATA_DEFAULT_MAX_LEN  (4*1024)					/*! @brief 消息体数据段最大长度 1024*1024*/
#define IOV_DEFAULT_MAX_LEN   (1024)					/*! @brief 数据每个IOV最长长度 4*1024*/
#define IOV_MAX_DEPTH (DATA_DEFAULT_MAX_LEN/IOV_DEFAULT_MAX_LEN) /*! @brief 数据每个IOV_MAX_DEPTH*/
/**
 * @brief  session消息体实例化参数
 *
 * @details
 *  	消息体是session数据收发的基础结构，需要先实例化
 */
struct arpc_msg_param{
	void* (*alloc_cb)(uint32_t size, void* usr_context);	/*! @brief 用于接收rsp消息时分配内存，可选 */
	int   (*free_cb)(void* buf_ptr, void* usr_context);		/*! @brief 内存释放 可选*/
	void 		*usr_context;								/*! @brief 用户上下文 */
	uint32_t 	flag;										/*! @brief 预留参数 */
};

/**
 * @brief  session消息结构体
 *
 * @details
 *  	每个session发送请求前，都需要向消息框架申请一个消息体，利用消息体发送数据。
 * 		由于session是异步发送，数据发送完毕和收到对应的数据都输通过异步通知的方式，
 * 		消息体除了需要用于异步发送数据，还需要用于异步回复消息的接收。
 */
struct arpc_msg{

	/*! @brief 待发送的数据， 异步发送，如果未设置clean_send_cb回调则会阻塞到发送完毕。*/
	struct arpc_vmsg	send;

	void 				*send_ctx;
	/*! @brief 发送完毕后回调，用于清理发送的资源 */
	int (*clean_send_cb)(struct arpc_vmsg *send, void* send_ctx);

	/*! @brief 待接受的消息数据， 消息框架会自动把send的对应的回复消息放置到这里 */
	struct arpc_vmsg	receive;

	void 				*receive_ctx;
	/*! @brief 接收到回复消息后， 用于处理发送的数据 */
	int (*proc_rsp_cb)(struct arpc_vmsg *receive, void *receive_ctx);

	/*! @brief 消息框架内部实例化私有数据，对调用者不可见 */
	PRIVATE_HANDLE;
};

/*! 
 * @brief 申请一个消息体
 * 
 * 向消息框架申请一个消息体，会分配内部内存。
 * 若调用者设置内存分配操作，则会根据用户参数自动为回复消息分配内存
 * 
 * @param[in] p 消息体参数,可以为空，则采用默认方式新建消息体
 * @return  arpc_session_handle_t; (<em>NULL</em>: fail ; ( <em>非NULL</em>: succeed
 */
struct arpc_msg *arpc_new_msg(const struct arpc_msg_param *p);

/*! 
 * @brief 释放一个消息体
 * 
 * 释放分配的内存；
 * 注意：如果消息体处在发送待接收状态，即框架锁定，则会释放不成功，必须等待消息框架释放消息体。
 * 		消息框架会确保消息体不会长期持有的，消息超时或者发送失败都是自动释放消息体
 * 
 * @param[inout] msg ,消息体指针的指针，如果释放成功，则句柄将会被置空
 * @return  int; (<em>-1</em>: fail ; ( <em>0</em>: succeed
 */
int arpc_delete_msg(struct arpc_msg **msg);

/*! 
 * @brief 重置一个消息体
 * 
 * 重复利用一个消息体，避免发送数据不断申请资源。
 * 注意：处在发送状态的消息体无法重置，重置是用于释放接收回复消息的缓存buff
 * 
 * @param[in] msg ,消息体指针
 * @return  int; (<em>-1</em>: fail ; ( <em>0</em>: succeed
 */
int arpc_reset_msg(struct arpc_msg *msg);

/*! 
 * @brief 发送请求
 * 
 * 发送一个请求等待回复（发送消息并阻塞等待接收方回复）
 * 
 * @param[in] fd ,a session handle
 * @param[in] msg ,a data that will send
 * @param[in] timeout_ms , 超时时间， -1则一直等待，若设置回调，则该值不生效，直接返回
 * @return int .0,表示发送成功，小于0则失败
 */
int arpc_do_request(const arpc_session_handle_t fd, struct arpc_msg *msg, int32_t timeout_ms);

typedef int (*clean_send_cb_t)(struct arpc_vmsg *send, void* usr_ctx);
/*! 
 * @brief 发送单向消息
 * 
 * 发送一个单向消息（接收方无需回复）
 * 
 * @param[in] fd ,a session handle
 * @param[in] msg ,a data that will send
 * @return int .0,表示发送成功，小于0则失败
 */
int arpc_send_oneway_msg(const arpc_session_handle_t fd, 
						struct arpc_vmsg *send, 
						clean_send_cb_t clean_send, 
						void* send_ctx);

/*! @brief 调用者回复消息资源释放回调函数，用于回复消息发送完毕后执行资源释放 */
typedef int (*rsp_cb_t)(struct arpc_vmsg *rsp_iov, void* rsp_cb_ctx);

/*! 
 * @brief 回复消息个请求方
 * 
 * 回复一个消息个请求方（只能是请求的消息才能回复）
 * 
 * @param[in] rsp_fd ,a rsp msg handle pointer, 由接收到消息回复时获取
 * @param[in] rsp_iov ,回复的消息
 * @param[in] release_rsp_cb ,回复结束后回调函数，用于释放调用者的回复消息资源
 * @param[in] rsp_cb_ctx , 回调函数调用者入参,可选，可以是NULL
 * @return int .0,表示发送成功, rsp_f会被置空; 小于0则失败，rsp_fd若不为空，需要再次回复
 */
int arpc_do_response(arpc_rsp_handle_t *rsp_fd, struct arpc_vmsg *rsp_iov, rsp_cb_t release_rsp_cb, void* rsp_cb_ctx);


/*! 
 * @brief 设置session通信参数
 * 
 * 可以根据session场景设置通信参数，可选，若为0，则使用初始设置的参数
 * 
 */
struct aprc_session_opt{
	uint32_t  msg_head_max_len; 	/*! @brief 每条消息的head最大长度，[128, 1024]，默认256 */
	uint64_t  msg_data_max_len; 	/*! @brief 每条消息的数据最大长度 ，[1k, 2*1024k] 默认1k，samba读写至少需要1M*/
	uint32_t  msg_iov_max_len;		/*! @brief IOV最大长度 ，[1024, 8*1024k] 默认1k，pfile需要设置为4k*/
};


/*! 
 * @brief 设置session通信参数
 * 
 * 可以根据session场景设置通信参数，可选，若为0，则使用初始设置的参数
 * 
 * @param[in] ses ,a rsp msg handle pointer, 由接收到消息回复时获取
 * @param[in] opt ,回复的消息
 * @return int .0,表示发送成功, ; 小于0则失败
 */
int arpc_get_session_opt(const arpc_session_handle_t *ses, struct aprc_session_opt *opt);

/**********************************************************************************************
 * @name client
 * @brif clien 客户端
 **********************************************************************************************/
#ifdef _DEF_SESSION_CLIENT

#define MAX_SESSION_REQ_DATA_LEN  1024								/*! @brief 申请session的数据长度 */

/**
 * @brief  客户端session实例化参数
 *
 * @details
 *  	用于实例化客户端session的参数
 */
struct arpc_client_session_param {
	struct arpc_con_info  		con;								/*! @brief 每个连接的传输类型和参数 */
	uint32_t					con_num;							/*! @brief 连接数量 包括读和写连接通道总数*/
	uint32_t					rx_con_num;							/*! @brief 接收通道数量，用于读写分离，0默认为不读写分离*/
	struct arpc_session_ops		*ops;								/*! @brief 回调函数 */
	void 						*ops_usr_ctx;						/*! @brief 调用者的上下文参数，用于操作函数入参 */
	uint32_t					req_data_len;						/*! @brief 申请session时的数据 */
	void						*req_data;							/*! @brief 调用者新建session请求时，发给服务端数据 */
	struct aprc_session_opt		opt;
};

#define ARPC_CLIENT_MAX_CON_NUM 16
/*!
 *  @brief  创建session客户端实例
 *
 *  @param[in] param  session服务端实例化的参数
 *  @return  arpc_session_handle_t; (<em>NULL</em>: fail ; ( <em>非NULL</em>: succeed
 *
 */
arpc_session_handle_t arpc_client_create_session(const struct arpc_client_session_param *param);

/*!
 *  @brief  销毁session客户端实例
 *
 *  @param[in] fd  session句柄
 *  @return  int; (<em>-1</em>: fail ; ( <em>0</em>: succeed
 *
 */
int arpc_client_destroy_session(arpc_session_handle_t *fd);

/**
 * @brief  session状态枚举
 *
 * @details
 *  	用于获取session状态
 */
enum arpc_session_status {
	ARPC_SESSION_STA_NOT_EXISTED = -1,							/*! @brief session不存在或者已经释放 */
	ARPC_SESSION_STA_ACTIVE = 0,								/*! @brief 正常活跃 */
	ARPC_SESSION_STA_RE_CON,									/*! @brief 断路，尝试重连 */
	ARPC_SESSION_STA_WAIT,										/*! @brief 断路，周期尝试重连 */
};

/*!
 *  @brief  获取session状态
 *
 *  @param[in] fd  session句柄
 *  @return  arpc_session_status;
 *
 */
enum arpc_session_status arpc_get_session_status(const arpc_session_handle_t fd);

#endif


#ifdef _DEF_SESSION_SERVER

/*!
 * @brief  新建session请求消息
 *
 * @details
 *  该状态是由client端发给server端,用于请求新建session
 *  
 */
struct arpc_new_session_req{
	uint64_t						session_id;
	struct arpc_con_info 			client_con_info;
	struct arpc_iov 				client_data;
};

/*!
 * @brief  新建session的状态
 *
 * @details
 *  该状态是由server端发给client端,用于应答新建session状态
 *  
 */
enum arpc_new_session_status{
	ARPC_E_STATUS_OK = 0,										/*! @brief session可正常建立*/
	ARPC_E_STATUS_INVALID_USER, 								/*! @brief 非法用户，拒绝建立session */
	ARPC_E_STATUS_TOO_MANY_USERS, 								/*! @brief 连接的用户太多，拒绝连接服务 */
	ARPC_E_STATUS_UNKOWN_ERR, 									/*! @brief 未知错误，拒绝建立session */
};

/*!
 * @brief  新建session回复消息
 *
 * @details
 *  该状态是由server端发给client端,用于回复新建session的结果
 *  
 */
struct arpc_new_session_rsp{
	void 							*rsp_data;
	uint32_t						rsp_data_len;
	enum arpc_new_session_status	ret_status;					/*! @brief 应答当前新建session调用者返回值，ARPC_E_STATUS_OK是可以建立，其它则失败*/
	struct arpc_session_ops			*ops;						/*! @brief 设置当前session的回调函数，可选，默认使用注册时回调函数*/
	void							*ops_new_ctx;				/*! @brief 新session的回调函数上下文参数 */
	void							*private;					/*! @brief 调用者私有数据 */
	uint32_t						private_len;				/*! @brief 调用者私有数据长度 */
};

/*!
 *  @brief session服务端实例化参数
 *
 * @details
 *   参数用于配置session功能
 *
 */
struct arpc_server_param {
	/*! @brief 每个连接的传输类型和参数 */
	struct arpc_con_info 	   		con;
	
	/*! @brief 工作线程数，用于并发，如果为0则使用默认值1，则只有一个主线程 */
	int32_t 	   					work_num;

	/*! @brief 收到申请建立session请求时回调，用于处理调用者逻辑和输出回复消息*/
	int (*new_session_start)(const struct arpc_new_session_req *, struct arpc_new_session_rsp *, void *server_ctx);

	/*! @brief 消息框架新建session后回调，用于释放调用者回复消息的资源*/
	int (*new_session_end)(arpc_session_handle_t, struct arpc_new_session_rsp *, void *server_ctx);

	/*! @brief 消息框架sessions释放后回调函数，用于释放调用者回复消息的资源*/
	int (*session_teardown)(const arpc_session_handle_t, void *usr_server_ctx, void *usr_session_ctx);

	/*! @brief server 服务的用户上下文 */
	void 							*server_ctx;

	/*! @brief session服务端默认的消息操作函数 */
	struct arpc_session_ops			default_ops;

	/*! @brief 作为消息处理函数的入参 */
	void 							*default_ops_usr_ctx;

	/*! @brief IOV数据深度 选填，默认为4*1024*/
	uint32_t						iov_max_len;

	/*! @brief session 配置参数，可选，用于资源和性能优化*/
	struct aprc_session_opt		opt;
};

/*!
 *  @brief  创建session服务端实例
 *
 *  @param[in] param  session服务端实例化的参数
 *  @return  int; (<em>NULL</em>: fail ; ( <em>非NULL</em>: succeed
 *
 */
arpc_server_t arpc_server_create(const struct arpc_server_param *param);

/*!
 *  @brief  监听session建立请求
 *
 *  @param[in] fd  session服务端实例化的参数
 *  @param[in] timeout_ms  服务端消息框架阻塞超时时间，-1表示一直阻塞，永不超时。
 *  @return  int; (<em>NULL</em>: fail ; ( <em>非NULL</em>: succeed
 *
 */
int arpc_server_loop(arpc_server_t fd, int32_t timeout_ms);

/*!
 *  @brief  释放session服务端实例
 *
 *  @param[inout] fd  session服务端实例化的参数, 释放成功，则句柄会被置空
 *  @return  int; (<em>NULL</em>: fail ; ( <em>非NULL</em>: succeed
 *
 */
int arpc_server_destroy(arpc_server_t *fd);

#endif

#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */

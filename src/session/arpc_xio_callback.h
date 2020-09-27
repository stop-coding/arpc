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

#ifndef _ARPC_XIO_CALLBACK_H
#define _ARPC_XIO_CALLBACK_H

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/time.h>

#include "base_log.h"
#include "arpc_com.h"
#include "arpc_session.h"


int server_session_event(struct xio_session *session, struct xio_session_event_data *event_data, void *session_context);
int server_on_new_session(struct xio_session *session,struct xio_new_session_req *req, void *server_context);


int client_session_established(struct xio_session *session, struct xio_new_session_rsp *rsp, void *conn_context);
int client_session_event(struct xio_session *session, struct xio_session_event_data *event_data, void *session_context);

int msg_head_process(struct xio_session *session, struct xio_msg *rsp, void *conn_context);
int msg_data_process(struct xio_session *session,struct xio_msg *msg, int last_in_rxq, void *conn_context);

//消息送达到对方
int msg_delivered(struct xio_session *session,struct xio_msg *msg, int last_in_rxq, void *conn_context);
int response_send_complete(struct xio_session *session, struct xio_msg *rsp, void *conn_context);
int oneway_send_complete(struct xio_session *session, struct xio_msg *msg, void *conn_context);

//
int message_error(struct xio_session *session, enum xio_status error, enum xio_msg_direction dir, struct xio_msg  *rsp, void *conn_context);


#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */

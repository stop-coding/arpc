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

#ifndef _BASE_LOG_H_
#define _BASE_LOG_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BASE_ERROR			-1
#define BASE_SUCCESS		 0

enum arpc_log_level{
	ARPC_LOG_LEVEL_E_FATAL = 0,
	ARPC_LOG_LEVEL_E_ERROR,
	ARPC_LOG_LEVEL_E_WARN,
	ARPC_LOG_LEVEL_E_INFO,
	ARPC_LOG_LEVEL_E_DEBUG,
	ARPC_LOG_LEVEL_E_TRACE,
	ARPC_LOG_LEVEL_E_MAX,
};
void arpc_vlog(enum arpc_log_level level, const char *module, const char *file,unsigned line, const char *function, const char *fmt, ...);


#define BASE_LOG_ERROR(format, arg...) \
		arpc_vlog(ARPC_LOG_LEVEL_E_ERROR, "ARPC", __FILE__, __LINE__, __FUNCTION__, format, ##arg);

#define BASE_LOG_NOTICE(format, arg...) \
		arpc_vlog(ARPC_LOG_LEVEL_E_INFO, "ARPC", __FILE__, __LINE__, __FUNCTION__, format, ##arg);

#define BASE_LOG_TRACE(format, arg...)\
		arpc_vlog(ARPC_LOG_LEVEL_E_TRACE, "ARPC", __FILE__, __LINE__, __FUNCTION__, format, ##arg);

#define BASE_LOG_DEBUG(format, arg...) \
		arpc_vlog(ARPC_LOG_LEVEL_E_DEBUG, "ARPC", __FILE__, __LINE__, __FUNCTION__, format, ##arg);

#define unlikely(x)    __builtin_expect(!!(x), 0)

#define LOG_THEN_GOTO_TAG_IF_VAL_TRUE(val, tag, format, arg...)	\
do{\
	if(unlikely((val))){\
		BASE_LOG_ERROR(format,##arg);\
		goto tag;\
	}\
}while(0);

#define LOG_THEN_RETURN_IF_VAL_TRUE(val, format, arg...)	\
do{\
	if(unlikely((val))){\
		BASE_LOG_ERROR(format,##arg);\
		return;\
	}\
}while(0);

#define LOG_THEN_RETURN_VAL_IF_TRUE(val, ret, format, arg...)\
do{\
	if(unlikely((val))){\
		BASE_LOG_ERROR(format, ##arg);\
		return ret;\
	}\
}while(0);

#define BASE_ASSERT(condition, format, arg...)	\
do{\
	if(unlikely(!(condition))){\
		BASE_LOG_ERROR(format, ##arg);\
	}\
	assert(condition);\
}while(0);

#define LOG_ERROR_IF_VAL_TRUE(val, format, arg...)	\
do{\
	if(unlikely((val))){\
		BASE_LOG_ERROR(format,##arg);\
	}\
}while(0);


#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */

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

//#define BASE_DEBUG_ON
//#define BASE_LOG_ERROR(format, arg...) syslog(LOG_ERR, 	"[ ARPC] [ ERROR] file:%s func: %s|%d---"format"\n", __FILE__, __FUNCTION__, __LINE__,##arg)
//#define BASE_LOG_NOTICE(format, arg...) syslog(LOG_NOTICE, "[ ARPC] [NOTICE] func: %s|%d---"format"\n",__FUNCTION__, __LINE__, ##arg)

#define BASE_LOG_ERROR(format, arg...) fprintf(stderr, 	"[ ARPC] [ ERROR] file:%s func: %s|%d---"format"\n", __FILE__, __FUNCTION__, __LINE__,##arg)
#define BASE_LOG_NOTICE(format, arg...) fprintf(stderr, "[ ARPC] [NOTICE] func: %s|%d---"format"\n",__FUNCTION__, __LINE__, ##arg)

#ifdef BASE_DEBUG_ON
#define BASE_LOG_DEBUG(format, arg...) syslog(LOG_WARNING, "[ ARPC] [ DEBUG] func: %s|%d---"format"\n",__FUNCTION__, __LINE__, ##arg)
#else
#define BASE_LOG_DEBUG(format, arg...)
#endif

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
	if(unlikely((condition))){\
		BASE_LOG_ERROR(format, ##arg);\
		assert(!condition);\
	}\
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

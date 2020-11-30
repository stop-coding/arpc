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

#include <sys/syscall.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>

#include "base_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_TIME_FMT "%04d/%02d/%02d-%02d:%02d:%02d.%05ld"

#define LOG_SWITCH_INIT (1<<ARPC_LOG_LEVEL_E_MAX)
#define LOG_STATUS_SWICH_FILE  "on_status"
#define LOG_TRACE_SWICH_FILE "on_trace"
#define LOG_DEBUG_SWICH_FILE "on_debug"
#define LOG_INFO_SWICH_FILE  "on_info"
#define LOG_ERROR_SWICH_FILE "off_err"
#define LOG_FATAL_SWICH_FILE "off_info"

//syslog
#define SYSLOG_SWICH_ENABLE (1<<(ARPC_LOG_LEVEL_E_MAX + 1))
#define SYSLOG_SWICH_FILE  "on_syslog"

//fprintf
#define FPRINTF_SWICH_ENABLE (1<<(ARPC_LOG_LEVEL_E_MAX + 2))
#define FPRINTF_SWICH_FILE "on_fprintf"

//本地文件
#define LOG_FILE_ENABLE (1<<(ARPC_LOG_LEVEL_E_MAX + 3))
#define LOG_FILE_SWICH_FILE  "on_filelog"
#define LOG_FILE_NAME        "messages.log"
#define LOG_FILE_MAX_SIZE    (512*1024)


static void set_log_switch(const char *module, int32_t *log_enabale)
{
    char			buf[128];

	snprintf(buf, sizeof(buf), "/run/%s/%s", module, LOG_STATUS_SWICH_FILE);
    if(access(buf, F_OK) == 0){
        *log_enabale = (*log_enabale)|(1<<ARPC_LOG_LEVEL_E_STATUS);
    }
    snprintf(buf, sizeof(buf), "/run/%s/%s", module, LOG_TRACE_SWICH_FILE);
    if(access(buf, F_OK) == 0){
        *log_enabale = (*log_enabale)|(1<<ARPC_LOG_LEVEL_E_TRACE);
    }
    snprintf(buf, sizeof(buf), "/run/%s/%s", module, LOG_DEBUG_SWICH_FILE);
    if(access(buf, F_OK) == 0){
        *log_enabale = (*log_enabale)|(1<<ARPC_LOG_LEVEL_E_DEBUG);
    }
    snprintf(buf, sizeof(buf), "/run/%s/%s", module, LOG_INFO_SWICH_FILE);
    if(access(buf, F_OK) == 0){
        *log_enabale = (*log_enabale)|(1<<ARPC_LOG_LEVEL_E_INFO);
    }
    snprintf(buf, sizeof(buf), "/run/%s/%s", module, LOG_ERROR_SWICH_FILE);
    if(access(buf, F_OK) != 0){
        *log_enabale = (*log_enabale)|(1<<ARPC_LOG_LEVEL_E_ERROR);
    }
    snprintf(buf, sizeof(buf), "/run/%s/%s", module, LOG_FATAL_SWICH_FILE);
    if(access(buf, F_OK) != 0){
        *log_enabale = (*log_enabale)|(1<<ARPC_LOG_LEVEL_E_FATAL);
    }

	//日志输出路径

	snprintf(buf, sizeof(buf), "/run/%s/%s", module, SYSLOG_SWICH_FILE);
    if(access(buf, F_OK) == 0){
        *log_enabale = (*log_enabale)|SYSLOG_SWICH_ENABLE;
    }

    snprintf(buf, sizeof(buf), "/run/%s/%s", module, FPRINTF_SWICH_FILE);
    if(access(buf, F_OK) == 0){
        *log_enabale = (*log_enabale)|FPRINTF_SWICH_ENABLE;
    }

    snprintf(buf, sizeof(buf), "/run/%s/%s", module, LOG_FILE_SWICH_FILE);
    if(access(buf, F_OK) == 0){
        *log_enabale = (*log_enabale)|LOG_FILE_ENABLE;
    }

    *log_enabale = (*log_enabale)|LOG_SWITCH_INIT;
}

static pid_t gettid()
{
	return syscall(SYS_gettid);
}

static long get_file_size(char* filename)
{
	long length = 0;
	FILE *fp = NULL;

	fp = fopen(filename, "rb");
	if (fp != NULL){
		fseek(fp, 0, SEEK_END);
		length = ftell(fp);
        fclose(fp);
	}
	return length;
}

static void write_log_file(char* filename, long max_size, const char *fmt, ...)
{
	FILE *fp;
    long length;
    va_list			args;
	char			buf[2048];
    if (filename != NULL){
		length = get_file_size(filename);
		if (length > max_size){
			unlink(filename); // 删除文件
		}
		fp = fopen(filename, "at+");
        if (fp != NULL){
            va_start(args, fmt);
            length = vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);
            buf[length] = 0;
            fwrite(buf, length, 1, fp);
            fclose(fp);
            fp = NULL;
        }
	}
}

void arpc_vlog(enum arpc_log_level level, const char *module, const char *file,unsigned line, const char *function, const char *fmt, ...)
{
	va_list			args;
	const char		*short_file;
	struct timeval		tv;
	struct tm		t;
	char			buf[2048];
	char			buf2[256];
	char            log_file_path[64];
	int			length = 0;
	static int32_t log_enabale = 0;
	static const char * const level_str[ARPC_LOG_LEVEL_E_MAX] = {
		"FATAL", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"
	};
	time_t time1;

	if (log_enabale&LOG_SWITCH_INIT) {
        if(!(log_enabale&(1<<level))){
            return;
        }
    }else{
        set_log_switch(module, &log_enabale);
		if(!(log_enabale&(1<<level))){
            return;
        }
    }

	va_start(args, fmt);
	length = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	buf[length] = 0;

	if(level ==ARPC_LOG_LEVEL_E_STATUS){
		if (log_enabale&FPRINTF_SWICH_ENABLE) {
			fprintf(stderr,"%s", buf);
		}
		if (log_enabale&SYSLOG_SWICH_ENABLE) {
			syslog(LOG_ERR,"%s", buf);
		}

		if (log_enabale&LOG_FILE_ENABLE) {
			snprintf(log_file_path, sizeof(log_file_path), "/run/%s/%s", module, LOG_FILE_NAME);
        	write_log_file(log_file_path, LOG_FILE_MAX_SIZE,"%s",buf);
		}
		return;
	}

	gettimeofday(&tv, NULL);
	time1 = (time_t)tv.tv_sec;
	localtime_r(&time1, &t);

	short_file = strrchr(file, '/');
	short_file = (!short_file) ? file : short_file + 1;
	if(level <= ARPC_LOG_LEVEL_E_ERROR) {
		snprintf(buf2, sizeof(buf2), "%s:%u|%s", short_file, line, function);
	}else{
		snprintf(buf2, sizeof(buf2), "%s:%u", short_file, line);
	}

	if (log_enabale&FPRINTF_SWICH_ENABLE) {
		fprintf(stderr, "[%-5s][%-5x][%-5s] [" LOG_TIME_FMT "] %-24s - %s\n", module, gettid(),
			level_str[level],
			t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
			t.tm_hour, t.tm_min, t.tm_sec, tv.tv_usec,
			buf2,
			buf);
	}
	if (log_enabale&SYSLOG_SWICH_ENABLE) {
		syslog(LOG_ERR, "[%-5s][%-5x][%-5s] [" LOG_TIME_FMT "] %-24s - %s", module, gettid(),
			level_str[level],
			t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
			t.tm_hour, t.tm_min, t.tm_sec, tv.tv_usec,
			buf2,
			buf);
	}

	if (log_enabale&LOG_FILE_ENABLE) {
        snprintf(log_file_path, sizeof(log_file_path), "/run/%s/%s", module, LOG_FILE_NAME);
        write_log_file(log_file_path, LOG_FILE_MAX_SIZE, "[%-5s][%-5x][%-5s] [" LOG_TIME_FMT "] %-24s - %s\n", module, gettid(),
		level_str[level],
		t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
		t.tm_hour, t.tm_min, t.tm_sec, tv.tv_usec,
		buf2,
		buf);
    }
	//fflush(stderr);
}

#ifdef __cplusplus
}
#endif


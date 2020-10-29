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

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <inttypes.h>
#include <semaphore.h>
#include <sys/sysinfo.h>
#include <sys/prctl.h>

#include "base_log.h"
#include "threadpool.h"
#include "queue.h"


#define TP_LOG_ERROR(format, arg...) BASE_LOG_ERROR(format, ##arg)
#define TP_LOG_NOTICE(format, arg...) BASE_LOG_NOTICE(format, ##arg)
#define TP_LOG_DEBUG(format, arg...) 	BASE_LOG_DEBUG(format,  ##arg)

#define FLAG_TASK_ACTIVE  0
#define FLAG_TASK_IDLE    1
#define FLAG_TASK_EXIT    2

#define FLAG_WORK_INIT    0
#define FLAG_WORK_RUN     1
#define FLAG_WORK_DONE    2
#define FLAG_WORK_WAIT    3

#define IS_SET(flag, tag) (flag&(1<<tag))
#define SET_FLAG(flag, tag) flag=(flag|(1<<tag))
#define CLR_FLAG(flag, tag) flag=(flag&~(1<<tag))

#define TASK_SET_ACTIVE(flag) flag=(flag|(1<<FLAG_TASK_ACTIVE))
#define TASK_CLR_ACTIVE(flag) flag=(flag&~(1<<FLAG_TASK_ACTIVE))

#define TASK_SET_IDLE(flag) flag=(flag|(1<<FLAG_TASK_IDLE))
#define TASK_CLR_IDLE(flag) flag=(flag&~(1<<FLAG_TASK_IDLE))

#define TASK_SET_EXIT(flag) flag=(flag|(1<<FLAG_TASK_EXIT))
#define TASK_CLR_EXIT(flag) flag=(flag&~(1<<FLAG_TASK_EXIT))

#define HIDE_ADDR 1

#define WORK_PRIVATE_DATA QUEUE queue;pthread_cond_t cond;pthread_mutex_t mutex;uint32_t flag;
struct _work {
	int (*loop)(void *usr_ctx);		/* 循环回调函数*/
	void (*stop)(void *usr_ctx);		/* 停止循环回调函数*/
	void *usr_ctx;						/* 任务上下文*/
	pthread_t  thread_id;
	WORK_PRIVATE_DATA;					/* 队列标识，内部使用，无需设置*/
};

struct _thread_msg{
    int32_t           work_id;
    pthread_t         thread_id; 
    uint32_t          flag;
};


struct _thread_pool_msg{
    QUEUE               wait_to_run;
    pthread_cond_t 	    cond;					/* 数据接收的条件变量*/
	pthread_mutex_t     mutex;	    			/* lock */
    uint32_t            thread_num;
    struct _thread_msg  *thread;
    uint32_t            idle_num;
	uint32_t			cpu_max_num;
	sem_t 				*sync;					/* 线程同步*/
	char  				ext_data[0];				
};

/* To avoid deadlock with uv_cancel() it's crucial that the worker
 * never holds the global mutex and the loop-local mutex at the same time.
 */
static void *task_worker(void* arg) {
	QUEUE* q;
	struct _work *to_run;
	struct _thread_msg  *t_msg;
	cpu_set_t		cpuset;
	struct _thread_pool_msg* pool_ctx = (struct _thread_pool_msg*) arg;
	LOG_THEN_RETURN_VAL_IF_TRUE((!pool_ctx), NULL, "pool_ctx empty fail.");

	pthread_mutex_lock(&pool_ctx->mutex);
	t_msg = &pool_ctx->thread[pool_ctx->idle_num];
	t_msg->work_id = pool_ctx->idle_num;

	CPU_ZERO(&cpuset);
	CPU_SET((pool_ctx->thread_num)%(pool_ctx->cpu_max_num), &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

	prctl(PR_SET_NAME, "share_thread");

	TASK_SET_ACTIVE(t_msg->flag);
	TP_LOG_DEBUG(" Eentry thread[%lu], init first.", t_msg->thread_id);
	/* 线程同步*/
	sem_post(pool_ctx->sync);
	for (;;) {
		while (QUEUE_EMPTY(&pool_ctx->wait_to_run)) {
			pool_ctx->idle_num++;
			TASK_SET_IDLE(t_msg->flag);
			pthread_cond_wait(&pool_ctx->cond, &pool_ctx->mutex);
			TASK_CLR_IDLE(t_msg->flag);
			pool_ctx->idle_num--;
			if (IS_SET(t_msg->flag, FLAG_TASK_EXIT)){
				break; // 被要求退出线程
			}
		}
		if (IS_SET(t_msg->flag, FLAG_TASK_EXIT)){
			break; // 被要求退出线程
		}

		q = QUEUE_HEAD(&pool_ctx->wait_to_run);

		QUEUE_REMOVE(q);
		QUEUE_INIT(q);  /* Signal uv_cancel() that the work req is executing. */

		pthread_mutex_unlock(&pool_ctx->mutex);
		to_run = QUEUE_DATA(q, struct _work, queue);

		pthread_mutex_lock(&to_run->mutex);
		to_run->thread_id = t_msg->thread_id;
		CLR_FLAG(to_run->flag, FLAG_WORK_DONE);
		SET_FLAG(to_run->flag, FLAG_WORK_RUN);
		pthread_mutex_unlock(&to_run->mutex);

		to_run->loop(to_run->usr_ctx);
		pthread_mutex_lock(&to_run->mutex);
		to_run->thread_id = 0;
		if (IS_SET(to_run->flag, FLAG_WORK_WAIT)) {
			CLR_FLAG(to_run->flag, FLAG_WORK_RUN);
			SET_FLAG(to_run->flag, FLAG_WORK_DONE);
			pthread_cond_signal(&to_run->cond);
			pthread_mutex_unlock(&to_run->mutex);
		}else{
			pthread_mutex_unlock(&to_run->mutex); // *主动释放
			pthread_cond_destroy(&to_run->cond);
			pthread_mutex_destroy(&to_run->mutex);
			free(to_run);
		}

		pthread_mutex_lock(&pool_ctx->mutex);

	}
	TASK_CLR_ACTIVE(t_msg->flag);
	pthread_mutex_unlock(&pool_ctx->mutex);
	TP_LOG_NOTICE("work thread[%lu] exit success.", t_msg->thread_id);
	return NULL;
}


tp_handle tp_create_thread_pool(struct tp_param *p) 
{
	uint32_t i;
	struct _thread_pool_msg *pool = NULL;
	uint32_t thread_num = 5;

	if (p) {
		thread_num = (p->thread_max_num > 0)?p->thread_max_num:thread_num;
	}
	pool = (struct _thread_pool_msg *)calloc(1, sizeof(struct _thread_pool_msg) + thread_num * sizeof(struct _thread_msg));
	LOG_THEN_RETURN_VAL_IF_TRUE((!pool), NULL, "pool calloc fail.");

	pool->thread_num = thread_num;
	pool->cpu_max_num = get_nprocs();
	TP_LOG_NOTICE("Get machine CPU num[%u].", pool->cpu_max_num);
	if (p){
		pool->cpu_max_num = (p->cpu_max_num >= 4)?p->cpu_max_num:pool->cpu_max_num;
	}
	TP_LOG_NOTICE("Set thread bind CPU num[%u].", pool->cpu_max_num);

	pthread_mutex_init(&pool->mutex, NULL); /* 初始化互斥锁 */
	pthread_cond_init(&pool->cond, NULL);	 /* 初始化条件变量 */

	QUEUE_INIT(&pool->wait_to_run);

	pool->thread = (struct _thread_msg*)pool->ext_data;
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!pool->thread), error_1, "pool->thread calloc fail.");
	
	pool->sync = (sem_t *)calloc(1, sizeof(sem_t));
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!pool->thread), error_1, "pool->sync calloc fail.");

	if(sem_init(pool->sync, 0, 0) < 0) {
		TP_LOG_ERROR("sem_init fail, to free pool.");
        goto error_2;
	}
	TP_LOG_NOTICE("create thread num:%u.", p->thread_max_num);
	for (i = 0; i < p->thread_max_num; i++){
		if (pthread_create(&pool->thread[i].thread_id, NULL, task_worker, pool) < 0) {
			TP_LOG_ERROR("create thread fail, to free pool.");
			pool->thread[i].thread_id = 0;
			goto error_3;
		}
		sem_wait(pool->sync);
		TP_LOG_NOTICE("create thread[%lu] success, do next.", pool->thread[i].thread_id);
	}
	if(pool->sync)
		free(pool->sync);
	pool->sync = NULL;

	return pool;
error_3:
	pthread_mutex_lock(&pool->mutex);
	for (i = 0; i < p->thread_max_num; i++){
		TASK_SET_EXIT(pool->thread[i].flag);
		TP_LOG_DEBUG("notify thread[%lu] exit, wait.", pool->thread[i].thread_id);
	}
	pthread_cond_broadcast(&pool->cond);
	pthread_mutex_unlock(&pool->mutex);
	for (i = 0; i < pool->thread_num; i++){
		TP_LOG_DEBUG("join thread[%lu] exit, do next.", pool->thread[i].thread_id);
		if (pool->thread[i].thread_id > 0)
			pthread_join(pool->thread[i].thread_id, NULL);
	}
error_2:
	if(pool->sync)
		free(pool->sync);
	pool->sync = NULL;
error_1:
	pthread_cond_destroy(&pool->cond);
	pthread_mutex_destroy(&pool->mutex);
	if (pool)
		free(pool);
	return NULL;
}

static inline void tp_usleep(uint64_t us)
{
	struct timeval time;
	if(!us){
		return;
	}
	time.tv_sec = 0;
	time.tv_usec = us;
	select(0, NULL, NULL, NULL, &time);
	return;
}

int tp_destroy_thread_pool(tp_handle *fd)
{
	uint32_t i;
	int retry = 3;
	struct _thread_pool_msg *pool = (struct _thread_pool_msg *)(*fd);
	LOG_THEN_RETURN_VAL_IF_TRUE((!pool), -1,"pool null fail.");

	pthread_mutex_lock(&pool->mutex);
wait_idle:
	for (i = 0; i < pool->thread_num; i++){
		if (!(IS_SET(pool->thread[i].flag, FLAG_TASK_IDLE))){
			TP_LOG_ERROR("the thread[%lu], work_id[%u] is runing,can't stop it", pool->thread[i].thread_id, i);
			if (retry) {
				retry--;
				pthread_mutex_unlock(&pool->mutex);
				tp_usleep(10*1000);
				pthread_mutex_lock(&pool->mutex);
				goto wait_idle;
			}else{
				goto error;
			}
		}
	}
	for (i = 0; i < pool->thread_num; i++){
		TASK_SET_EXIT(pool->thread[i].flag);
		TP_LOG_DEBUG("notify thread[%lu] exit, wait.", pool->thread[i].thread_id);
	}

	pthread_cond_broadcast(&pool->cond);
	pthread_mutex_unlock(&pool->mutex);

	for (i = 0; i < pool->thread_num; i++){
		TP_LOG_DEBUG("join[%u] thread[%lu] exit, do next.", i, pool->thread[i].thread_id);
		if (pool->thread[i].thread_id > 0)
			pthread_join(pool->thread[i].thread_id, NULL);
	}

	pthread_cond_destroy(&pool->cond);
	pthread_mutex_destroy(&pool->mutex);
	if(pool)
		free(pool);
	TP_LOG_DEBUG(" free pool success, exit.");
	*fd = NULL;
	return 0;
error:
  	pthread_mutex_unlock(&pool->mutex);
  	return -1;
}

work_handle_t tp_post_one_work(tp_handle fd, struct tp_thread_work *w, uint8_t auto_free)
{
  struct _thread_pool_msg *pool = (struct _thread_pool_msg *)fd;
  struct _work *work;
  int ret;
  LOG_THEN_RETURN_VAL_IF_TRUE((!pool || !w), NULL,"pool null or w null fail.");
  LOG_THEN_RETURN_VAL_IF_TRUE(!w->loop, NULL, "work loop null, fail.");
  
  work = (struct _work*)calloc(1, sizeof(struct _work));
  work->loop = w->loop;
  work->stop = w->stop;
  work->usr_ctx = w->usr_ctx;
  work->thread_id = 0;
  ret = pthread_mutex_init(&work->mutex, NULL); /* 初始化互斥锁 */
  LOG_THEN_GOTO_TAG_IF_VAL_TRUE((ret != 0), error, "pthread_mutex_init fail.");
  ret = pthread_cond_init(&work->cond, NULL);	 /* 初始化条件变量 */
  if (ret != 0) {
    pthread_cond_destroy(&work->cond);
    goto error;
  }
  SET_FLAG(work->flag, FLAG_WORK_INIT);
  if (auto_free){
    CLR_FLAG(work->flag, FLAG_WORK_WAIT);
  }
  pthread_mutex_lock(&pool->mutex);
  QUEUE_INSERT_TAIL(&pool->wait_to_run, &work->queue);
  if (pool->idle_num > 0){
    	pthread_cond_signal(&pool->cond);
  }else{
	  TP_LOG_NOTICE("no idle thread to process task, idle:%u, total:%u.", pool->idle_num, pool->thread_num);
  }
  pthread_mutex_unlock(&pool->mutex);
  return work;
error:
  if (work)
    free(work);
  work = NULL;

  return NULL;
}

int tp_wait_work_done(work_handle_t *w, uint32_t timeout_ms)
{
	struct _work *work = (struct _work *)(*w);
	struct timespec abstime;
	struct timeval now;
	uint64_t nsec;
	LOG_THEN_RETURN_VAL_IF_TRUE((!w || !work || (work == (void*)HIDE_ADDR)), -1, "work_handle_t  fail.");
	pthread_mutex_lock(&work->mutex);
	if (IS_SET(work->flag, FLAG_WORK_DONE)){
		goto end;
	}

	SET_FLAG(work->flag, FLAG_WORK_WAIT);
	if (timeout_ms > 0) {
		gettimeofday(&now, NULL);	// 线程安全
		nsec = now.tv_usec * 1000 + (timeout_ms % 1000) * 1000000;
		abstime.tv_sec=now.tv_sec + nsec / 1000000000 + timeout_ms / 1000;
		abstime.tv_nsec=nsec % 1000000000;
		pthread_cond_timedwait(&work->cond, &work->mutex, &abstime);
	}else{
		pthread_cond_wait(&work->cond, &work->mutex);
	}

	if (!IS_SET(work->flag, FLAG_WORK_DONE)){
		TP_LOG_ERROR("the work wait fail.");
		CLR_FLAG(work->flag, FLAG_WORK_WAIT);
		pthread_mutex_unlock(&work->mutex);
		return -1;
	}

	pthread_mutex_unlock(&work->mutex);
end:
	if (IS_SET(work->flag, FLAG_WORK_INIT)) {
		pthread_cond_destroy(&work->cond);
		pthread_mutex_destroy(&work->mutex);
	}
	if (work)
		free(work);
	*w = NULL;
	return 0;
}

int tp_cancel_one_work(work_handle_t *w)
{
	struct _work *work = (struct _work *)(*w);
	LOG_THEN_RETURN_VAL_IF_TRUE((!work || (work==(void*)HIDE_ADDR)), -1, "work_handle_t  fail.");
	pthread_mutex_lock(&work->mutex);
	if (!work->stop){
		pthread_mutex_unlock(&work->mutex);
		return -1;
	}

	if (IS_SET(work->flag, FLAG_WORK_DONE)){
		goto end;
	}
	SET_FLAG(work->flag, FLAG_WORK_WAIT);
	work->stop(work->usr_ctx);
	pthread_cond_wait(&work->cond, &work->mutex);
end:
	pthread_mutex_unlock(&work->mutex);
	if (IS_SET(work->flag, FLAG_WORK_INIT)) {
		pthread_cond_destroy(&work->cond);
		pthread_mutex_destroy(&work->mutex);
	}
	if (*w)
		free(*w);
	*w = NULL;
	return 0;
}

uint64_t tp_get_work_thread_id(work_handle_t w)
{
	struct _work *work = (struct _work *)w;
	if(work){
		return (uint64_t)work->thread_id;
	}
	return 0;
}

uint32_t tp_get_pool_idle_num(tp_handle fd)
{
	struct _thread_pool_msg *pool = (struct _thread_pool_msg *)fd;
  	int ret;
	uint32_t idle = 0;
  	LOG_THEN_RETURN_VAL_IF_TRUE((!pool), 0,"pool null or w null fail.");
	pthread_mutex_lock(&pool->mutex);
	idle = pool->idle_num;
  	pthread_mutex_unlock(&pool->mutex);
	return idle;
}
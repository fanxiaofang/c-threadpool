/*
 * Copyright (c) 2016, Mathias Brossard <mathias@brossard.org>.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file threadpool.c
 * @brief Threadpool implementation file
 */

#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include "threadpool.h"

/**
 * 线程池两种关闭方式:立即关闭和优雅关闭
 */
typedef enum {
    immediate_shutdown = 1,
    graceful_shutdown  = 2
} threadpool_shutdown_t;

/**
 * 线程池两种状态：运行中和暂停中
 * 暂停线程池不会停止正在执行的任务，只是阻止新任务被工作线程取走执行
 */
typedef enum {
    pool_running = 0,
    pool_paused = 1
} threadpool_state_t;
/**
 *  @struct threadpool_task
 *  @brief the work struct
 *
 *  @var function Pointer to the function that will perform the task.
 *  @var argument Argument to be passed to the function.
 */
/**
 * 任务定义：任务函数、传给给任务函数的参数
 */
typedef struct {
    void (*function)(void *);
    void *argument;
} threadpool_task_t;

/**
 *  @struct threadpool
 *  @brief The threadpool struct
 *
 *  @var notify       Condition variable to notify worker threads.
 *  @var threads      Array containing worker threads ID.
 *  @var thread_count Number of threads
 *  @var queue        Array containing the task queue.
 *  @var queue_size   Size of the task queue.
 *  @var head         Index of the first element.
 *  @var tail         Index of the next element.
 *  @var count        Number of pending tasks
 *  @var shutdown     Flag indicating if the pool is shutting down
 *  @var started      Number of started threads
 */
/**
 * 线程池的结构定义
 *  @var lock         互斥锁，用于保护共享资源
 *  @var notify       条件变量，用于线程之间通知
 *  @var queue_not_full  条件变量，任务队列满时threadpool_add阻塞等待
 *  @var notify_pause 条件变量，线程暂停/恢复通知
 *  @var threads      数组头指针，数组用来存储所有线程的线程ID
 *  @var thread_count 线程数量
 *  @var queue        任务队列(环形缓冲区)，存储任务的数组
 *  @var queue_size   任务队列的大小
 *  @var head         任务队列的首个任务索引
 *  @var tail         任务队列中最后一个任务的下一个索引位置（任务队列是数组结构，这里head tail指的是数组索引
 *  @var count        任务队列中等待处理的任务数
 *  @var shutdown     线程池是否关闭的状态变量
 *  @var started      开始做任务的线程数
 *  @var state        线程池状态（运行or暂停）
 *  @var paused_threads      当前暂停的线程数量（用于管理暂停
 * 
 */
struct threadpool_t {
  pthread_mutex_t lock;
  pthread_cond_t notify;
  pthread_cond_t queue_not_full;
  pthread_cond_t notify_pause;

  pthread_t *threads;
  threadpool_task_t *queue;

  int thread_count;
  int queue_size;
  int head;
  int tail;
  int count;
  int started;

  threadpool_shutdown_t shutdown;
 
  threadpool_state_t state;
  int paused_threads;
};

/**
 * @function void *threadpool_thread(void *threadpool)
 * @brief the worker thread
 * @param threadpool the pool which own the thread
 */
static void *threadpool_thread(void *threadpool);

int threadpool_free(threadpool_t *pool);

/**
 * 创建线程池
 */
threadpool_t *threadpool_create(int thread_count, int queue_size, int flags)
{
    threadpool_t *pool;
    int i;
    (void) flags;

    if(thread_count <= 0 || thread_count > MAX_THREADS || queue_size <= 0 || queue_size > MAX_QUEUE) {
        return NULL;
    }

    /* 创建线程池对象 */
    if((pool = (threadpool_t *)malloc(sizeof(threadpool_t))) == NULL) {
        goto err;
    }

    /* Initialize */
    pool->thread_count = 0;
    pool->queue_size = queue_size;
    pool->head = pool->tail = pool->count = 0;
    pool->shutdown = pool->started = 0;

    /* Allocate thread and task queue */
    /* 为线程池中的线程数组和任务队列数组申请内存 */
    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * thread_count);
    pool->queue = (threadpool_task_t *)malloc
        (sizeof(threadpool_task_t) * queue_size);

    /* Initialize mutex and conditional variable first */
    /* 初始化互斥锁 条件变量 */
    if((pthread_mutex_init(&(pool->lock), NULL) != 0) ||
       (pthread_cond_init(&(pool->notify), NULL) != 0) ||
       (pthread_cond_init(&(pool->queue_not_full), NULL) != 0) ||
       (pthread_cond_init(&(pool->notify_pause), NULL) != 0) ||
       (pool->threads == NULL) ||
       (pool->queue == NULL)) {
        goto err;
    }

    /* Start worker threads */
    /* 创建指定数量的线程 */
    for(i = 0; i < thread_count; i++) {
        if(pthread_create(&(pool->threads[i]), NULL,
                          threadpool_thread, (void*)pool) != 0) {
            threadpool_destroy(pool, 0);
            return NULL;
        }
        pool->thread_count++;
        pool->started++;
    }

    /* 返回创建好的线程池 */
    return pool;

 err:
    if(pool) {
        threadpool_free(pool);
    }
    return NULL;
}

int threadpool_add(threadpool_t *pool, void (*function)(void *),
                   void *argument, int flags)
{
    int err = 0;
    int next;
    (void) flags;

    if(pool == NULL || function == NULL) {
        return threadpool_invalid;
    }

    /* 往任务队列中添加任务时，先获取互斥锁保护共享资源 */
    if(pthread_mutex_lock(&(pool->lock)) != 0) {
        return threadpool_lock_failure;
    }

    /* 计算存储新任务的数组索引：获取当前任务队列的最后一个任务的下一个空闲位置索引 */
    next = (pool->tail + 1) % pool->queue_size;

    /* 任务队列满 且线程池不是关闭状态时 阻塞等待*/
    while ((pool->count == pool->queue_size) && (!pool->shutdown)) {
        /* 阻塞 等待任务队列有空位通知*/
        // printf("---- queue is full, im waiting ---\n");
        pthread_cond_wait(&(pool->queue_not_full), &(pool->lock));
        // printf("---- queue is not full ---\n");
    }

    do {
        /* Are we shutting down ? */
        if(pool->shutdown) {
            err = threadpool_shutdown;
            break;
        }

        /* Add task to queue */
        /* 添加任务到队列中 */
        pool->queue[pool->tail].function = function;
        pool->queue[pool->tail].argument = argument;
        /* 更新 tail 和 count */
        pool->tail = next;
        pool->count += 1;

        /* pthread_cond_broadcast */
        /* 通知等待在条件变量notify的线程 有新任务*/
        if(pthread_cond_signal(&(pool->notify)) != 0) {
            err = threadpool_lock_failure;
            break;
        }
    } while(0);
    /**
     * 使用do{}while(0)结构，为了能够在发生异常时 使用break跳出代码块
     */

    if(pthread_mutex_unlock(&pool->lock) != 0) {
        err = threadpool_lock_failure;
    }

    return err;
}

/**
 * 关闭线程池，等待所有线程退出
 */
int threadpool_destroy(threadpool_t *pool, int flags)
{
    if(pool == NULL) {
        return threadpool_invalid;
    }

    /* 销毁线程时，加互斥锁保护*/
    if(pthread_mutex_lock(&(pool->lock)) != 0) {
        return threadpool_lock_failure;
    }

    /* Already shutting down */
    /* 线程池是否已经被关闭 */
    if(pool->shutdown) {
        pthread_mutex_unlock(&(pool->lock));
        return threadpool_shutdown;
    }

    /* 获取关闭线程池的方式：优雅or立刻 */
    pool->shutdown = (flags & threadpool_graceful) ?
        graceful_shutdown : immediate_shutdown;

    /* Wake up all worker threads */
    /** 
     * 唤醒所有等待条件变量的线程
     * 原因是，线程阻塞在等待任务队列有任务的地方
     * 这里需要唤醒线程，让其跳过继续wait的部分，等于是通知线程线程池要关闭了
     * pthread_cond_broadcast执行错误的可能性很小，所以无需判断返回值
    */
    pthread_cond_broadcast(&(pool->notify));
    pthread_cond_broadcast(&(pool->queue_not_full));
    pthread_cond_broadcast(&(pool->notify_pause));

    pthread_mutex_unlock(&(pool->lock));

    /* Join all worker thread */
    /* pthread_join 等待所有线程结束后释放线程资源*/
    for(int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    /* 释放线程池资源*/
    return threadpool_free(pool);
}

/**
 * 释放销毁线程池资源（内存、锁、条件变量等）
 */
int threadpool_free(threadpool_t *pool)
{
    /* 确保没有线程在运行，pool->started = 0*/
    if(pool == NULL || pool->started > 0) {
        return -1;
    }

    /* Did we manage to allocate ? */
    if(pool->threads) {
        free(pool->threads);
        pool->threads = NULL;
    }

    if (pool->queue) {
        free(pool->queue);
        pool->queue = NULL;
    }

    /* 安全销毁互斥锁 */
    if (pthread_mutex_destroy(&(pool->lock)) != 0) {
        return -1;
    }

    /* 安全销毁条件变量，确保没有线程等待该条件变量 */
    if (pthread_cond_destroy(&(pool->notify)) != 0) {
        return -1;
    }

    /* 安全销毁条件变量，确保没有线程等待该条件变量 */
    if (pthread_cond_destroy(&(pool->queue_not_full)) != 0) {
        return -1;
    }

    /* 安全销毁条件变量，确保没有线程等待该条件变量 */
    if (pthread_cond_destroy(&(pool->notify_pause)) != 0) {
        return -1;
    }
 
    // /* Because we allocate pool->threads after initializing the
    //    mutex and condition variable, we're sure they're
    //    initialized. Let's lock the mutex just in case. */
    // /**
    //  *加锁销毁锁不可取，销毁互斥锁时，锁必须处于未锁定状态，且没有其它线程尝试操作该锁
    //  * */
    // pthread_mutex_lock(&(pool->lock));
    // pthread_mutex_destroy(&(pool->lock));
    // pthread_cond_destroy(&(pool->notify));

    free(pool);    
    return 0;
}


static void *threadpool_thread(void *threadpool)
{
    threadpool_t *pool = (threadpool_t *)threadpool;
    threadpool_task_t task;

    for(;;) {
        /* Lock must be taken to wait on conditional variable */
        /* 先获取互斥锁，确保同一时间只能有一个线程操作任务队列 */
        pthread_mutex_lock(&(pool->lock));

        /* Wait on condition variable, check for spurious wakeups.
           When returning from pthread_cond_wait(), we own the lock. */
        /* 使用while 确保线程被唤醒后重新判断条件是否满足 */
        while((pool->count == 0) && (!pool->shutdown)) {
            /* 任务队列为空时，在此条件变量上阻塞等待唤醒 */
            pthread_cond_wait(&(pool->notify), &(pool->lock));
        }

        /* 线程暂停处理 */
        while ((pool->state == pool_paused) && (!pool->shutdown)) {
            pool->paused_threads++;
            pthread_cond_wait(&(pool->notify_pause), &(pool->lock));
            pool->paused_threads--;
        }

        /* 当需要立即关闭线程池 或者 优雅关闭线程池且任务队列中没有任务 时*/
        if((pool->shutdown == immediate_shutdown) ||
           ((pool->shutdown == graceful_shutdown) &&
            (pool->count == 0))) {
            pool->started--;
            pthread_mutex_unlock(&(pool->lock));
            break;
        }

        /* Grab our task */
        /* 从任务队列中 取出当前head位置的任务，并将head向后移动一个索引位置*/
        task.function = pool->queue[pool->head].function;
        task.argument = pool->queue[pool->head].argument;
        pool->head = (pool->head + 1) % pool->queue_size;
        /* 如果head移动到了任务队列的末尾，则从任务队列头部重新开始*/
        pool->count -= 1;

        /* 任务队列空出位置，通知等待空位的的任务生产者*/
        pthread_cond_signal(&(pool->queue_not_full));

        /* Unlock */
        pthread_mutex_unlock(&(pool->lock));

        /* Get to work */
        /* 运行任务函数 */
        (*(task.function))(task.argument);
        /* 任务运行完毕 */
    }
 
    pthread_exit(NULL);
    return(NULL);
}

void threadpool_pause(threadpool_t* pool)
{
    pthread_mutex_lock(&pool->lock);
    pool->state = pool_paused;
    pthread_mutex_unlock(&pool->lock);
}

void threadpool_resume(threadpool_t *pool)
{
    pthread_mutex_lock(&pool->lock);
    pool->state = pool_running;
    pthread_cond_broadcast(&pool->notify_pause);
    pthread_mutex_unlock(&pool->lock);
}
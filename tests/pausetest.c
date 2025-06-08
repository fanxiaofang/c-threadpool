#define THREAD 5
#define QUEUE  16

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

#include "threadpool.h"

#define TASK_NUM 32
pthread_mutex_t lock;


void dummy_task(void *arg) {
    int num = *(int*)arg;
    printf("[Task %d] 开始执行线程：%lu\n", num, pthread_self());
    sleep(2); /* 模拟执行任务耗时 */
    printf("[Task %d] 执行完成线程：%lu\n", num, pthread_self());
}

int main(int argc, char **argv)
{
    threadpool_t *pool;

    assert((pool = threadpool_create(THREAD, QUEUE, 0)) != NULL);
    fprintf(stderr, "Pool started with %d threads and "
            "queue size of %d\n", THREAD, QUEUE);

    int task_num[TASK_NUM];
    for (int i = 0; i < TASK_NUM; i++) {
        task_num[i] = i + 1;
        if (threadpool_add(pool, &dummy_task, &task_num[i], 0) != 0) {
            fprintf(stderr, "任务 %d 添加失败\n", i + 1);
        }
    }

    printf(" 暂停线程池, 线程将停止取新任务\n");
    threadpool_pause(pool);

    printf(" wait 10s , 观察任务是否暂停\n");
    sleep(10);

    printf(" 恢复线程池，继续执行剩余任务... \n");
    threadpool_resume(pool);

    /* 等待所有任务执行完 */
    sleep(10);

    assert(threadpool_destroy(pool, 0) == 0);
    fprintf(stderr, "done\n");

    return 0;
}

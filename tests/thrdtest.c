#define THREAD 32
#define QUEUE  256

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

#include "threadpool.h"

int tasks = 0, done = 0, max_tasks = 512;
pthread_mutex_t lock;

void dummy_task(void *arg) {
    // usleep(10000);
    /* 增加任务函数中sleep时长，为了模拟出任务队列满出现阻塞等待的情况 */
    sleep(5);
    pthread_mutex_lock(&lock);
    done++;
    pthread_mutex_unlock(&lock);
}

int main(int argc, char **argv)
{
    threadpool_t *pool;

    pthread_mutex_init(&lock, NULL);

    assert((pool = threadpool_create(THREAD, QUEUE, 0)) != NULL);
    fprintf(stderr, "Pool started with %d threads and "
            "queue size of %d\n", THREAD, QUEUE);

    for (int i = 0; i < max_tasks; i++) {
        if (threadpool_add(pool, &dummy_task, NULL, 0) == 0) {
            pthread_mutex_lock(&lock);
            tasks++;
            printf(" tasks:%d\n", tasks);
            pthread_mutex_unlock(&lock);
        }
    }

    fprintf(stderr, "Added %d tasks\n", tasks);

    while((tasks / 2) > done) {
        usleep(10000);
    }
    assert(threadpool_destroy(pool, 0) == 0);
    fprintf(stderr, "Did %d tasks\n", done);

    return 0;
}

#ifndef WORKER_H
#define WORKER_H

/* 每个工作线程处理的请求数量 */
#define WORKER_REQUESTS_PER_THREAD 5

/*
 * 工作线程参数结构体，通过 pthread_create() 传入
 */
typedef struct {
    int thread_id;  /* 线程编号（1-based），用于日志标识 */
    int req_start;  /* 本线程负责处理的起始请求 ID */
} worker_args_t;

int  worker_module_init(void);
void *worker_thread(void *arg);

#endif /* WORKER_H */

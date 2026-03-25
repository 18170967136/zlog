/*
 * worker.c — 工作线程
 *
 * 演示要点：
 *   1. 多个线程共享同一个 zlog_category_t*（worker 分类），完全线程安全。
 *   2. 每条请求在处理入口调用 zlog_put_mdc("request_id", ...) 设置线程上下文。
 *      MDC 存储在线程本地存储（TLS）中，不同线程的 MDC 值互不干扰。
 *   3. 请求处理链中所有模块（network.c、database.c）打印的日志行都会自动
 *      包含本线程设置的 request_id，形成完整的请求追踪日志，无需层层传参。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zlog.h"
#include "worker.h"
#include "network.h"
#include "database.h"

/* 对应 zlog.conf 中 "worker.*" 规则，所有 worker 线程共享此句柄 */
static zlog_category_t *wk_cat;

int worker_module_init(void)
{
    wk_cat = zlog_get_category("worker");
    if (!wk_cat) {
        fprintf(stderr, "[worker] zlog_get_category(\"worker\") failed\n");
        return -1;
    }
    return 0;
}

void *worker_thread(void *arg)
{
    worker_args_t *wa = (worker_args_t *)arg;
    char req_id_str[32];
    char payload[128];
    int i;

    zlog_info(wk_cat, "worker-%d started, will handle req [%d..%d]",
              wa->thread_id,
              wa->req_start,
              wa->req_start + WORKER_REQUESTS_PER_THREAD - 1);

    for (i = 0; i < WORKER_REQUESTS_PER_THREAD; i++) {
        int req_id = wa->req_start + i;

        /* ---------------------------------------------------------------- *
         * MDC 核心用法：在请求处理入口设置线程上下文                             *
         *                                                                    *
         * zlog_put_mdc(key, value) 将键值对写入当前线程的 TLS，               *
         * 后续在本线程执行的所有 zlog_* 调用（含 network.c / database.c 内部） *
         * 都会将该值嵌入日志行（通过配置文件中的 %M(request_id) 格式字符）。    *
         *                                                                    *
         * 不同线程的 MDC 完全隔离，线程 A 设置的值不会出现在线程 B 的日志中。   *
         * ---------------------------------------------------------------- */
        snprintf(req_id_str, sizeof(req_id_str), "REQ-%04d", req_id);
        zlog_put_mdc("request_id", req_id_str);

        zlog_info(wk_cat, "---- begin %s ----", req_id_str);

        /* 步骤1：从网络接收数据（network.c 内部的日志也会携带 request_id） */
        if (network_receive(req_id, payload, sizeof(payload)) != 0) {
            zlog_error(wk_cat, "network_receive failed for %s", req_id_str);
            continue;
        }

        /* 步骤2：保存到数据库（database.c 内部的日志也会携带 request_id） */
        if (database_save(req_id, payload) != 0) {
            zlog_error(wk_cat, "database_save failed for %s", req_id_str);
            continue;
        }

        zlog_info(wk_cat, "---- end %s (ok) ----", req_id_str);
    }

    /*
     * 请求处理完毕后清理本线程的 MDC（推荐做法）。
     * 即使不显式清理，线程退出时 TLS 析构函数也会自动释放 MDC 资源。
     */
    zlog_clean_mdc();

    zlog_info(wk_cat, "worker-%d finished", wa->thread_id);
    return NULL;
}

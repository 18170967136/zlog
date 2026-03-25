/*
 * main.c — 程序入口
 *
 * 多线程多源文件使用 zlog 的完整流程：
 *
 *   1. mkdir logs/          — 提前创建日志目录（zlog 不会自动创建目录）
 *   2. zlog_init(conf)      — 全局初始化，整个进程只调用一次，在主线程完成
 *   3. zlog_get_category()  — 各模块获取自己的分类句柄，在启动子线程前完成
 *   4. pthread_create()     — 启动工作线程，线程内直接调用 zlog_* 宏即可
 *   5. pthread_join()       — 等待所有线程结束
 *   6. zlog_fini()          — 全局清理，整个进程只调用一次，在所有线程结束后
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "zlog.h"
#include "network.h"
#include "database.h"
#include "worker.h"

#define NUM_THREADS 4
#define CONF_FILE   "zlog.conf"

/* main 模块自己的日志分类，对应 zlog.conf 中 "main.*" 规则 */
static zlog_category_t *main_cat;

int main(void)
{
    int           rc;
    int           i;
    pthread_t     tids[NUM_THREADS];
    worker_args_t wargs[NUM_THREADS];

    /* ------------------------------------------------------------------
     * 步骤1：提前创建日志目录
     * zlog 在写文件时不会自动创建目录，必须提前手动创建。
     * ------------------------------------------------------------------ */
    if (mkdir("./logs", 0755) != 0) {
        /* EEXIST 表示目录已存在，是正常情况，不视为错误 */
    }

    /* ------------------------------------------------------------------
     * 步骤2：全局初始化 zlog
     * 整个进程只调用一次，在所有 zlog_* 调用之前完成。
     * zlog_init() 内部加写锁，本身是线程安全的，但推荐在主线程单次调用。
     * ------------------------------------------------------------------ */
    rc = zlog_init(CONF_FILE);
    if (rc) {
        fprintf(stderr, "zlog_init(\"%s\") failed, rc=%d\n", CONF_FILE, rc);
        return EXIT_FAILURE;
    }

    /* ------------------------------------------------------------------
     * 步骤3：获取 main 模块的日志分类
     * ------------------------------------------------------------------ */
    main_cat = zlog_get_category("main");
    if (!main_cat) {
        fprintf(stderr, "zlog_get_category(\"main\") failed\n");
        zlog_fini();
        return EXIT_FAILURE;
    }

    zlog_info(main_cat, "=== application starting, zlog version: %s ===",
              zlog_version());

    /* ------------------------------------------------------------------
     * 步骤4：在启动工作线程之前，完成所有模块的初始化
     *
     * 建议在主线程中依次调用各模块的 init 函数，原因：
     *   - 避免多个工作线程同时首次调用 zlog_get_category()，
     *     虽然 zlog 内部加了写锁保证安全，但集中在主线程初始化更清晰。
     *   - 若某模块初始化失败，可以在启动线程前及时报错退出。
     * ------------------------------------------------------------------ */
    if (network_module_init() != 0) {
        zlog_fatal(main_cat, "network_module_init failed");
        zlog_fini();
        return EXIT_FAILURE;
    }
    if (database_module_init() != 0) {
        zlog_fatal(main_cat, "database_module_init failed");
        zlog_fini();
        return EXIT_FAILURE;
    }
    if (worker_module_init() != 0) {
        zlog_fatal(main_cat, "worker_module_init failed");
        zlog_fini();
        return EXIT_FAILURE;
    }

    zlog_info(main_cat, "all modules initialized, spawning %d worker threads",
              NUM_THREADS);

    /* ------------------------------------------------------------------
     * 步骤5：启动工作线程
     *
     * 线程内可以直接调用任何模块的日志函数，zlog 保证线程安全：
     *   - 全局配置由读写锁保护
     *   - 每个线程拥有独立的消息缓冲区（TLS），格式化不存在竞争
     * ------------------------------------------------------------------ */
    for (i = 0; i < NUM_THREADS; i++) {
        wargs[i].thread_id = i + 1;
        wargs[i].req_start = i * WORKER_REQUESTS_PER_THREAD + 1;

        rc = pthread_create(&tids[i], NULL, worker_thread, &wargs[i]);
        if (rc != 0) {
            zlog_fatal(main_cat, "pthread_create failed for thread %d, rc=%d",
                       i + 1, rc);
            zlog_fini();
            return EXIT_FAILURE;
        }
        zlog_debug(main_cat, "spawned worker-%d, req_start=%d",
                   i + 1, wargs[i].req_start);
    }

    /* ------------------------------------------------------------------
     * 步骤6：等待所有工作线程结束
     * ------------------------------------------------------------------ */
    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(tids[i], NULL);
        zlog_debug(main_cat, "worker-%d joined", i + 1);
    }

    zlog_info(main_cat, "all %d worker threads finished", NUM_THREADS);

    /* ------------------------------------------------------------------
     * 步骤7：各模块清理（可选，进程即将退出时也可省略）
     * ------------------------------------------------------------------ */
    network_module_fini();
    database_module_fini();

    zlog_info(main_cat, "=== application exiting ===");

    /* ------------------------------------------------------------------
     * 步骤8：全局清理 zlog
     * 必须在所有工作线程结束（pthread_join）之后调用。
     * 若在线程仍在运行时调用 zlog_fini()，可能导致未定义行为。
     * ------------------------------------------------------------------ */
    zlog_fini();

    return EXIT_SUCCESS;
}

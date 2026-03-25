/*
 * database.c — 数据库模块
 *
 * 与 network.c 相同的模式：
 *   每个源文件持有自己的 static zlog_category_t*，
 *   对应不同的日志分类，日志可以路由到不同的输出目标。
 */

#include <stdio.h>
#include <unistd.h>

#include "zlog.h"
#include "database.h"

/* 对应 zlog.conf 中 "database.*" 规则 */
static zlog_category_t *db_cat;

int database_module_init(void)
{
    db_cat = zlog_get_category("database");
    if (!db_cat) {
        fprintf(stderr, "[database] zlog_get_category(\"database\") failed\n");
        return -1;
    }
    zlog_info(db_cat, "database module initialized");
    return 0;
}

int database_save(int req_id, const char *data)
{
    /*
     * MDC 中的 request_id 由调用线程（worker_thread）在请求入口处设置，
     * 此处打印的日志会自动携带该上下文，无需手动传递请求 ID 到日志参数。
     */
    zlog_debug(db_cat, "saving to db, req=%d data=[%s]", req_id, data);

    /* 模拟数据库写入延迟（800 µs） */
    usleep(800);

    zlog_info(db_cat, "save ok, req=%d", req_id);
    return 0;
}

void database_module_fini(void)
{
    zlog_info(db_cat, "database module shutting down");
}

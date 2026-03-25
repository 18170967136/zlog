#ifndef DATABASE_H
#define DATABASE_H

/*
 * 数据库模块（database.c）
 *
 * 使用模式说明：
 *   - 本模块维护自己的 zlog 日志分类 "database"，与其他模块完全独立。
 *   - 调用任何 database_* 函数之前，必须先完成：
 *       1. zlog_init()             — 在 main() 中调用一次
 *       2. database_module_init()  — 在启动工作线程之前调用一次
 */

int  database_module_init(void);
int  database_save(int req_id, const char *data);
void database_module_fini(void);

#endif /* DATABASE_H */

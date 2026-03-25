/*
 * modular_mt_demo.c - 多线程多模块分散加载配置示例
 *
 * 功能演示：
 *   1. 每个模块在独立线程中加载自己的日志配置
 *   2. 多个线程并发注册，互不影响
 *   3. 重复加载同一模块时自动覆盖旧配置
 *   4. 使用 zlog_modular.h 封装层，不修改 zlog 源码
 *
 * 架构：
 *   main()
 *     ├── zlog_init_from_string(基础配置)
 *     ├── zlog_mod_init(全局配置)
 *     ├── 启动 N 个线程，每个线程模拟一个模块初始化
 *     │     ├── zlog_mod_register(模块名, 格式, 规则)
 *     │     ├── zlog_get_category(模块名)
 *     │     └── zlog_info(cat, "模块已加载")
 *     ├── 等待所有线程完成
 *     ├── 演示重复加载（覆盖）
 *     ├── 演示模块卸载
 *     ├── zlog_mod_fini()
 *     └── zlog_fini()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "zlog.h"
#include "zlog_modular.h"

/* ========== 模块配置定义 ========== */

typedef struct {
    const char *module_name;
    const char *format_name;
    const char *format_pattern;
    const char *log_level;
    int         thread_id;
} module_thread_arg_t;

/* ========== 线程函数：模拟模块初始化 ========== */

static void *module_init_thread(void *arg)
{
    module_thread_arg_t *marg = (module_thread_arg_t *)arg;
    int rc;
    char format_line[512];
    char rule_line[512];

    printf("[Thread %d] 开始加载模块 '%s' ...\n",
           marg->thread_id, marg->module_name);

    /* 构建格式行（配置文件格式） */
    snprintf(format_line, sizeof(format_line),
             "%s = \"%s\"", marg->format_name, marg->format_pattern);

    /* 构建规则行 */
    snprintf(rule_line, sizeof(rule_line),
             "%s.%s >stdout; %s",
             marg->module_name, marg->log_level, marg->format_name);

    const char *formats[] = { format_line };
    const char *rules[]   = { rule_line };

    /* 检查是否已注册 */
    if (zlog_mod_has_module(marg->module_name)) {
        printf("[Thread %d] 模块 '%s' 已存在，将被覆盖\n",
               marg->thread_id, marg->module_name);
    }

    /* 注册模块（线程安全） */
    rc = zlog_mod_register(marg->module_name, formats, 1, rules, 1);
    if (rc) {
        fprintf(stderr, "[Thread %d] 模块 '%s' 注册失败!\n",
                marg->thread_id, marg->module_name);
        return NULL;
    }

    printf("[Thread %d] 模块 '%s' 注册成功 (当前共 %d 个模块)\n",
           marg->thread_id, marg->module_name, zlog_mod_count());

    /* 获取分类并输出日志 */
    zlog_category_t *cat = zlog_get_category(marg->module_name);
    if (cat) {
        zlog_info(cat, "模块 '%s' 初始化完成 (线程 %d)",
                  marg->module_name, marg->thread_id);
    }

    return NULL;
}

/* ========== 演示场景 ========== */

/*
 * 场景 1：多线程并发加载多个模块
 */
static void demo_concurrent_loading(void)
{
    printf("\n=== 场景 1：多线程并发加载 ===\n\n");

    module_thread_arg_t modules[] = {
        { "auth",     "auth_fmt",  "%d(%H:%M:%S) [AUTH]  [%-5V] %m%n", "DEBUG", 0 },
        { "api",      "api_fmt",   "%d(%H:%M:%S) [API]   [%-5V] %m%n", "INFO",  1 },
        { "database", "db_fmt",    "%d(%H:%M:%S) [DB]    [%-5V] %m%n", "WARN",  2 },
        { "cache",    "cache_fmt", "%d(%H:%M:%S) [CACHE] [%-5V] %m%n", "DEBUG", 3 },
        { "payment",  "pay_fmt",   "%d(%H:%M:%S) [PAY]   [%-5V] %m%n", "INFO",  4 },
    };
    int count = sizeof(modules) / sizeof(modules[0]);
    pthread_t threads[5];
    int i;

    /* 启动线程 */
    for (i = 0; i < count; i++) {
        if (pthread_create(&threads[i], NULL, module_init_thread, &modules[i])) {
            fprintf(stderr, "pthread_create failed for thread %d\n", i);
        }
    }

    /* 等待所有线程完成 */
    for (i = 0; i < count; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("\n所有模块加载完成！共 %d 个模块注册\n", zlog_mod_count());

    /* 导出 JSON 查看当前配置状态 */
    printf("\n--- 当前模块配置（JSON 格式） ---\n");
    char *json = zlog_mod_dump_json();
    if (json) {
        printf("%s\n", json);
        free(json);
    }

    /* 测试各模块日志 */
    printf("\n--- 测试各模块日志输出 ---\n");
    zlog_category_t *cat;

    cat = zlog_get_category("auth");
    if (cat) {
        zlog_debug(cat, "认证模块 DEBUG 日志");
        zlog_info(cat, "用户 admin 登录成功");
    }

    cat = zlog_get_category("api");
    if (cat) {
        zlog_info(cat, "API 请求: GET /users");
        zlog_warn(cat, "API 响应较慢: 2.5s");
    }

    cat = zlog_get_category("database");
    if (cat) {
        zlog_info(cat, "数据库 INFO (WARN 级别, 不应显示)");
        zlog_warn(cat, "数据库连接池使用率: 80%%");
    }

    cat = zlog_get_category("cache");
    if (cat) {
        zlog_debug(cat, "缓存命中: key=user:123");
    }

    cat = zlog_get_category("payment");
    if (cat) {
        zlog_info(cat, "支付订单 #12345 处理中");
    }
}

/*
 * 场景 2：重复加载同一模块（覆盖）
 */
static void demo_duplicate_loading(void)
{
    printf("\n=== 场景 2：重复加载模块（自动覆盖） ===\n\n");

    /* auth 模块已经注册了，再次注册应该覆盖 */
    printf("auth 模块已注册? %s\n",
           zlog_mod_has_module("auth") ? "是" : "否");

    /* 用新的配置覆盖 auth 模块 */
    const char *formats[] = {
        "auth_fmt = \"%d(%H:%M:%S) [AUTH-v2] [%-5V] %m%n\""
    };
    const char *rules[] = {
        "auth.WARN >stdout; auth_fmt"  /* 从 DEBUG 升级为 WARN */
    };

    printf("重新注册 auth 模块（级别从 DEBUG 升级为 WARN）...\n");
    int rc = zlog_mod_register("auth", formats, 1, rules, 1);
    if (rc) {
        fprintf(stderr, "重新注册 auth 模块失败\n");
        return;
    }

    printf("重新注册成功！当前共 %d 个模块\n", zlog_mod_count());

    /* 测试新的日志级别 */
    zlog_category_t *cat = zlog_get_category("auth");
    if (cat) {
        zlog_info(cat, "这条 INFO 不应该显示（级别已升级为 WARN）");
        zlog_warn(cat, "auth v2 警告日志（应该显示）");
    }
}

/*
 * 场景 3：卸载模块
 */
static void demo_unregister_module(void)
{
    printf("\n=== 场景 3：卸载模块 ===\n\n");

    printf("卸载前: 共 %d 个模块\n", zlog_mod_count());
    printf("cache 模块已注册? %s\n",
           zlog_mod_has_module("cache") ? "是" : "否");

    int rc = zlog_mod_unregister("cache");
    if (rc) {
        fprintf(stderr, "卸载 cache 模块失败\n");
    } else {
        printf("cache 模块已卸载\n");
    }

    printf("卸载后: 共 %d 个模块\n", zlog_mod_count());
    printf("cache 模块已注册? %s\n",
           zlog_mod_has_module("cache") ? "是" : "否");

    /* 卸载后导出 JSON 查看变化 */
    printf("\n--- 卸载后的模块配置（JSON 格式） ---\n");
    char *json = zlog_mod_dump_json();
    if (json) {
        printf("%s\n", json);
        free(json);
    }
}

/*
 * 场景 4：多线程同时注册和使用日志
 */
typedef struct {
    int thread_id;
    const char *module_name;
    int log_count;
} worker_arg_t;

static void *worker_thread(void *arg)
{
    worker_arg_t *warg = (worker_arg_t *)arg;
    zlog_category_t *cat;
    int i;

    cat = zlog_get_category(warg->module_name);
    if (!cat) {
        fprintf(stderr, "[Worker %d] 获取分类 '%s' 失败\n",
                warg->thread_id, warg->module_name);
        return NULL;
    }

    for (i = 0; i < warg->log_count; i++) {
        zlog_info(cat, "Worker %d 消息 %d/%d",
                  warg->thread_id, i + 1, warg->log_count);
    }

    return NULL;
}

static void demo_concurrent_logging(void)
{
    printf("\n=== 场景 4：多线程并发日志输出 ===\n\n");

    worker_arg_t workers[] = {
        { 0, "auth",     3 },
        { 1, "api",      3 },
        { 2, "database", 3 },
        { 3, "payment",  3 },
    };
    int count = sizeof(workers) / sizeof(workers[0]);
    pthread_t threads[4];
    int i;

    for (i = 0; i < count; i++) {
        if (pthread_create(&threads[i], NULL, worker_thread, &workers[i])) {
            fprintf(stderr, "pthread_create failed for worker %d\n", i);
        }
    }

    for (i = 0; i < count; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("所有工作线程完成\n");
}

/* ========== 主函数 ========== */

int main(void)
{
    int rc;

    /* 基础配置：只包含最小设置 */
    const char *base_config =
        "[global]\n"
        "strict init = false\n"
        "\n"
        "[formats]\n"
        "default = \"%d(%H:%M:%S) [%-5V] %m%n\"\n"
        "\n"
        "[rules]\n"
        "*.WARN >stdout; default\n";

    printf("========================================\n");
    printf("  多线程多模块分散加载配置示例\n");
    printf("========================================\n\n");

    /* Step 1: 初始化 zlog（基础配置） */
    printf("Step 1: 使用基础配置初始化 zlog...\n");
    rc = zlog_init_from_string(base_config);
    if (rc) {
        fprintf(stderr, "zlog_init_from_string failed\n");
        return EXIT_FAILURE;
    }

    /* Step 2: 初始化模块管理器 */
    printf("Step 2: 初始化模块管理器...\n");
    rc = zlog_mod_init("strict init = false");
    if (rc) {
        fprintf(stderr, "zlog_mod_init failed\n");
        zlog_fini();
        return EXIT_FAILURE;
    }

    /* 场景演示 */
    demo_concurrent_loading();
    demo_duplicate_loading();
    demo_unregister_module();
    demo_concurrent_logging();

    /* 清理 */
    printf("\n========================================\n");
    printf("  程序结束，清理资源\n");
    printf("========================================\n");

    zlog_mod_fini();
    zlog_fini();

    return EXIT_SUCCESS;
}

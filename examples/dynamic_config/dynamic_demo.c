/*
 * dynamic_demo.c — 动态添加分类和过滤规则示例
 *
 * 功能演示：
 *   1. 使用最小化配置启动 zlog
 *   2. 运行时动态构建新的配置字符串
 *   3. 使用 zlog_reload_from_string() 添加新分类和规则
 *   4. 无需重启程序，立即生效
 *
 * 这种方式特别适合：
 *   - 需要根据运行时条件决定日志输出目标
 *   - 插件化架构，每个插件需要独立的日志分类
 *   - 根据用户配置或远程命令动态调整日志规则
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "zlog.h"

/* 全局配置缓冲区，用于构建动态配置 */
#define MAX_CONFIG_SIZE 8192
static char g_config_buffer[MAX_CONFIG_SIZE];

/* 初始的最小化配置，只定义全局设置和一个默认规则 */
static const char *INITIAL_CONFIG =
"[global]\n"
"strict init = false\n"
"buffer min = 1KB\n"
"buffer max = 2MB\n"
"\n"
"[formats]\n"
"simple = \"%d(%H:%M:%S) [%-5V] [%c] %m%n\"\n"
"detailed = \"%d(%Y-%m-%d %H:%M:%S).%ms [%-5V] [%c] [%f:%L] %m%n\"\n"
"\n"
"[rules]\n"
"# 默认规则：所有分类的 INFO 及以上输出到控制台\n"
"*.INFO >stdout; simple\n";

/**
 * 构建动态配置字符串
 *
 * @param categories 分类名称数组
 * @param count      分类数量
 * @param log_dir    日志目录路径
 * @return           成功返回0，失败返回-1
 */
int build_dynamic_config(const char *categories[], int count, const char *log_dir)
{
    int offset = 0;
    int i;
    int ret;

    /* 首先复制基础配置 */
    offset = snprintf(g_config_buffer, MAX_CONFIG_SIZE, "%s", INITIAL_CONFIG);
    if (offset >= MAX_CONFIG_SIZE) {
        fprintf(stderr, "Config buffer overflow at initial config\n");
        return -1;
    }

    /* 为每个分类动态添加规则 */
    for (i = 0; i < count; i++) {
        /* 添加文件输出规则：DEBUG 及以上写入独立日志文件 */
        ret = snprintf(g_config_buffer + offset, MAX_CONFIG_SIZE - offset,
                       "%s.DEBUG \"%s/%s.log\"; detailed\n",
                       categories[i], log_dir, categories[i]);
        if (ret < 0 || offset + ret >= MAX_CONFIG_SIZE) {
            fprintf(stderr, "Config buffer overflow at category %s\n", categories[i]);
            return -1;
        }
        offset += ret;

        /* 添加控制台错误输出规则：ERROR 及以上输出到 stderr */
        ret = snprintf(g_config_buffer + offset, MAX_CONFIG_SIZE - offset,
                       "%s.ERROR >stderr; simple\n", categories[i]);
        if (ret < 0 || offset + ret >= MAX_CONFIG_SIZE) {
            fprintf(stderr, "Config buffer overflow at category %s stderr\n", categories[i]);
            return -1;
        }
        offset += ret;
    }

    /* 添加统一的警告日志文件 */
    ret = snprintf(g_config_buffer + offset, MAX_CONFIG_SIZE - offset,
                   "*.WARN \"%s/all_warnings.log\"; detailed\n", log_dir);
    if (ret < 0 || offset + ret >= MAX_CONFIG_SIZE) {
        fprintf(stderr, "Config buffer overflow at warning rule\n");
        return -1;
    }

    return 0;
}

/**
 * 演示场景1：系统启动时根据配置动态添加模块日志
 */
void demo_startup_modules(void)
{
    int rc;
    const char *modules[] = {"auth", "api", "database", "cache"};
    int module_count = sizeof(modules) / sizeof(modules[0]);
    zlog_category_t *cat;
    int i;

    printf("\n=== 演示场景1：启动时批量添加模块分类 ===\n");

    /* 构建包含所有模块的配置 */
    rc = build_dynamic_config(modules, module_count, "./logs");
    if (rc) {
        fprintf(stderr, "Failed to build dynamic config\n");
        return;
    }

    printf("生成的动态配置：\n%s\n", g_config_buffer);

    /* 重新加载配置（首次可用 zlog_init_from_string） */
    rc = zlog_reload_from_string(g_config_buffer);
    if (rc) {
        fprintf(stderr, "zlog_reload_from_string failed\n");
        return;
    }

    printf("配置重新加载成功！\n\n");

    /* 测试每个模块的日志输出 */
    for (i = 0; i < module_count; i++) {
        cat = zlog_get_category(modules[i]);
        if (!cat) {
            fprintf(stderr, "Failed to get category %s\n", modules[i]);
            continue;
        }

        zlog_info(cat, "%s 模块初始化完成", modules[i]);
        zlog_debug(cat, "%s 模块调试信息（写入文件）", modules[i]);
        zlog_warn(cat, "%s 模块警告信息（写入 all_warnings.log）", modules[i]);
    }
}

/**
 * 演示场景2：运行时动态添加新的插件日志分类
 */
void demo_add_plugin_at_runtime(const char *plugin_name)
{
    int rc;
    const char *all_categories[] = {"auth", "api", "database", "cache", NULL};
    zlog_category_t *cat;
    int count;

    printf("\n=== 演示场景2：运行时添加新插件 [%s] ===\n", plugin_name);

    /* 添加新插件到分类列表 */
    all_categories[4] = plugin_name;
    count = 5;

    /* 重新构建配置 */
    rc = build_dynamic_config(all_categories, count, "./logs");
    if (rc) {
        fprintf(stderr, "Failed to build config with new plugin\n");
        return;
    }

    /* 重新加载配置 */
    rc = zlog_reload_from_string(g_config_buffer);
    if (rc) {
        fprintf(stderr, "Failed to reload config\n");
        return;
    }

    printf("插件 [%s] 的日志配置已添加！\n\n", plugin_name);

    /* 测试新插件的日志 */
    cat = zlog_get_category(plugin_name);
    if (!cat) {
        fprintf(stderr, "Failed to get category %s\n", plugin_name);
        return;
    }

    zlog_info(cat, "插件 %s 已加载", plugin_name);
    zlog_debug(cat, "插件 %s 初始化参数：version=1.0", plugin_name);
    zlog_error(cat, "插件 %s 测试错误输出（应该显示在 stderr）", plugin_name);
}

/**
 * 演示场景3：根据运行时条件调整日志级别
 */
void demo_adjust_log_level(void)
{
    int rc;
    zlog_category_t *api_cat;

    printf("\n=== 演示场景3：动态调整日志级别 ===\n");

    /* 获取 API 分类 */
    api_cat = zlog_get_category("api");
    if (!api_cat) {
        fprintf(stderr, "Failed to get api category\n");
        return;
    }

    printf("当前 API 日志级别：DEBUG 及以上\n");
    zlog_debug(api_cat, "API 调试信息 - 当前可见");

    /* 构建新配置，将 api 分类的级别提升到 WARN */
    snprintf(g_config_buffer, MAX_CONFIG_SIZE,
        "[global]\n"
        "strict init = false\n"
        "buffer min = 1KB\n"
        "buffer max = 2MB\n"
        "\n"
        "[formats]\n"
        "simple = \"%%d(%%H:%%M:%%S) [%%-5V] [%%c] %%m%%n\"\n"
        "detailed = \"%%d(%%Y-%%m-%%d %%H:%%M:%%S).%%ms [%%-5V] [%%c] [%%f:%%L] %%m%%n\"\n"
        "\n"
        "[rules]\n"
        "# 提升 api 分类到 WARN 级别，过滤掉 DEBUG 和 INFO\n"
        "api.WARN \"./logs/api.log\"; detailed\n"
        "api.WARN >stdout; simple\n"
        "\n"
        "# 其他分类保持原样\n"
        "auth.DEBUG \"./logs/auth.log\"; detailed\n"
        "database.DEBUG \"./logs/database.log\"; detailed\n"
        "cache.DEBUG \"./logs/cache.log\"; detailed\n"
        "*.WARN \"./logs/all_warnings.log\"; detailed\n"
    );

    rc = zlog_reload_from_string(g_config_buffer);
    if (rc) {
        fprintf(stderr, "Failed to reload config\n");
        return;
    }

    printf("\n日志级别已调整为 WARN 及以上\n");

    /* 重新获取分类（reload 后需要重新获取） */
    api_cat = zlog_get_category("api");
    if (!api_cat) {
        fprintf(stderr, "Failed to get api category after reload\n");
        return;
    }

    zlog_debug(api_cat, "API 调试信息 - 现在应该被过滤");
    zlog_info(api_cat, "API 普通信息 - 现在应该被过滤");
    zlog_warn(api_cat, "API 警告信息 - 现在可见");
    zlog_error(api_cat, "API 错误信息 - 现在可见");
}

int main(void)
{
    int rc;

    /* 创建日志目录 */
    mkdir("./logs", 0755);

    /* 步骤1：使用最小化配置初始化 */
    printf("=== 使用最小化配置初始化 zlog ===\n");
    rc = zlog_init_from_string(INITIAL_CONFIG);
    if (rc) {
        fprintf(stderr, "zlog_init_from_string failed, rc=%d\n", rc);
        return EXIT_FAILURE;
    }
    printf("zlog 初始化成功（初始配置）\n");

    /* 演示场景1：启动时批量添加模块 */
    demo_startup_modules();

    /* 等待一下让日志刷新 */
    sleep(1);

    /* 演示场景2：运行时添加插件 */
    demo_add_plugin_at_runtime("payment");

    /* 等待一下 */
    sleep(1);

    /* 演示场景3：动态调整日志级别 */
    demo_adjust_log_level();

    /* 清理 */
    printf("\n=== 程序结束，清理 zlog ===\n");
    zlog_fini();

    printf("\n提示：请检查 ./logs/ 目录下的日志文件：\n");
    printf("  - auth.log, api.log, database.log, cache.log, payment.log\n");
    printf("  - all_warnings.log (包含所有模块的警告)\n");

    return EXIT_SUCCESS;
}

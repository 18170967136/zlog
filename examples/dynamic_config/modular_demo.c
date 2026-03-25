/*
 * modular_demo.c - 模块化分散加载配置示例
 *
 * 功能演示：
 *   1. 每个模块独立加载自己的格式和规则
 *   2. 重复加载同一模块时，先清除旧规则再添加新规则（覆盖）
 *   3. 格式名冲突时，后加载的覆盖先加载的
 *   4. 多个模块完全独立，互不影响
 *
 * 使用的新 API：
 *   zlog_add_format(name, pattern)       — 添加/覆盖格式
 *   zlog_add_rule(rule_line)             — 添加规则
 *   zlog_remove_rules(category)          — 删除某分类的所有规则
 *   zlog_has_format(name)                — 查询格式是否存在
 *   zlog_has_category_rules(category)    — 查询分类规则是否存在
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "zlog.h"

/**
 * 模拟一个模块的初始化函数
 * 每个模块独立注册自己的格式和规则
 */
typedef struct {
    const char *name;
    const char *format_name;
    const char *format_pattern;
    const char *log_level;
} module_config_t;

/**
 * 注册一个模块的日志配置
 * 如果模块已经注册过（重复加载），会先清除旧规则再添加新规则
 */
int register_module_logging(const module_config_t *mod)
{
    int rc;
    char rule_line[1024];

    printf("  注册模块 [%s] ...\n", mod->name);

    /* 检查格式是否已存在 */
    if (zlog_has_format(mod->format_name)) {
        printf("    格式 '%s' 已存在，将被覆盖\n", mod->format_name);
    }

    /* 添加或覆盖格式 */
    rc = zlog_add_format(mod->format_name, mod->format_pattern);
    if (rc) {
        fprintf(stderr, "    添加格式失败: %s\n", mod->format_name);
        return -1;
    }

    /* 检查该分类是否已有规则（可能是重复加载） */
    if (zlog_has_category_rules(mod->name)) {
        printf("    分类 '%s' 已有规则，先清除旧规则\n", mod->name);
        rc = zlog_remove_rules(mod->name);
        if (rc < 0) {
            fprintf(stderr, "    清除旧规则失败\n");
            return -1;
        }
        printf("    已清除 %d 条旧规则\n", rc);
    }

    /* 添加新规则 */
    snprintf(rule_line, sizeof(rule_line),
             "%s.%s >stdout; %s", mod->name, mod->log_level, mod->format_name);
    rc = zlog_add_rule(rule_line);
    if (rc) {
        fprintf(stderr, "    添加规则失败: %s\n", rule_line);
        return -1;
    }

    printf("    模块 [%s] 注册成功\n", mod->name);
    return 0;
}

/**
 * 演示场景1：多个模块独立加载
 */
void demo_independent_modules(void)
{
    printf("\n=== 场景1：多个模块独立加载 ===\n");

    /* 模块 A：认证模块 */
    module_config_t auth_mod = {
        .name = "auth",
        .format_name = "auth_fmt",
        .format_pattern = "%d(%H:%M:%S) [AUTH] [%-5V] %m%n",
        .log_level = "DEBUG",
    };

    /* 模块 B：API 模块 */
    module_config_t api_mod = {
        .name = "api",
        .format_name = "api_fmt",
        .format_pattern = "%d(%H:%M:%S) [API] [%-5V] %m%n",
        .log_level = "INFO",
    };

    /* 模块 C：数据库模块 */
    module_config_t db_mod = {
        .name = "database",
        .format_name = "db_fmt",
        .format_pattern = "%d(%H:%M:%S) [DB] [%-5V] %m%n",
        .log_level = "WARN",
    };

    /* 每个模块独立注册 */
    register_module_logging(&auth_mod);
    register_module_logging(&api_mod);
    register_module_logging(&db_mod);

    /* 测试输出 */
    printf("\n  测试各模块日志输出：\n");
    zlog_category_t *cat_auth = zlog_get_category("auth");
    zlog_category_t *cat_api = zlog_get_category("api");
    zlog_category_t *cat_db = zlog_get_category("database");

    if (cat_auth) {
        zlog_debug(cat_auth, "认证模块调试信息");
        zlog_info(cat_auth, "用户 admin 登录成功");
    }

    if (cat_api) {
        zlog_info(cat_api, "API 请求: GET /users");
        zlog_warn(cat_api, "API 响应较慢: 2.5s");
    }

    if (cat_db) {
        zlog_info(cat_db, "数据库查询 (DEBUG级别，不应显示)");
        zlog_warn(cat_db, "数据库连接池使用率: 80%%");
    }
}

/**
 * 演示场景2：重复加载同一模块（覆盖旧规则）
 */
void demo_duplicate_loading(void)
{
    printf("\n=== 场景2：重复加载模块（覆盖） ===\n");

    /* 第一次加载 auth 模块 */
    module_config_t auth_v1 = {
        .name = "auth",
        .format_name = "auth_fmt",
        .format_pattern = "%d(%H:%M:%S) [AUTH-v1] %m%n",
        .log_level = "DEBUG",
    };

    printf("  第一次加载 auth 模块：\n");
    register_module_logging(&auth_v1);

    zlog_category_t *cat = zlog_get_category("auth");
    if (cat) zlog_info(cat, "auth v1 日志");

    /* 第二次加载同一模块（模拟重复加载或升级） */
    module_config_t auth_v2 = {
        .name = "auth",
        .format_name = "auth_fmt",
        .format_pattern = "%d(%H:%M:%S) [AUTH-v2] %m%n",
        .log_level = "WARN",
    };

    printf("\n  第二次加载 auth 模块（覆盖）：\n");
    register_module_logging(&auth_v2);

    cat = zlog_get_category("auth");
    if (cat) {
        zlog_info(cat, "这条 INFO 不应该显示（已升级为 WARN 级别）");
        zlog_warn(cat, "auth v2 警告日志（应该显示）");
    }
}

/**
 * 演示场景3：格式名冲突
 */
void demo_format_conflict(void)
{
    printf("\n=== 场景3：格式名冲突处理 ===\n");

    /* 模块 X 定义 "common" 格式 */
    printf("  模块 X 定义 'common' 格式...\n");
    zlog_add_format("common", "%d [ModX] %m%n");
    zlog_add_rule("mod_x.INFO >stdout; common");

    /* 模块 Y 也定义 "common" 格式（冲突！） */
    printf("  模块 Y 也定义 'common' 格式（将覆盖模块 X 的定义）...\n");
    zlog_add_format("common", "%d [ModY] %m%n");
    zlog_add_rule("mod_y.INFO >stdout; common");

    /* 注意：格式被覆盖后，使用该格式的所有规则都会使用新的格式定义 */
    printf("\n  注意：'common' 格式已被模块 Y 覆盖\n");

    zlog_category_t *cat_x = zlog_get_category("mod_x");
    zlog_category_t *cat_y = zlog_get_category("mod_y");

    if (cat_x) zlog_info(cat_x, "模块 X 的日志（格式已被 Y 覆盖）");
    if (cat_y) zlog_info(cat_y, "模块 Y 的日志");

    printf("\n  提示：为避免冲突，建议每个模块使用带模块名前缀的格式名\n");
}

int main(void)
{
    int rc;

    /* 使用最小配置初始化 */
    const char *config =
        "[global]\n"
        "strict init = false\n"
        "\n"
        "[formats]\n"
        "default = \"%d(%H:%M:%S) [%-5V] %m%n\"\n"
        "\n"
        "[rules]\n"
        "*.WARN >stdout; default\n";

    printf("=== 模块化分散加载配置示例 ===\n");
    printf("使用最小配置初始化 zlog...\n");

    rc = zlog_init_from_string(config);
    if (rc) {
        fprintf(stderr, "zlog_init_from_string failed\n");
        return EXIT_FAILURE;
    }

    /* 演示各场景 */
    demo_independent_modules();
    demo_duplicate_loading();
    demo_format_conflict();

    /* 清理 */
    printf("\n=== 程序结束 ===\n");
    zlog_fini();

    return EXIT_SUCCESS;
}

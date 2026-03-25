/*
 * simple_demo.c — 最简单的动态配置示例
 *
 * 演示如何在运行时添加新的日志分类和规则，而不需要修改配置文件。
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "zlog.h"

int main(void)
{
    int rc;
    zlog_category_t *cat1, *cat2, *cat3;

    /* 创建日志目录 */
    mkdir("./logs", 0755);

    /* 初始配置：只有一个基本规则 */
    const char *config1 =
        "[global]\n"
        "strict init = false\n"
        "\n"
        "[formats]\n"
        "simple = \"%d(%H:%M:%S) [%c] %m%n\"\n"
        "\n"
        "[rules]\n"
        "*.INFO >stdout; simple\n";

    printf("=== 步骤1：初始化 zlog（只有默认规则） ===\n");
    rc = zlog_init_from_string(config1);
    if (rc) {
        fprintf(stderr, "zlog_init_from_string failed\n");
        return EXIT_FAILURE;
    }

    cat1 = zlog_get_category("module_a");
    zlog_info(cat1, "模块 A 的第一条日志");

    /* 动态添加新的分类和规则 */
    printf("\n=== 步骤2：动态添加模块 B 和 C 的日志规则 ===\n");
    const char *config2 =
        "[global]\n"
        "strict init = false\n"
        "\n"
        "[formats]\n"
        "simple = \"%d(%H:%M:%S) [%c] %m%n\"\n"
        "detailed = \"%d(%Y-%m-%d %H:%M:%S) [%-5V] [%c] %m%n\"\n"
        "\n"
        "[rules]\n"
        "# 模块 A：继续输出到控制台\n"
        "module_a.INFO >stdout; simple\n"
        "\n"
        "# 模块 B：新增，输出到文件\n"
        "module_b.DEBUG \"./logs/module_b.log\"; detailed\n"
        "module_b.WARN >stdout; simple\n"
        "\n"
        "# 模块 C：新增，输出到文件\n"
        "module_c.DEBUG \"./logs/module_c.log\"; detailed\n"
        "module_c.ERROR >stderr; simple\n";

    rc = zlog_reload_from_string(config2);
    if (rc) {
        fprintf(stderr, "zlog_reload_from_string failed\n");
        zlog_fini();
        return EXIT_FAILURE;
    }

    printf("配置已更新！现在测试新添加的模块...\n\n");

    /* 测试新添加的分类 */
    cat2 = zlog_get_category("module_b");
    cat3 = zlog_get_category("module_c");

    zlog_info(cat1, "模块 A 的第二条日志");
    zlog_debug(cat2, "模块 B 的调试信息（写入 module_b.log）");
    zlog_warn(cat2, "模块 B 的警告（输出到控制台）");
    zlog_info(cat3, "模块 C 的普通信息（写入 module_c.log）");
    zlog_error(cat3, "模块 C 的错误（输出到 stderr）");

    /* 再次动态修改：只保留重要日志 */
    printf("\n=== 步骤3：动态调整为只记录 WARN 及以上 ===\n");
    const char *config3 =
        "[global]\n"
        "strict init = false\n"
        "\n"
        "[formats]\n"
        "simple = \"%d(%H:%M:%S) [%-5V] [%c] %m%n\"\n"
        "\n"
        "[rules]\n"
        "*.WARN >stdout; simple\n"
        "*.WARN \"./logs/important.log\"; simple\n";

    rc = zlog_reload_from_string(config3);
    if (rc) {
        fprintf(stderr, "zlog_reload_from_string failed\n");
        zlog_fini();
        return EXIT_FAILURE;
    }

    printf("配置已调整为只记录警告及以上级别！\n\n");

    /* 重新获取分类并测试 */
    cat1 = zlog_get_category("module_a");
    cat2 = zlog_get_category("module_b");
    cat3 = zlog_get_category("module_c");

    zlog_debug(cat1, "这条 DEBUG 不会输出");
    zlog_info(cat1, "这条 INFO 不会输出");
    zlog_warn(cat1, "这条 WARN 会输出");
    zlog_error(cat2, "这条 ERROR 会输出");
    zlog_fatal(cat3, "这条 FATAL 会输出");

    /* 清理 */
    printf("\n=== 程序结束 ===\n");
    zlog_fini();

    printf("\n提示：查看生成的日志文件：\n");
    printf("  ls -la ./logs/\n");

    return EXIT_SUCCESS;
}

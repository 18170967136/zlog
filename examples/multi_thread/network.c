/*
 * network.c — 网络模块
 *
 * 多源文件使用 zlog 的核心模式：
 *   每个 .c 文件声明一个 static zlog_category_t *cat，
 *   在模块初始化函数中通过 zlog_get_category() 获取分类句柄，
 *   之后所有日志宏直接使用该局部变量，无需传参。
 *
 * 线程安全说明：
 *   zlog_get_category() 内部加写锁，是线程安全的；
 *   获取到的 zlog_category_t* 是只读句柄，可供多线程并发使用；
 *   zlog_info/debug/… 宏内部加读锁，消息格式化在各线程私有缓冲区中完成，
 *   多线程并发调用不会产生竞争。
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "zlog.h"
#include "network.h"

/* ------------------------------------------------------------------ *
 * 模块私有日志分类                                                       *
 * 对应 zlog.conf [rules] 中 "network.*" 规则                           *
 * ------------------------------------------------------------------ */
static zlog_category_t *net_cat;

int network_module_init(void)
{
    /*
     * zlog_get_category() 在 zlog_init() 之后调用。
     * 建议在主线程启动工作线程之前完成所有模块的初始化，
     * 避免多线程同时调用 zlog_get_category() 造成不必要的锁竞争。
     */
    net_cat = zlog_get_category("network");
    if (!net_cat) {
        fprintf(stderr, "[network] zlog_get_category(\"network\") failed\n");
        return -1;
    }
    zlog_info(net_cat, "network module initialized");
    return 0;
}

int network_receive(int req_id, char *out_buf, int buf_size)
{
    /*
     * 此函数在工作线程中调用。调用线程已通过 zlog_put_mdc("request_id", ...)
     * 设置了请求上下文，因此本模块打印的日志行会自动携带 request_id 字段，
     * 无需在函数签名中额外传递该参数。
     */
    zlog_debug(net_cat, "receiving data for req=%d", req_id);

    /* 模拟网络 I/O 延迟（500 µs） */
    usleep(500);

    snprintf(out_buf, buf_size, "payload_for_req_%d", req_id);

    zlog_info(net_cat, "received %zu bytes: [%s]", strlen(out_buf), out_buf);
    return 0;
}

void network_module_fini(void)
{
    zlog_info(net_cat, "network module shutting down");
}

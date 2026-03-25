#ifndef NETWORK_H
#define NETWORK_H

/*
 * 网络模块（network.c）
 *
 * 使用模式说明：
 *   - 本模块维护自己的 zlog 日志分类 "network"，与其他模块完全独立。
 *   - 调用任何 network_* 函数之前，必须先完成：
 *       1. zlog_init()           — 在 main() 中调用一次
 *       2. network_module_init() — 在启动工作线程之前调用一次
 */

int  network_module_init(void);
int  network_receive(int req_id, char *out_buf, int buf_size);
void network_module_fini(void);

#endif /* NETWORK_H */

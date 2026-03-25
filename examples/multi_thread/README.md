# zlog 多线程多源文件使用示例

本示例展示在多线程、多源文件的 C 项目中正确使用 zlog 的推荐模式。

## 目录结构

```
examples/multi_thread/
├── zlog.conf      — zlog 配置文件（多分类、MDC 格式）
├── main.c         — 程序入口：init/fini、线程管理
├── network.h/c    — 网络模块：独立日志分类 "network"
├── database.h/c   — 数据库模块：独立日志分类 "database"
├── worker.h/c     — 工作线程：MDC 请求追踪用法
├── CMakeLists.txt — CMake 构建文件
└── Makefile       — 独立 Makefile（需系统已安装 zlog）
```

## 核心模式

### 1. 每个源文件维护自己的日志分类

```c
/* network.c */
#include "zlog.h"

static zlog_category_t *net_cat;   /* 模块私有，只在本文件中可见 */

int network_module_init(void)
{
    net_cat = zlog_get_category("network");  /* zlog_init() 之后调用 */
    return net_cat ? 0 : -1;
}

void network_receive(void)
{
    zlog_debug(net_cat, "receiving ...");    /* 线程安全，可在任意线程调用 */
    zlog_info(net_cat, "received ok");
}
```

**为什么这样设计：**
- 各模块日志分类名不同，可在 `zlog.conf` 中为每个模块配置独立的输出目标和级别。
- `zlog_category_t*` 是只读句柄，多线程共享同一指针完全安全。
- 格式化在每个线程私有的缓冲区（TLS）中完成，无锁竞争。

---

### 2. 初始化顺序

```c
/* main.c */
int main(void)
{
    mkdir("./logs", 0755);          /* ① 提前创建日志目录 */
    zlog_init("zlog.conf");         /* ② 全局初始化，只调用一次 */

    /* ③ 各模块初始化（在启动子线程之前完成） */
    network_module_init();
    database_module_init();
    worker_module_init();

    /* ④ 启动工作线程 */
    pthread_create(&tid, NULL, worker_thread, &args);

    pthread_join(tid, NULL);        /* ⑤ 等待线程结束 */

    zlog_fini();                    /* ⑥ 全局清理，在所有线程结束后调用 */
    return 0;
}
```

> **注意**：`zlog_fini()` 必须在所有工作线程 `pthread_join()` 之后调用，否则正在运行的线程访问已释放的 zlog 资源会导致未定义行为。

---

### 3. MDC（Mapped Diagnostic Context）—— 请求追踪

MDC 是 zlog 提供的线程本地键值存储，用于在整个请求处理链中自动携带上下文信息，无需逐层传递参数。

```c
/* worker.c — 请求处理入口 */
void *worker_thread(void *arg)
{
    char req_id[32];

    for (int i = 0; i < N; i++) {
        snprintf(req_id, sizeof(req_id), "REQ-%04d", i);

        /* 在请求入口设置 MDC，后续所有日志（含其他模块）都会自动携带该值 */
        zlog_put_mdc("request_id", req_id);

        zlog_info(wk_cat, "begin request");

        network_receive(...);   /* network.c 内部的日志也会携带 request_id */
        database_save(...);     /* database.c 内部的日志也会携带 request_id */

        zlog_info(wk_cat, "end request");
    }

    zlog_clean_mdc();   /* 清理本线程 MDC（可选，线程退出时自动清理） */
    return NULL;
}
```

在 `zlog.conf` 的格式字符串中通过 `%M(request_id)` 引用 MDC 值：

```ini
[formats]
full = "%d(%H:%M:%S).%ms [%-5V] [%c] [tid:%t] [req:%M(request_id)] %m%n"
```

**效果**：所有线程的日志行都会打印各自的 `request_id`，即使多线程并发执行，
也能通过 `request_id` 过滤出单条请求的完整日志链路。

**MDC 线程隔离**：MDC 存储在线程本地存储（TLS）中，线程 A 的 `request_id` 不会影响线程 B。

---

### 4. 配置文件（zlog.conf）关键配置

```ini
[global]
strict init = true    # 分类名/格式名未找到时报错退出（推荐开启）
buffer min  = 1KB     # 每线程消息缓冲初始大小
buffer max  = 2MB     # 每线程消息缓冲最大大小

[formats]
# %M(request_id) 读取当前线程 MDC 中键名为 request_id 的值
full = "%d(%Y-%m-%d %H:%M:%S).%ms [%-5V] [%-8c] [tid:%-5t] [req:%M(request_id)] %m%n"

[rules]
# 各模块分别路由到不同文件，方便定向排查
network.DEBUG  "./logs/network.log";  full
database.DEBUG "./logs/database.log"; full
worker.DEBUG   "./logs/worker.log";   full

# 所有 WARN 以上汇总到一个文件，方便整体巡检
*.WARN  "./logs/app_warn.log";  full
```

---

## 构建与运行

### 方式一：在 zlog 源码树内用 CMake 构建（推荐）

```bash
cd /path/to/zlog
cmake -DBUILD_EXAMPLES=ON -B build
cmake --build build -j4

cd build/bin
./zlog_multi_thread_example
```

### 方式二：独立构建（需先安装 zlog）

```bash
# 先安装 zlog
cd /path/to/zlog
cmake -B build && cmake --build build -j4
sudo cmake --install build

# 再构建示例
cd examples/multi_thread
make run
```

### 方式三：独立 CMake 构建（使用系统 zlog）

```bash
cd examples/multi_thread
cmake -DUSE_SYSTEM_ZLOG=ON -B build
cmake --build build -j4
mkdir -p build/logs
cd build && ./zlog_multi_thread_example
```

---

## 预期输出示例

控制台（simple 格式，所有线程交错输出）：
```
02:12:52.136 [INFO ] [main] === application starting, zlog version: 1.2.18 ===
02:12:52.137 [INFO ] [main] all modules initialized, spawning 4 worker threads
02:12:52.137 [DEBUG] [main] spawned worker-1, req_start=1
02:12:52.144 [INFO ] [main] all 4 worker threads finished
02:12:52.144 [INFO ] [main] === application exiting ===
```

`logs/worker.log`（full 格式，可见 tid 和 request_id 的 MDC 追踪）：
```
2026-03-25 02:12:52.137 [INFO ] [worker  ] [tid:7f84f19ff6c0] [worker.c:61 worker_thread()] [req:REQ-0001] ---- begin REQ-0001 ----
2026-03-25 02:12:52.137 [INFO ] [worker  ] [tid:7f84f09fe6c0] [worker.c:61 worker_thread()] [req:REQ-0006] ---- begin REQ-0006 ----
2026-03-25 02:12:52.138 [INFO ] [worker  ] [tid:7f84f19ff6c0] [worker.c:75 worker_thread()] [req:REQ-0001] ---- end REQ-0001 (ok) ----
...
2026-03-25 02:12:52.144 [INFO ] [worker  ] [tid:7f84f19ff6c0] [worker.c:84 worker_thread()] [req:] worker-1 finished
```

观察要点：
- 同一 `tid` 的日志行追踪同一线程的执行轨迹
- 同一 `req:REQ-XXXX` 的日志行（跨 worker/network/database 分类）追踪同一请求的完整链路
- 线程退出后 `req:` 字段为空，说明 `zlog_clean_mdc()` 正确清理了 MDC

---

## 线程安全保证（来自 zlog 底层）

| 操作 | 线程安全性 | 说明 |
|------|-----------|------|
| `zlog_init()` / `zlog_fini()` | ✅ 安全 | 内部加写锁，建议在主线程单次调用 |
| `zlog_get_category()` | ✅ 安全 | 内部加写锁 |
| `zlog_info()` 等日志宏 | ✅ 安全 | 内部加读锁，格式化在 TLS 缓冲中完成，零竞争 |
| `zlog_put_mdc()` / `zlog_get_mdc()` | ✅ 安全 | MDC 存储在 TLS，每线程独立 |
| 多线程共享 `zlog_category_t*` | ✅ 安全 | 只读句柄，获取后不可修改 |

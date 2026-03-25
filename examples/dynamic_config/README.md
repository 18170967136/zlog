# 动态配置示例 (Dynamic Configuration Example)

本目录演示如何在 zlog 运行时动态添加分类和过滤规则，而不需要提前在配置文件中定义所有内容。

## 核心 API

zlog 提供了两个 API 支持动态配置：

```c
/* 从字符串初始化 zlog */
int zlog_init_from_string(const char *config_string);

/* 从字符串重新加载配置 */
int zlog_reload_from_string(const char *config_string);
```

## 模块化加载封装 (zlog_modular)

本目录额外提供了 `zlog_modular.h` / `zlog_modular.c` 封装层，基于 `zlog_reload_from_string()` 实现模块化分散加载。**不修改 zlog 源码**，仅使用公共 API。

内部使用 [cJSON](https://github.com/DaveGamble/cJSON)（MIT 许可）管理配置数据，支持通过 `zlog_mod_dump_json()` 导出 JSON 文档，方便观测和调试。

```c
#include "zlog_modular.h"

/* 初始化模块管理器 */
int zlog_mod_init(const char *global_conf);

/* 注册一个模块的格式和规则（同名模块自动覆盖） */
int zlog_mod_register(const char *module_name,
                      const char *formats[], int format_count,
                      const char *rules[], int rule_count);

/* 卸载一个模块 */
int zlog_mod_unregister(const char *module_name);

/* 查询模块是否已注册 */
int zlog_mod_has_module(const char *module_name);

/* 获取已注册模块数量 */
int zlog_mod_count(void);

/* 导出当前配置状态为 JSON 字符串（调用者用 free() 释放） */
char *zlog_mod_dump_json(void);

/* 清理模块管理器 */
void zlog_mod_fini(void);
```

### 设计思路

- 内部使用 cJSON 对象维护全局注册表，记录每个模块的格式和规则
- 每次 `zlog_mod_register()` / `zlog_mod_unregister()` 时，自动合并所有模块的配置，生成完整配置字符串，调用 `zlog_reload_from_string()`
- 同名模块重复注册时，自动覆盖旧配置（解决重复加载问题）
- 使用 `pthread_mutex` 保护注册表（线程安全）
- 调用 `zlog_mod_dump_json()` 可随时导出 JSON 文档，清晰展示所有模块的配置状态

### JSON 可观测性

调用 `zlog_mod_dump_json()` 返回的 JSON 文档格式：

```json
{
    "global_conf": "strict init = false",
    "module_count": 3,
    "modules": {
        "auth": {
            "formats": ["auth_fmt = \"%d [AUTH] %m%n\""],
            "rules": ["auth.DEBUG >stdout; auth_fmt"]
        },
        "api": {
            "formats": ["api_fmt = \"%d [API] %m%n\""],
            "rules": ["api.INFO >stdout; api_fmt"]
        },
        "database": {
            "formats": ["db_fmt = \"%d [DB] %m%n\""],
            "rules": ["database.WARN >stdout; db_fmt"]
        }
    }
}
```

使用示例：

```c
/* 随时查看当前配置状态 */
char *json = zlog_mod_dump_json();
if (json) {
    printf("当前配置:\n%s\n", json);
    free(json);
}
```

## 使用场景

1. **插件化架构**：插件加载时动态添加其日志分类
2. **运行时配置调整**：根据用户命令或远程配置动态改变日志级别
3. **模块化系统**：每个模块启动时注册自己的日志规则
4. **程序解耦**：不同模块无需提前在配置文件中声明
5. **分散加载**：各模块在独立线程中加载自己的配置，互不影响

## 示例文件

### simple_demo.c
最简单的示例，演示：
- 使用最小化配置启动
- 运行时添加新的模块日志规则
- 动态调整日志级别

### dynamic_demo.c
完整示例，演示：
- 启动时批量添加多个模块
- 运行时动态添加新插件
- 根据条件调整日志级别

### modular_mt_demo.c
**多线程多模块分散加载示例**，使用 `zlog_modular.h` 封装层，演示：
- 5 个线程并发注册 5 个模块的日志配置
- 重复加载同一模块时自动覆盖旧配置
- 运行时卸载模块
- 多线程并发日志输出

## 编译和运行

### 方式1：使用 CMake（推荐）

```bash
# 在 zlog 根目录
cmake -B build
cmake --build build

# 运行示例
cd examples/dynamic_config
../../build/examples/dynamic_config/simple_demo
../../build/examples/dynamic_config/dynamic_demo
../../build/examples/dynamic_config/modular_mt_demo
```

### 方式2：手动编译

```bash
# 先编译 zlog 库
cd /path/to/zlog
make

# 编译示例
cd examples/dynamic_config
gcc -o simple_demo simple_demo.c -I../../src -L../../src -lzlog -lpthread
gcc -o dynamic_demo dynamic_demo.c -I../../src -L../../src -lzlog -lpthread
gcc -o modular_mt_demo modular_mt_demo.c zlog_modular.c cJSON.c -I../../src -L../../src -lzlog -lpthread -lm

# 设置库路径并运行
export LD_LIBRARY_PATH=../../src:$LD_LIBRARY_PATH
./simple_demo
./dynamic_demo
./modular_mt_demo
```

## 关键要点

### 1. 配置字符串格式

配置字符串的格式与配置文件完全相同：

```c
const char *config =
    "[global]\n"
    "strict init = false\n"
    "\n"
    "[formats]\n"
    "simple = \"%d(%H:%M:%S) [%c] %m%n\"\n"
    "\n"
    "[rules]\n"
    "my_module.DEBUG \"./logs/my_module.log\"; simple\n"
    "my_module.WARN >stdout; simple\n";
```

### 2. 重新加载后需要重新获取分类

```c
zlog_category_t *cat = zlog_get_category("my_cat");
zlog_info(cat, "before reload");

zlog_reload_from_string(new_config);

/* 重要：reload 后最好重新获取分类 */
cat = zlog_get_category("my_cat");
zlog_info(cat, "after reload");
```

### 3. 线程安全

`zlog_reload_from_string()` 是线程安全的，可以在任何线程调用。但建议：
- 在主线程或专门的管理线程中调用
- 避免频繁重新加载（有性能开销）

### 4. 配置合并

每次调用 `zlog_reload_from_string()` 都会完全替换当前配置，不是增量添加。
如果需要保留现有分类，必须在新配置中重新包含它们的规则。

如果需要增量添加，使用 `zlog_modular.h` 封装层，它会自动管理所有模块的配置合并。

### 5. 模块化分散加载

使用 `zlog_modular.h` 封装层可以实现每个模块独立加载配置，无需手动管理完整配置字符串：

```c
#include "zlog_modular.h"

/* 每个模块独立注册自己的日志配置 */
void my_module_init_logging(void) {
    const char *formats[] = {
        "my_fmt = \"%d [%c] [%-5V] %m%n\""
    };
    const char *rules[] = {
        "my_module.DEBUG >stdout; my_fmt"
    };

    /* 如果模块已注册过，zlog_mod_register 会自动覆盖 */
    zlog_mod_register("my_module", formats, 1, rules, 1);
}

/* 随时查看当前所有模块的配置状态 */
void dump_logging_config(void) {
    char *json = zlog_mod_dump_json();
    if (json) {
        printf("当前日志配置:\n%s\n", json);
        free(json);
    }
}
```

## 实际应用示例

### 示例1：插件系统

```c
void load_plugin(const char *plugin_name) {
    char config[4096];

    /* 构建包含新插件的完整配置 */
    build_full_config_with_plugin(config, sizeof(config), plugin_name);

    /* 重新加载 */
    if (zlog_reload_from_string(config) == 0) {
        /* 插件可以立即使用自己的日志分类 */
        zlog_category_t *cat = zlog_get_category(plugin_name);
        zlog_info(cat, "Plugin %s loaded", plugin_name);
    }
}
```

### 示例2：调试模式切换

```c
void enable_debug_mode(bool enable) {
    char config[4096];

    if (enable) {
        /* 所有模块改为 DEBUG 级别 */
        snprintf(config, sizeof(config),
            "[global]\n"
            "strict init = false\n"
            "[formats]\n"
            "debug_fmt = \"%%d [DEBUG] [%%c] %%m%%n\"\n"
            "[rules]\n"
            "*.DEBUG >stdout; debug_fmt\n"
            "*.DEBUG \"./logs/debug.log\"; debug_fmt\n");
    } else {
        /* 恢复为 INFO 级别 */
        snprintf(config, sizeof(config),
            "[global]\n"
            "strict init = false\n"
            "[formats]\n"
            "normal_fmt = \"%%d [%%V] [%%c] %%m%%n\"\n"
            "[rules]\n"
            "*.INFO >stdout; normal_fmt\n");
    }

    zlog_reload_from_string(config);
}
```

### 示例3：远程配置管理

```c
void on_config_update(const char *remote_config) {
    /* 从配置中心收到新配置 */
    if (validate_config(remote_config)) {
        int rc = zlog_reload_from_string(remote_config);
        if (rc == 0) {
            printf("Log config updated from remote\n");
        } else {
            fprintf(stderr, "Failed to apply remote config\n");
        }
    }
}
```

## 与配置文件方式的对比

| 方面 | 配置文件 | 动态配置字符串 | zlog_modular 封装 |
|------|----------|----------------|-------------------|
| 灵活性 | 静态，需要提前定义 | 动态，运行时构建 | 动态，按模块增量添加 |
| 解耦性 | 所有模块耦合在一个文件 | 需手动合并所有模块配置 | 每个模块完全独立 |
| 调试 | 修改文件需要 reload | 可通过命令即时调整 | 可按模块注册/卸载 |
| 可观测性 | 查看文件即可 | 需自行维护配置副本 | `zlog_mod_dump_json()` 导出 JSON |
| 复杂度 | 简单，直接编辑文件 | 需要编程构建字符串 | API 调用，最简单 |
| 重复处理 | 手动避免 | 手动避免 | 自动检测和覆盖 |
| 线程安全 | N/A | reload 线程安全 | 注册/卸载线程安全 |
| 适用场景 | 配置相对固定 | 配置动态变化 | 模块化/插件化架构 |

## 最佳实践

1. **保持基础配置简单**：初始化时只设置必需的全局参数
2. **封装配置构建函数**：将配置字符串生成逻辑封装为函数
3. **验证配置**：在 reload 前验证配置字符串的正确性
4. **处理失败**：reload 失败时保持原配置继续工作
5. **避免频繁 reload**：每次 reload 都有性能开销

## 常见问题

**Q: reload 会影响正在运行的日志吗？**
A: reload 期间会短暂阻塞，但不会丢失日志。建议在低负载时刻执行。

**Q: 可以只添加新分类不影响现有的吗？**
A: 直接使用 `zlog_reload_from_string()` 不行，每次 reload 都是完整替换。
但使用 `zlog_modular.h` 封装层（`zlog_mod_register()`）可以按模块增量添加，封装层会自动合并所有已注册模块的配置。

**Q: 多个模块格式名冲突怎么办？**
A: 建议给格式名加模块前缀（如 `auth_fmt`）以避免冲突。如果冲突了，最后注册的会覆盖之前的。

**Q: 重复加载同一模块怎么处理？**
A: `zlog_mod_register()` 会自动检测同名模块，先卸载旧配置再注册新的。也可以先调用 `zlog_mod_has_module()` 检查。

**Q: 可以在多个线程同时注册模块吗？**
A: 可以，`zlog_modular.h` 的所有 API 都是线程安全的。参见 `modular_mt_demo.c`。

**Q: 配置字符串中的百分号需要转义吗？**
A: 在 C 字符串中，格式化符号的百分号需要写成 `%%`（两个百分号）。

**Q: 是否可以从网络接收配置？**
A: 可以，但需要注意安全性，建议验证和清理配置内容。

## 参考

- zlog 主文档：`../../README.md`
- API 参考：`../../src/zlog.h`
- 配置文件格式：`../../test/test_hello.conf`

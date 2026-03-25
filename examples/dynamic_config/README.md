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

以及模块化增量配置 API：

```c
/* 添加或覆盖格式（同名格式会被新定义覆盖） */
int zlog_add_format(const char *name, const char *pattern);

/* 添加一条规则（格式同配置文件中的规则行） */
int zlog_add_rule(const char *rule_line);

/* 删除某分类的所有规则（返回删除的规则数，-1 表示失败） */
int zlog_remove_rules(const char *category);

/* 查询格式是否存在（1=存在, 0=不存在, -1=出错） */
int zlog_has_format(const char *name);

/* 查询某分类是否有规则（1=有, 0=没有, -1=出错） */
int zlog_has_category_rules(const char *category);
```

## 使用场景

1. **插件化架构**：插件加载时动态添加其日志分类
2. **运行时配置调整**：根据用户命令或远程配置动态改变日志级别
3. **模块化系统**：每个模块启动时注册自己的日志规则
4. **程序解耦**：不同模块无需提前在配置文件中声明
5. **分散加载**：各模块独立加载自己的配置，重复加载自动覆盖

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

### modular_demo.c
模块化分散加载示例，演示：
- 每个模块独立注册自己的格式和规则
- 重复加载同一模块时自动覆盖旧规则
- 格式名冲突的检测与处理
- 使用 `zlog_add_format()`、`zlog_add_rule()`、`zlog_remove_rules()` 等新 API

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

# 设置库路径并运行
export LD_LIBRARY_PATH=../../src:$LD_LIBRARY_PATH
./simple_demo
./dynamic_demo
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

如果需要增量添加，使用模块化 API：`zlog_add_format()`、`zlog_add_rule()`、`zlog_remove_rules()`。

### 5. 模块化加载（分散加载）

使用模块化 API 可以实现每个模块独立加载配置，无需重建完整配置：

```c
/* 每个模块独立注册自己的日志配置 */
void module_init_logging(const char *module_name) {
    char rule[256];

    /* 检查是否已加载过（避免重复） */
    if (zlog_has_category_rules(module_name)) {
        /* 先清除旧规则 */
        zlog_remove_rules(module_name);
    }

    /* 添加模块专用格式（同名会覆盖） */
    char fmt_name[64];
    snprintf(fmt_name, sizeof(fmt_name), "%s_fmt", module_name);
    zlog_add_format(fmt_name, "%d [%c] [%-5V] %m%n");

    /* 添加模块规则 */
    snprintf(rule, sizeof(rule), "%s.DEBUG >stdout; %s", module_name, fmt_name);
    zlog_add_rule(rule);
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

| 方面 | 配置文件 | 动态配置字符串 | 模块化 API |
|------|----------|----------------|------------|
| 灵活性 | 静态，需要提前定义 | 动态，运行时构建 | 动态，增量添加 |
| 解耦性 | 所有模块耦合在一个文件 | 每个模块独立管理 | 每个模块完全独立 |
| 调试 | 修改文件需要 reload | 可通过命令即时调整 | 可逐条添加/删除 |
| 复杂度 | 简单，直接编辑文件 | 需要编程构建字符串 | API 调用，最简单 |
| 重复处理 | 手动避免 | 手动避免 | 自动检测和覆盖 |
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
A: 使用 `zlog_reload_from_string()` 时不行，每次 reload 都是完整替换。
但使用模块化 API（`zlog_add_rule()`）可以增量添加规则，不影响现有配置。

**Q: 多个模块格式名冲突怎么办？**
A: 使用 `zlog_add_format()` 时，同名格式会被新定义覆盖。建议给格式名加模块前缀（如 `auth_fmt`）以避免冲突。

**Q: 重复加载同一模块怎么处理？**
A: 使用 `zlog_has_category_rules()` 检查是否已加载，如果是则先调用 `zlog_remove_rules()` 清除旧规则，再用 `zlog_add_rule()` 添加新规则。

**Q: 配置字符串中的百分号需要转义吗？**
A: 在 C 字符串中，格式化符号的百分号需要写成 `%%`（两个百分号）。

**Q: 是否可以从网络接收配置？**
A: 可以，但需要注意安全性，建议验证和清理配置内容。

## 参考

- zlog 主文档：`../../README.md`
- API 参考：`../../src/zlog.h`
- 配置文件格式：`../../test/test_hello.conf`

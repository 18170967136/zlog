/*
 * zlog_modular.h - 模块化分散加载配置的封装层
 *
 * 基于 zlog 现有的 zlog_reload_from_string() 接口实现增量式模块配置管理。
 * 每个模块可以独立注册自己的格式和规则，本层维护一个全局注册表，
 * 在每次变更后自动重建完整配置字符串并调用 zlog_reload_from_string()。
 *
 * 内部使用 cJSON 管理配置数据，支持通过 zlog_mod_dump_json() 导出
 * 当前所有模块的注册状态为 JSON 文档，方便观测和调试。
 *
 * 特点：
 *   - 不修改 zlog 源码，仅使用公共 API
 *   - 线程安全（使用 pthread_mutex 保护注册表）
 *   - 支持重复加载检测和覆盖
 *   - 支持按模块名卸载
 *   - 支持导出 JSON 文档，提升可观测性
 *
 * 用法：
 *   1. 先调用 zlog_init_from_string() 初始化 zlog（基础配置）
 *   2. 调用 zlog_mod_init() 初始化模块管理器（传入全局部分的配置）
 *   3. 各模块调用 zlog_mod_register() 注册自己的格式和规则
 *   4. 可随时调用 zlog_mod_dump_json() 查看当前配置状态
 *   5. 程序退出时调用 zlog_mod_fini() 清理
 */

#ifndef __zlog_modular_h
#define __zlog_modular_h

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化模块管理器
 *
 * @param global_conf  [global] 段的配置内容（不含段标记），例如：
 *                     "strict init = false\nbuffer min = 1KB\n"
 *                     可以为 NULL，表示使用默认全局设置
 * @return 0 成功，-1 失败
 */
int zlog_mod_init(const char *global_conf);

/**
 * 清理模块管理器，释放所有注册信息
 * 注意：不调用 zlog_fini()，需要用户自行调用
 */
void zlog_mod_fini(void);

/**
 * 注册一个模块的日志配置
 *
 * 如果同名模块已存在，会先卸载旧配置再注册新的（覆盖行为）。
 * 注册后会自动重建配置并调用 zlog_reload_from_string()。
 *
 * @param module_name    模块名称（唯一标识）
 * @param formats        格式定义数组，每项为 "name = \"pattern\"" 形式的字符串，
 *                       与配置文件 [formats] 段中的行格式相同。可以为 NULL。
 * @param format_count   格式数组长度
 * @param rules          规则定义数组，每项为 "category.LEVEL output; format" 形式，
 *                       与配置文件 [rules] 段中的行格式相同。
 * @param rule_count     规则数组长度
 * @return 0 成功，-1 失败
 */
int zlog_mod_register(const char *module_name,
                      const char *formats[], int format_count,
                      const char *rules[], int rule_count);

/**
 * 卸载一个模块的日志配置
 *
 * 卸载后会自动重建配置并调用 zlog_reload_from_string()。
 *
 * @param module_name  模块名称
 * @return 0 成功，-1 未找到该模块
 */
int zlog_mod_unregister(const char *module_name);

/**
 * 检查某个模块是否已注册
 *
 * @param module_name  模块名称
 * @return 1 已注册，0 未注册
 */
int zlog_mod_has_module(const char *module_name);

/**
 * 获取当前已注册的模块数量
 *
 * @return 模块数量
 */
int zlog_mod_count(void);

/**
 * 导出当前所有模块的注册状态为 JSON 字符串
 *
 * 返回的 JSON 文档格式如下：
 * {
 *   "global_conf": "strict init = false",
 *   "module_count": 2,
 *   "modules": {
 *     "auth": {
 *       "formats": ["auth_fmt = \"%d [AUTH] %m%n\""],
 *       "rules": ["auth.DEBUG >stdout; auth_fmt"]
 *     },
 *     "api": {
 *       "formats": ["api_fmt = \"%d [API] %m%n\""],
 *       "rules": ["api.INFO >stdout; api_fmt"]
 *     }
 *   }
 * }
 *
 * @return  JSON 字符串（调用者负责用 free() 释放），失败返回 NULL
 */
char *zlog_mod_dump_json(void);

#ifdef __cplusplus
}
#endif

#endif

/*
 * zlog_modular.cpp - 模块化分散加载配置的封装层实现（C++ 版）
 *
 * 使用 nlohmann::json 维护各模块的格式和规则注册信息，
 * 使用 std::mutex + std::lock_guard（RAII）实现线程安全，
 * 每次变更后自动重建完整配置字符串并调用 zlog_reload_from_string()。
 * 可通过 zlog_mod_dump_json() 导出 JSON 文档以便观测当前配置状态。
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <mutex>
#include <vector>

#include "json.hpp"

extern "C" {
#include "zlog.h"
}

#include "zlog_modular.h"

using json = nlohmann::json;

/* ========== 内部数据结构 ========== */

/*
 * 全局状态
 *
 * 内部 JSON 文档结构：
 * {
 *   "global_conf": "strict init = false",
 *   "module_count": 2,
 *   "modules": {
 *     "auth": {
 *       "formats": ["auth_fmt = \"%d [AUTH] %m%n\""],
 *       "rules": ["auth.DEBUG >stdout; auth_fmt"]
 *     },
 *     "api": { ... }
 *   }
 * }
 */
static struct {
    bool inited = false;
    std::mutex lock;
    json root;
} g_state;

/* ========== 内部辅助函数 ========== */

/*
 * 重建完整配置字符串并调用 zlog_reload_from_string()
 * 调用者必须持有 g_state.lock
 */
static int rebuild_and_reload_unlocked()
{
    const auto &modules = g_state.root["modules"];
    std::string global_conf;

    if (g_state.root.contains("global_conf") && g_state.root["global_conf"].is_string()) {
        global_conf = g_state.root["global_conf"].get<std::string>();
    }

    /* 构建配置字符串 */
    std::string config;
    config.reserve(1024);

    /* [global] section */
    config += "[global]\n";
    if (!global_conf.empty()) {
        config += global_conf;
        config += "\n";
    } else {
        config += "strict init = false\n";
    }

    /* [formats] section */
    config += "\n[formats]\n";
    for (auto it = modules.begin(); it != modules.end(); ++it) {
        const auto &mod = it.value();
        if (mod.contains("formats") && mod["formats"].is_array()) {
            for (const auto &fmt : mod["formats"]) {
                if (fmt.is_string()) {
                    config += fmt.get<std::string>();
                    config += "\n";
                }
            }
        }
    }

    /* [rules] section */
    config += "\n[rules]\n";
    for (auto it = modules.begin(); it != modules.end(); ++it) {
        const auto &mod = it.value();
        if (mod.contains("rules") && mod["rules"].is_array()) {
            for (const auto &rule : mod["rules"]) {
                if (rule.is_string()) {
                    config += rule.get<std::string>();
                    config += "\n";
                }
            }
        }
    }

    /* Apply the config */
    int ret = zlog_reload_from_string(config.c_str());
    if (ret) {
        fprintf(stderr, "zlog_modular: zlog_reload_from_string failed\n");
        return -1;
    }

    return 0;
}

/* ========== 公共 API（extern "C"） ========== */

extern "C" {

int zlog_mod_init(const char *global_conf)
{
    std::lock_guard<std::mutex> guard(g_state.lock);

    /* 重新初始化：清理旧数据 */
    g_state.root = json::object();
    g_state.root["global_conf"] = global_conf ? global_conf : "";
    g_state.root["module_count"] = 0;
    g_state.root["modules"] = json::object();
    g_state.inited = true;

    return 0;
}

void zlog_mod_fini(void)
{
    std::lock_guard<std::mutex> guard(g_state.lock);

    g_state.root = json();
    g_state.inited = false;
}

int zlog_mod_register(const char *module_name,
                      const char *formats[], int format_count,
                      const char *rules[], int rule_count)
{
    if (!module_name) return -1;

    std::lock_guard<std::mutex> guard(g_state.lock);

    if (!g_state.inited) {
        fprintf(stderr, "zlog_modular: not initialized, call zlog_mod_init() first\n");
        return -1;
    }

    auto &modules = g_state.root["modules"];

    /* If module already exists, remove it first (override) */
    if (modules.contains(module_name)) {
        modules.erase(module_name);
        g_state.root["module_count"] = g_state.root["module_count"].get<int>() - 1;
    }

    /* Create new module entry */
    json entry = json::object();

    /* Add formats array */
    json fmt_array = json::array();
    for (int i = 0; i < format_count && formats; i++) {
        fmt_array.push_back(formats[i]);
    }
    entry["formats"] = std::move(fmt_array);

    /* Add rules array */
    json rule_array = json::array();
    for (int i = 0; i < rule_count && rules; i++) {
        rule_array.push_back(rules[i]);
    }
    entry["rules"] = std::move(rule_array);

    /* Add entry to modules object */
    modules[module_name] = std::move(entry);
    g_state.root["module_count"] = g_state.root["module_count"].get<int>() + 1;

    /* Rebuild and reload */
    return rebuild_and_reload_unlocked();
}

int zlog_mod_unregister(const char *module_name)
{
    if (!module_name) return -1;

    std::lock_guard<std::mutex> guard(g_state.lock);

    if (!g_state.inited) {
        return -1;
    }

    auto &modules = g_state.root["modules"];
    if (!modules.contains(module_name)) {
        return -1; /* not found */
    }

    modules.erase(module_name);
    g_state.root["module_count"] = g_state.root["module_count"].get<int>() - 1;

    /* Rebuild and reload */
    return rebuild_and_reload_unlocked();
}

int zlog_mod_has_module(const char *module_name)
{
    if (!module_name) return 0;

    std::lock_guard<std::mutex> guard(g_state.lock);

    if (!g_state.inited) {
        return 0;
    }

    const auto &modules = g_state.root["modules"];
    return modules.contains(module_name) ? 1 : 0;
}

int zlog_mod_count(void)
{
    std::lock_guard<std::mutex> guard(g_state.lock);

    if (!g_state.inited) {
        return 0;
    }

    return g_state.root["module_count"].get<int>();
}

char *zlog_mod_dump_json(void)
{
    std::lock_guard<std::mutex> guard(g_state.lock);

    if (!g_state.inited) {
        return nullptr;
    }

    /* dump(4) 返回 4 空格缩进的格式化 JSON 字符串 */
    std::string json_str = g_state.root.dump(4);

    /* 返回 C 风格字符串，调用者用 free() 释放 */
    char *result = static_cast<char *>(malloc(json_str.size() + 1));
    if (!result) {
        return nullptr;
    }
    memcpy(result, json_str.c_str(), json_str.size() + 1);
    return result;
}

} /* extern "C" */

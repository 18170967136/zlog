/*
 * zlog_modular.c - 模块化分散加载配置的封装层实现
 *
 * 使用 cJSON 维护各模块的格式和规则注册信息，
 * 每次变更后自动重建完整配置字符串并调用 zlog_reload_from_string()。
 * 可通过 zlog_mod_dump_json() 导出 JSON 文档以便观测当前配置状态。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "zlog.h"
#include "zlog_modular.h"
#include "cJSON.h"

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
    int inited;
    pthread_mutex_t lock;
    cJSON *root;           /* JSON 根对象 */
} g_state = {
    .inited = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .root = NULL,
};

/* ========== 内部辅助函数 ========== */

/* 获取 modules 对象（不加锁，由调用者负责加锁） */
static cJSON *get_modules_unlocked(void)
{
    return cJSON_GetObjectItemCaseSensitive(g_state.root, "modules");
}

/* 获取 module_count 数值（不加锁） */
static int get_module_count_unlocked(void)
{
    cJSON *cnt = cJSON_GetObjectItemCaseSensitive(g_state.root, "module_count");
    if (cnt && cJSON_IsNumber(cnt)) {
        return cnt->valueint;
    }
    return 0;
}

/* 设置 module_count 数值（不加锁） */
static void set_module_count_unlocked(int count)
{
    cJSON *cnt = cJSON_GetObjectItemCaseSensitive(g_state.root, "module_count");
    if (cnt) {
        cJSON_SetNumberValue(cnt, count);
    }
}

/*
 * 重建完整配置字符串并调用 zlog_reload_from_string()
 * 调用者必须持有 g_state.lock
 */
static int rebuild_and_reload_unlocked(void)
{
    cJSON *modules = get_modules_unlocked();
    cJSON *global_conf_item = cJSON_GetObjectItemCaseSensitive(g_state.root, "global_conf");
    const char *global_conf = NULL;

    if (global_conf_item && cJSON_IsString(global_conf_item)) {
        global_conf = global_conf_item->valuestring;
    }

    /*
     * 估算配置缓冲区大小：
     * [global] + global_conf + [formats] + all formats + [rules] + all rules
     */
    size_t buf_size = 1024; /* 基础部分 */

    if (global_conf) {
        buf_size += strlen(global_conf) + 1;
    }

    /* 遍历所有模块，统计 formats 和 rules 的总大小 */
    /* 注：cJSON_ArrayForEach 可用于 object 和 array，两者内部均为 child 链表 */
    cJSON *mod = NULL;
    cJSON_ArrayForEach(mod, modules) {
        cJSON *formats = cJSON_GetObjectItemCaseSensitive(mod, "formats");
        cJSON *rules = cJSON_GetObjectItemCaseSensitive(mod, "rules");
        cJSON *item = NULL;

        cJSON_ArrayForEach(item, formats) {
            if (cJSON_IsString(item)) {
                buf_size += strlen(item->valuestring) + 2;
            }
        }
        cJSON_ArrayForEach(item, rules) {
            if (cJSON_IsString(item)) {
                buf_size += strlen(item->valuestring) + 2;
            }
        }
    }

    char *config = malloc(buf_size);
    if (!config) {
        fprintf(stderr, "zlog_modular: malloc(%zu) failed for config buffer\n", buf_size);
        return -1;
    }

    size_t offset = 0;
    int ret;

    /* [global] section */
    ret = snprintf(config + offset, buf_size - offset, "[global]\n");
    if (ret < 0 || (size_t)ret >= buf_size - offset) goto err;
    offset += ret;

    if (global_conf && global_conf[0]) {
        ret = snprintf(config + offset, buf_size - offset, "%s\n", global_conf);
        if (ret < 0 || (size_t)ret >= buf_size - offset) goto err;
        offset += ret;
    } else {
        ret = snprintf(config + offset, buf_size - offset, "strict init = false\n");
        if (ret < 0 || (size_t)ret >= buf_size - offset) goto err;
        offset += ret;
    }

    /* [formats] section */
    ret = snprintf(config + offset, buf_size - offset, "\n[formats]\n");
    if (ret < 0 || (size_t)ret >= buf_size - offset) goto err;
    offset += ret;

    cJSON_ArrayForEach(mod, modules) {
        cJSON *formats = cJSON_GetObjectItemCaseSensitive(mod, "formats");
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, formats) {
            if (cJSON_IsString(item)) {
                ret = snprintf(config + offset, buf_size - offset, "%s\n", item->valuestring);
                if (ret < 0 || (size_t)ret >= buf_size - offset) goto err;
                offset += ret;
            }
        }
    }

    /* [rules] section */
    ret = snprintf(config + offset, buf_size - offset, "\n[rules]\n");
    if (ret < 0 || (size_t)ret >= buf_size - offset) goto err;
    offset += ret;

    cJSON_ArrayForEach(mod, modules) {
        cJSON *rules = cJSON_GetObjectItemCaseSensitive(mod, "rules");
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, rules) {
            if (cJSON_IsString(item)) {
                ret = snprintf(config + offset, buf_size - offset, "%s\n", item->valuestring);
                if (ret < 0 || (size_t)ret >= buf_size - offset) goto err;
                offset += ret;
            }
        }
    }

    /* Apply the config */
    ret = zlog_reload_from_string(config);
    free(config);

    if (ret) {
        fprintf(stderr, "zlog_modular: zlog_reload_from_string failed\n");
        return -1;
    }

    return 0;

err:
    free(config);
    return -1;
}

/* ========== 公共 API ========== */

int zlog_mod_init(const char *global_conf)
{
    pthread_mutex_lock(&g_state.lock);

    if (g_state.inited) {
        /* 已经初始化过，先清理旧数据 */
        cJSON_Delete(g_state.root);
        g_state.root = NULL;
    }

    /* 创建根 JSON 对象 */
    g_state.root = cJSON_CreateObject();
    if (!g_state.root) {
        pthread_mutex_unlock(&g_state.lock);
        return -1;
    }

    /* 设置 global_conf */
    if (global_conf) {
        cJSON_AddStringToObject(g_state.root, "global_conf", global_conf);
    } else {
        cJSON_AddStringToObject(g_state.root, "global_conf", "");
    }

    /* 初始化 module_count */
    cJSON_AddNumberToObject(g_state.root, "module_count", 0);

    /* 创建 modules 对象 */
    cJSON_AddObjectToObject(g_state.root, "modules");

    g_state.inited = 1;

    pthread_mutex_unlock(&g_state.lock);
    return 0;
}

void zlog_mod_fini(void)
{
    pthread_mutex_lock(&g_state.lock);

    cJSON_Delete(g_state.root);
    g_state.root = NULL;
    g_state.inited = 0;

    pthread_mutex_unlock(&g_state.lock);
}

int zlog_mod_register(const char *module_name,
                      const char *formats[], int format_count,
                      const char *rules[], int rule_count)
{
    int i, rc;

    if (!module_name) return -1;

    pthread_mutex_lock(&g_state.lock);

    if (!g_state.inited) {
        fprintf(stderr, "zlog_modular: not initialized, call zlog_mod_init() first\n");
        pthread_mutex_unlock(&g_state.lock);
        return -1;
    }

    cJSON *modules = get_modules_unlocked();
    if (!modules) {
        pthread_mutex_unlock(&g_state.lock);
        return -1;
    }

    /* If module already exists, remove it first (override) */
    if (cJSON_HasObjectItem(modules, module_name)) {
        cJSON_DeleteItemFromObjectCaseSensitive(modules, module_name);
        set_module_count_unlocked(get_module_count_unlocked() - 1);
    }

    /* Create new module entry */
    cJSON *entry = cJSON_CreateObject();
    if (!entry) {
        pthread_mutex_unlock(&g_state.lock);
        return -1;
    }

    /* Add formats array */
    cJSON *fmt_array = cJSON_AddArrayToObject(entry, "formats");
    if (!fmt_array) {
        cJSON_Delete(entry);
        pthread_mutex_unlock(&g_state.lock);
        return -1;
    }
    for (i = 0; i < format_count && formats; i++) {
        cJSON_AddItemToArray(fmt_array, cJSON_CreateString(formats[i]));
    }

    /* Add rules array */
    cJSON *rule_array = cJSON_AddArrayToObject(entry, "rules");
    if (!rule_array) {
        cJSON_Delete(entry);
        pthread_mutex_unlock(&g_state.lock);
        return -1;
    }
    for (i = 0; i < rule_count && rules; i++) {
        cJSON_AddItemToArray(rule_array, cJSON_CreateString(rules[i]));
    }

    /* Add entry to modules object */
    cJSON_AddItemToObject(modules, module_name, entry);
    set_module_count_unlocked(get_module_count_unlocked() + 1);

    /* Rebuild and reload */
    rc = rebuild_and_reload_unlocked();

    pthread_mutex_unlock(&g_state.lock);
    return rc;
}

int zlog_mod_unregister(const char *module_name)
{
    int rc;

    if (!module_name) return -1;

    pthread_mutex_lock(&g_state.lock);

    if (!g_state.inited) {
        pthread_mutex_unlock(&g_state.lock);
        return -1;
    }

    cJSON *modules = get_modules_unlocked();
    if (!modules || !cJSON_HasObjectItem(modules, module_name)) {
        pthread_mutex_unlock(&g_state.lock);
        return -1; /* not found */
    }

    cJSON_DeleteItemFromObjectCaseSensitive(modules, module_name);
    set_module_count_unlocked(get_module_count_unlocked() - 1);

    /* Rebuild and reload */
    rc = rebuild_and_reload_unlocked();

    pthread_mutex_unlock(&g_state.lock);
    return rc;
}

int zlog_mod_has_module(const char *module_name)
{
    int found;

    if (!module_name) return 0;

    pthread_mutex_lock(&g_state.lock);
    if (!g_state.inited || !g_state.root) {
        pthread_mutex_unlock(&g_state.lock);
        return 0;
    }
    cJSON *modules = get_modules_unlocked();
    found = (modules && cJSON_HasObjectItem(modules, module_name)) ? 1 : 0;
    pthread_mutex_unlock(&g_state.lock);

    return found;
}

int zlog_mod_count(void)
{
    int count;

    pthread_mutex_lock(&g_state.lock);
    if (!g_state.inited || !g_state.root) {
        pthread_mutex_unlock(&g_state.lock);
        return 0;
    }
    count = get_module_count_unlocked();
    pthread_mutex_unlock(&g_state.lock);

    return count;
}

char *zlog_mod_dump_json(void)
{
    char *json_str = NULL;

    pthread_mutex_lock(&g_state.lock);

    if (!g_state.inited || !g_state.root) {
        pthread_mutex_unlock(&g_state.lock);
        return NULL;
    }

    /* cJSON_Print 返回格式化的 JSON 字符串（调用者用 free() 释放） */
    json_str = cJSON_Print(g_state.root);

    pthread_mutex_unlock(&g_state.lock);
    return json_str;
}

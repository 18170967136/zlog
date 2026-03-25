/*
 * zlog_modular.c - 模块化分散加载配置的封装层实现
 *
 * 使用链表维护各模块的格式和规则注册信息，
 * 每次变更后自动重建完整配置字符串并调用 zlog_reload_from_string()。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "zlog.h"
#include "zlog_modular.h"

/* ========== 内部数据结构 ========== */

/* 单条配置行（格式或规则） */
typedef struct config_line_s {
    char *text;
    struct config_line_s *next;
} config_line_t;

/* 模块注册信息 */
typedef struct module_entry_s {
    char *name;
    config_line_t *formats;    /* 格式链表 */
    config_line_t *rules;      /* 规则链表 */
    struct module_entry_s *next;
} module_entry_t;

/* 全局状态 */
static struct {
    int inited;
    pthread_mutex_t lock;
    char *global_conf;          /* [global] 段内容 */
    module_entry_t *modules;    /* 模块链表 */
    int module_count;
} g_state = {
    .inited = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .global_conf = NULL,
    .modules = NULL,
    .module_count = 0,
};

/* ========== 内部辅助函数 ========== */

static config_line_t *config_line_new(const char *text)
{
    config_line_t *line = calloc(1, sizeof(config_line_t));
    if (!line) return NULL;
    line->text = strdup(text);
    if (!line->text) {
        free(line);
        return NULL;
    }
    line->next = NULL;
    return line;
}

static void config_line_list_free(config_line_t *head)
{
    config_line_t *cur, *next;
    for (cur = head; cur; cur = next) {
        next = cur->next;
        free(cur->text);
        free(cur);
    }
}

static module_entry_t *module_entry_new(const char *name)
{
    module_entry_t *entry = calloc(1, sizeof(module_entry_t));
    if (!entry) return NULL;
    entry->name = strdup(name);
    if (!entry->name) {
        free(entry);
        return NULL;
    }
    entry->formats = NULL;
    entry->rules = NULL;
    entry->next = NULL;
    return entry;
}

static void module_entry_free(module_entry_t *entry)
{
    if (!entry) return;
    free(entry->name);
    config_line_list_free(entry->formats);
    config_line_list_free(entry->rules);
    free(entry);
}

static void module_list_free(module_entry_t *head)
{
    module_entry_t *cur, *next;
    for (cur = head; cur; cur = next) {
        next = cur->next;
        module_entry_free(cur);
    }
}

/* 在链表末尾追加配置行 */
static int config_line_list_append(config_line_t **head, const char *text)
{
    config_line_t *line = config_line_new(text);
    if (!line) return -1;

    if (!*head) {
        *head = line;
    } else {
        config_line_t *tail = *head;
        while (tail->next) tail = tail->next;
        tail->next = line;
    }
    return 0;
}

/* 查找模块（不加锁，由调用者负责加锁） */
static module_entry_t *find_module_unlocked(const char *name)
{
    module_entry_t *cur;
    for (cur = g_state.modules; cur; cur = cur->next) {
        if (strcmp(cur->name, name) == 0) {
            return cur;
        }
    }
    return NULL;
}

/* 卸载模块（不加锁，由调用者负责加锁） */
static int unregister_module_unlocked(const char *name)
{
    module_entry_t *cur, *prev = NULL;
    for (cur = g_state.modules; cur; prev = cur, cur = cur->next) {
        if (strcmp(cur->name, name) == 0) {
            if (prev) {
                prev->next = cur->next;
            } else {
                g_state.modules = cur->next;
            }
            module_entry_free(cur);
            g_state.module_count--;
            return 0;
        }
    }
    return -1; /* not found */
}

/*
 * 重建完整配置字符串并调用 zlog_reload_from_string()
 * 调用者必须持有 g_state.lock
 */
static int rebuild_and_reload_unlocked(void)
{
    /*
     * 估算配置缓冲区大小：
     * [global] + global_conf + [formats] + all formats + [rules] + all rules
     */
    size_t buf_size = 1024; /* 基础部分 */
    module_entry_t *mod;
    config_line_t *line;

    /* 先计算需要多大的缓冲区 */
    for (mod = g_state.modules; mod; mod = mod->next) {
        for (line = mod->formats; line; line = line->next) {
            buf_size += strlen(line->text) + 2; /* +2 for \n and safety */
        }
        for (line = mod->rules; line; line = line->next) {
            buf_size += strlen(line->text) + 2;
        }
    }
    if (g_state.global_conf) {
        buf_size += strlen(g_state.global_conf) + 1;
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

    if (g_state.global_conf && g_state.global_conf[0]) {
        ret = snprintf(config + offset, buf_size - offset, "%s\n", g_state.global_conf);
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

    for (mod = g_state.modules; mod; mod = mod->next) {
        for (line = mod->formats; line; line = line->next) {
            ret = snprintf(config + offset, buf_size - offset, "%s\n", line->text);
            if (ret < 0 || (size_t)ret >= buf_size - offset) goto err;
            offset += ret;
        }
    }

    /* [rules] section */
    ret = snprintf(config + offset, buf_size - offset, "\n[rules]\n");
    if (ret < 0 || (size_t)ret >= buf_size - offset) goto err;
    offset += ret;

    for (mod = g_state.modules; mod; mod = mod->next) {
        for (line = mod->rules; line; line = line->next) {
            ret = snprintf(config + offset, buf_size - offset, "%s\n", line->text);
            if (ret < 0 || (size_t)ret >= buf_size - offset) goto err;
            offset += ret;
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
        module_list_free(g_state.modules);
        g_state.modules = NULL;
        g_state.module_count = 0;
        free(g_state.global_conf);
        g_state.global_conf = NULL;
    }

    if (global_conf) {
        g_state.global_conf = strdup(global_conf);
        if (!g_state.global_conf) {
            pthread_mutex_unlock(&g_state.lock);
            return -1;
        }
    }

    g_state.inited = 1;

    pthread_mutex_unlock(&g_state.lock);
    return 0;
}

void zlog_mod_fini(void)
{
    pthread_mutex_lock(&g_state.lock);

    module_list_free(g_state.modules);
    g_state.modules = NULL;
    g_state.module_count = 0;

    free(g_state.global_conf);
    g_state.global_conf = NULL;

    g_state.inited = 0;

    pthread_mutex_unlock(&g_state.lock);
}

int zlog_mod_register(const char *module_name,
                      const char *formats[], int format_count,
                      const char *rules[], int rule_count)
{
    int i, rc;
    module_entry_t *entry;

    if (!module_name) return -1;

    pthread_mutex_lock(&g_state.lock);

    if (!g_state.inited) {
        fprintf(stderr, "zlog_modular: not initialized, call zlog_mod_init() first\n");
        pthread_mutex_unlock(&g_state.lock);
        return -1;
    }

    /* If module already exists, remove it first (override) */
    if (find_module_unlocked(module_name)) {
        unregister_module_unlocked(module_name);
    }

    /* Create new entry */
    entry = module_entry_new(module_name);
    if (!entry) {
        pthread_mutex_unlock(&g_state.lock);
        return -1;
    }

    /* Add formats */
    for (i = 0; i < format_count && formats; i++) {
        if (config_line_list_append(&entry->formats, formats[i])) {
            module_entry_free(entry);
            pthread_mutex_unlock(&g_state.lock);
            return -1;
        }
    }

    /* Add rules */
    for (i = 0; i < rule_count && rules; i++) {
        if (config_line_list_append(&entry->rules, rules[i])) {
            module_entry_free(entry);
            pthread_mutex_unlock(&g_state.lock);
            return -1;
        }
    }

    /* Insert at head */
    entry->next = g_state.modules;
    g_state.modules = entry;
    g_state.module_count++;

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

    if (unregister_module_unlocked(module_name) != 0) {
        pthread_mutex_unlock(&g_state.lock);
        return -1; /* not found */
    }

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
    found = (find_module_unlocked(module_name) != NULL) ? 1 : 0;
    pthread_mutex_unlock(&g_state.lock);

    return found;
}

int zlog_mod_count(void)
{
    int count;

    pthread_mutex_lock(&g_state.lock);
    count = g_state.module_count;
    pthread_mutex_unlock(&g_state.lock);

    return count;
}

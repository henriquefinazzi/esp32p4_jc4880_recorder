#include "i18n.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

extern const char pt_json_start[] asm("_binary_pt_json_start");
extern const char en_json_start[] asm("_binary_en_json_start");

static const char *TAG = "i18n";

// Tabela de um idioma: pares chave/texto ordenados pela chave (busca binária).
typedef struct {
    char code[I18N_CODE_MAX + 1];
    int count;
    const char **keys;
    const char **values;
    char *pool; // todas as strings num bloco só
} table_t;

// Idiomas já carregados ficam na memória (nunca são liberados): o servidor web lê textos
// em outra tarefa, e trocar de idioma só troca o ponteiro s_cur.
static table_t s_base;                     // português, sempre carregado
static table_t s_cache[I18N_MAX_LANGS];    // outros idiomas carregados
static int s_cached;
static const table_t *volatile s_cur;      // NULL: português
static char s_code[I18N_CODE_MAX + 1] = "pt";

static void table_free(table_t *t)
{
    free(t->keys);
    free(t->values);
    free(t->pool);
    memset(t, 0, sizeof(*t));
}


typedef struct {
    const char *k, *v;
} pair_t;

static int cmp_pair(const void *a, const void *b)
{
    return strcmp(((const pair_t *)a)->k, ((const pair_t *)b)->k);
}

static bool table_load(table_t *t, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    int n = 0;
    size_t bytes = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, root)
    {
        if (!cJSON_IsString(it)) continue;
        n++;
        bytes += strlen(it->string) + strlen(it->valuestring) + 2;
    }
    table_t nt = {0};
    nt.pool = heap_caps_malloc(bytes ? bytes : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    pair_t *pairs = heap_caps_malloc((n ? n : 1) * sizeof(pair_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    nt.keys = heap_caps_malloc((n ? n : 1) * sizeof(char *), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    nt.values = heap_caps_malloc((n ? n : 1) * sizeof(char *), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!nt.pool || !pairs || !nt.keys || !nt.values) {
        free(pairs);
        table_free(&nt);
        cJSON_Delete(root);
        return false;
    }
    char *p = nt.pool;
    int i = 0;
    cJSON_ArrayForEach(it, root)
    {
        if (!cJSON_IsString(it)) continue;
        size_t kl = strlen(it->string) + 1, vl = strlen(it->valuestring) + 1;
        memcpy(p, it->string, kl);
        pairs[i].k = p;
        p += kl;
        memcpy(p, it->valuestring, vl);
        pairs[i].v = p;
        p += vl;
        if (strcmp(it->string, "_codigo") == 0) snprintf(nt.code, sizeof(nt.code), "%s", it->valuestring);
        i++;
    }
    cJSON_Delete(root);
    qsort(pairs, n, sizeof(pair_t), cmp_pair);
    for (i = 0; i < n; i++) {
        nt.keys[i] = pairs[i].k;
        nt.values[i] = pairs[i].v;
    }
    free(pairs);
    nt.count = n;
    table_free(t);
    *t = nt;
    return true;
}

static const char *table_get(const table_t *t, const char *key)
{
    int lo = 0, hi = t->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(key, t->keys[mid]);
        if (c == 0) return t->values[mid];
        if (c < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return NULL;
}

// Texto JSON de um idioma: embutido ou do cartão (malloc; liberar). NULL se não achar.
static char *source_for(const char *code, bool *builtin)
{
    *builtin = true;
    if (strcmp(code, "pt") == 0) return strdup(pt_json_start);
    if (strcmp(code, "en") == 0) return strdup(en_json_start);
    *builtin = false;
    char path[64];
    snprintf(path, sizeof(path), I18N_DIR "/%s.json", code);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (len > 0 && len < 256 * 1024) ? heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    if (buf && fread(buf, 1, len, f) == (size_t)len) {
        buf[len] = 0;
    } else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

void i18n_init(const char *code)
{
    if (!table_load(&s_base, pt_json_start)) ESP_LOGE(TAG, "pt.json embutido com erro");
    if (code && code[0] && !i18n_set(code)) ESP_LOGW(TAG, "idioma %s indisponivel; usando pt", code);
}

bool i18n_set(const char *code)
{
    if (!code || !code[0] || strlen(code) > I18N_CODE_MAX) return false;
    if (strcmp(code, "pt") == 0) {
        s_cur = NULL;
        snprintf(s_code, sizeof(s_code), "pt");
        return true;
    }
    for (int i = 0; i < s_cached; i++) {
        if (strcmp(s_cache[i].code, code) == 0) {
            s_cur = &s_cache[i];
            snprintf(s_code, sizeof(s_code), "%s", code);
            return true;
        }
    }
    if (s_cached >= I18N_MAX_LANGS) return false;
    bool builtin;
    char *json = source_for(code, &builtin);
    if (!json) return false;
    table_t t = {0};
    bool ok = table_load(&t, json);
    free(json);
    if (!ok) {
        ESP_LOGW(TAG, "%s.json com erro", code);
        return false;
    }
    snprintf(t.code, sizeof(t.code), "%s", code); // o código vale pelo nome do arquivo
    s_cache[s_cached] = t;
    s_cur = &s_cache[s_cached++];
    snprintf(s_code, sizeof(s_code), "%s", code);
    return true;
}

const char *i18n_current(void)
{
    return s_code;
}

const char *T(const char *key)
{
    const table_t *cur = s_cur;
    const char *v = cur ? table_get(cur, key) : NULL;
    if (!v) v = table_get(&s_base, key);
    return v ? v : key;
}

char *i18n_export(const char *const *prefixes, int n)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddStringToObject(root, "_codigo", s_code);
    for (int i = 0; i < s_base.count; i++) {
        for (int p = 0; p < n; p++) {
            if (strncmp(s_base.keys[i], prefixes[p], strlen(prefixes[p])) == 0) {
                cJSON_AddStringToObject(root, s_base.keys[i], T(s_base.keys[i]));
                break;
            }
        }
    }
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

bool i18n_parse_header(const char *json, char code[I18N_CODE_MAX + 1], char name[I18N_NAME_MAX + 1])
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "_codigo");
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(root, "_nome");
    bool ok = cJSON_IsString(c) && cJSON_IsString(n) && c->valuestring[0] && strlen(c->valuestring) <= I18N_CODE_MAX;
    if (ok) {
        snprintf(code, I18N_CODE_MAX + 1, "%s", c->valuestring);
        snprintf(name, I18N_NAME_MAX + 1, "%s", n->valuestring);
    }
    cJSON_Delete(root);
    return ok;
}

int i18n_list(i18n_lang_t *out, int max)
{
    int n = 0;
    const char *builtins[] = {pt_json_start, en_json_start};
    for (int i = 0; i < 2 && n < max; i++) {
        if (i18n_parse_header(builtins[i], out[n].code, out[n].name)) {
            out[n].builtin = true;
            n++;
        }
    }
    DIR *d = opendir(I18N_DIR);
    if (!d) return n;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        size_t l = strlen(e->d_name);
        if (l < 6 || strcasecmp(e->d_name + l - 5, ".json") != 0 || e->d_name[0] == '.') continue;
        char code[I18N_CODE_MAX + 1];
        snprintf(code, sizeof(code), "%.*s", (int)(l - 5), e->d_name);
        for (char *c = code; *c; c++) *c = (char)tolower((unsigned char)*c);
        bool builtin, dup = false;
        for (int i = 0; i < n; i++) dup |= strcmp(out[i].code, code) == 0;
        if (dup) continue;
        char *json = source_for(code, &builtin);
        if (!json) continue;
        char fcode[I18N_CODE_MAX + 1];
        if (i18n_parse_header(json, fcode, out[n].name) && strcmp(fcode, code) == 0) {
            snprintf(out[n].code, sizeof(out[n].code), "%s", code);
            out[n].builtin = false;
            n++;
        }
        free(json);
    }
    closedir(d);
    return n;
}

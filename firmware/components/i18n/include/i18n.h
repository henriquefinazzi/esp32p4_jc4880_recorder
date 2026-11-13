// Textos da interface em vários idiomas.
// Cada idioma é um arquivo JSON plano de chave -> texto, com "_codigo" e "_nome":
//   { "_codigo": "en", "_nome": "English", "home.ready": "Ready to record", ... }
// Português (pt) e inglês (en) vêm embutidos no firmware (firmware/lang/*.json).
// Outros idiomas: um arquivo <codigo>.json na pasta IDIOMAS do cartão.
// Texto que faltar no idioma escolhido sai em português; se faltar também, sai a chave.
#pragma once

#include <stdbool.h>

#define I18N_DIR "/sdcard/IDIOMAS"
#define I18N_CODE_MAX 15
#define I18N_NAME_MAX 31
#define I18N_MAX_LANGS 12

typedef struct {
    char code[I18N_CODE_MAX + 1];
    char name[I18N_NAME_MAX + 1];
    bool builtin;
} i18n_lang_t;

// Carrega o português (base) e o idioma `code` (cai no português se não achar).
void i18n_init(const char *code);

// Troca de idioma. false se não achar ou o arquivo tiver erro (continua no atual).
// Chamar só da tarefa da tela. T() pode ser chamado de qualquer tarefa.
bool i18n_set(const char *code);

// Código do idioma em uso.
const char *i18n_current(void);

// Texto da chave no idioma atual.
const char *T(const char *key);

// Textos do idioma atual cujas chaves começam com um dos prefixos, como objeto JSON
// (para a página web). malloc; liberar com free. As chaves vêm do português (base).
char *i18n_export(const char *const *prefixes, int n);

// Idiomas disponíveis: embutidos e os do cartão. Devolve quantos preencheu.
int i18n_list(i18n_lang_t *out, int max);

// Lê o "_codigo" e o "_nome" de um texto JSON de idioma (para testes e listagem).
bool i18n_parse_header(const char *json, char code[I18N_CODE_MAX + 1], char name[I18N_NAME_MAX + 1]);

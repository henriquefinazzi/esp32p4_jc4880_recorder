// UI texts in several languages.
// Each language is a flat JSON file of key -> text, with "_codigo" (code) and "_nome" (name):
//   { "_codigo": "en", "_nome": "English", "home.ready": "Ready to record", ... }
// Portuguese (pt) and English (en) are embedded in the firmware (firmware/lang/*.json).
// Other languages: a <code>.json file in the card's LANGUAGES folder.
// Text missing in the chosen language falls back to English; if missing there too, the key.
#pragma once

#include <stdbool.h>

#define I18N_DIR "/sdcard/LANGUAGES"
#define I18N_CODE_MAX 15
#define I18N_NAME_MAX 31
#define I18N_MAX_LANGS 12

typedef struct {
    char code[I18N_CODE_MAX + 1];
    char name[I18N_NAME_MAX + 1];
    bool builtin;
} i18n_lang_t;

// Loads English (base) and language `code` (falls back to English if not found).
void i18n_init(const char *code);

// Switches language. false if not found or the file has an error (keeps the current one).
// Call only from the screen task. T() may be called from any task.
bool i18n_set(const char *code);

// Code of the language in use.
const char *i18n_current(void);

// Text for the key in the current language.
const char *T(const char *key);

// Texts of the current language whose keys start with one of the prefixes, as a JSON object
// (for the web page). malloc'd; release with free. Keys come from English (base).
char *i18n_export(const char *const *prefixes, int n);

// Available languages: embedded and those on the card. Returns how many were filled.
int i18n_list(i18n_lang_t *out, int max);

// Reads "_codigo" and "_nome" from a language JSON text (for tests and listing).
bool i18n_parse_header(const char *json, char code[I18N_CODE_MAX + 1], char name[I18N_NAME_MAX + 1]);

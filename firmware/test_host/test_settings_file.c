// Testes do settings_file: ida e volta, valores inválidos, arquivo quebrado, lista de redes.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings_file.h"

static int failures;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static settings_t base(void)
{
    settings_t s;
    memset(&s, 0, sizeof(s));
    s.brightness = 80;
    s.screen_off_s = 10;
    s.gain_db = 30;
    s.web_on = true;
    s.lock_on = true;
    strcpy(s.lock_pin, "2468");
    s.lock_after_s = 300;
    s.dev_on = true;
    strcpy(s.host, "p4recorder");
    strcpy(s.user, "p4recorder");
    strcpy(s.pass, "123456");
    s.wifi_count = 2;
    strcpy(s.wifi[0].ssid, "Casa \"2G\"");
    strcpy(s.wifi[0].pass, "senha com espaço");
    strcpy(s.wifi[1].ssid, "Aberta");
    strcpy(s.wifi[1].pass, "");
    strcpy(s.ntp, "a.st1.ntp.br");
    strcpy(s.lang, "pt");
    s.ap_on = true;
    strcpy(s.ap_ssid, "Gravador-1A2B");
    strcpy(s.ap_pass, "12345678");
    return s;
}

static void round_trip(void)
{
    settings_t a = base(), b;
    int ignored = -1;
    char *text = settings_to_json(&a);
    CHECK(text != NULL);
    settings_t other = base();
    other.brightness = 50;
    other.wifi_count = 0;
    CHECK(settings_from_json(text, &other, &b, &ignored));
    CHECK(ignored == 0);
    CHECK(settings_equal(&a, &b));
    free(text);
}

static void broken_file(void)
{
    settings_t a = base(), b;
    int ignored = 0;
    CHECK(!settings_from_json("{ \"aparelho\": { \"brilho\": 50, } ", &a, &b, &ignored));
    CHECK(settings_equal(&a, &b));
    CHECK(!settings_from_json("[1,2]", &a, &b, &ignored));
    CHECK(!settings_from_json("", &a, &b, &ignored));
}

static void partial_and_invalid(void)
{
    settings_t a = base(), b;
    int ignored = 0;
    const char *t = "{\"aparelho\":{\"brilho\":5,\"apagar_tela_s\":30,\"ganho_db\":31},"
                    "\"navegador\":{\"ligado\":\"sim\",\"nome\":\"Meu-Gravador\",\"usuario\":\"ana\",\"senha\":\"abc\"}}";
    CHECK(settings_from_json(t, &a, &b, &ignored));
    CHECK(ignored == 5);           // brilho, ganho, ligado, nome, senha
    CHECK(b.brightness == 80);     // fora da faixa: mantém
    CHECK(b.screen_off_s == 30);   // válido: aplica
    CHECK(b.gain_db == 30);        // não é múltiplo de 6
    CHECK(b.web_on == true);
    CHECK(strcmp(b.host, "p4recorder") == 0);
    CHECK(strcmp(b.user, "ana") == 0);
    CHECK(strcmp(b.pass, "123456") == 0);
    CHECK(b.wifi_count == 2);      // sem "wifi" no arquivo: mantém a lista
}

static void wifi_list(void)
{
    settings_t a = base(), b;
    int ignored = 0;
    const char *t = "{\"wifi\":[{\"rede\":\"Nova\",\"senha\":\"12345678\"},"
                    "{\"rede\":\"Curta\",\"senha\":\"123\"},"
                    "{\"rede\":\"Nova\",\"senha\":\"87654321\"},"
                    "{\"rede\":\"SemSenha\"},"
                    "{\"senha\":\"12345678\"}]}";
    CHECK(settings_from_json(t, &a, &b, &ignored));
    CHECK(ignored == 3); // senha curta, repetida, sem nome
    CHECK(b.wifi_count == 2);
    CHECK(strcmp(b.wifi[0].ssid, "Nova") == 0 && strcmp(b.wifi[0].pass, "12345678") == 0);
    CHECK(strcmp(b.wifi[1].ssid, "SemSenha") == 0 && b.wifi[1].pass[0] == 0);

    CHECK(settings_from_json("{\"wifi\":[]}", &a, &b, &ignored));
    CHECK(ignored == 0 && b.wifi_count == 0); // lista vazia esquece todas

    CHECK(settings_from_json("{\"wifi\":[{\"rede\":\"a\"},{\"rede\":\"b\"},{\"rede\":\"c\"},{\"rede\":\"d\"},{\"rede\":\"e\"}]}", &a, &b, &ignored));
    CHECK(b.wifi_count == 4 && ignored == 1);
}

static void clock_and_ap(void)
{
    settings_t a = base(), b;
    int ignored = 0;
    const char *t = "{\"relogio\":{\"ntp\":\"pool.ntp.org\"},\"rede_propria\":{\"ligada\":false,\"nome\":\"Sala 3\",\"senha\":\"curta\"}}";
    CHECK(settings_from_json(t, &a, &b, &ignored));
    CHECK(ignored == 1);                          // senha curta
    CHECK(strcmp(b.ntp, "pool.ntp.org") == 0);
    CHECK(b.ap_on == false);
    CHECK(strcmp(b.ap_ssid, "Sala 3") == 0);
    CHECK(strcmp(b.ap_pass, "12345678") == 0);
    CHECK(settings_from_json("{\"relogio\":{\"ntp\":\"http://x\"}}", &a, &b, &ignored));
    CHECK(ignored == 1 && strcmp(b.ntp, "a.st1.ntp.br") == 0);
    settings_t c = a;
    c.ap_on = false;
    CHECK(!settings_equal(&a, &c));
    CHECK(settings_from_json("{\"navegador\":{\"desenvolvedor\":false}}", &a, &b, &ignored));
    CHECK(ignored == 0 && b.dev_on == false && b.web_on == true);
    CHECK(settings_from_json("{\"aparelho\":{\"tema\":\"claro\"}}", &a, &b, &ignored));
    CHECK(ignored == 0 && b.theme == 1);
    CHECK(settings_from_json("{\"aparelho\":{\"tema\":\"azul\"}}", &a, &b, &ignored));
    CHECK(ignored == 1 && b.theme == 0);
    CHECK(settings_from_json("{\"aparelho\":{\"idioma\":\"en\"}}", &a, &b, &ignored));
    CHECK(ignored == 0 && strcmp(b.lang, "en") == 0);
    CHECK(settings_from_json("{\"aparelho\":{\"idioma\":\"EN!\"}}", &a, &b, &ignored));
    CHECK(ignored == 1 && strcmp(b.lang, "pt") == 0);
    CHECK(settings_from_json("{\"aparelho\":{\"formato\":\"opus\",\"filtro\":false,\"apagar_antigas\":true}}", &a, &b, &ignored));
    CHECK(ignored == 0 && b.rec_format == 1 && !b.filter && b.autodelete);
    CHECK(settings_from_json("{\"aparelho\":{\"formato\":\"flac\"}}", &a, &b, &ignored));
    CHECK(ignored == 1 && b.rec_format == a.rec_format);
    CHECK(settings_from_json("{\"bloqueio\":{\"ligado\":true,\"codigo\":\"12a4\",\"apos_s\":61}}", &a, &b, &ignored));
    CHECK(ignored == 2 && strcmp(b.lock_pin, "2468") == 0 && b.lock_after_s == 300);
    settings_t nopin = a;
    nopin.lock_pin[0] = 0;
    CHECK(settings_from_json("{\"bloqueio\":{\"ligado\":true}}", &nopin, &b, &ignored));
    CHECK(!b.lock_on); // sem código válido, não liga
    CHECK(settings_lock_pin_valid("0000") && !settings_lock_pin_valid("123") && !settings_lock_pin_valid("123456789"));
}

static void rules(void)
{
    CHECK(settings_ntp_valid("192.168.0.1"));
    CHECK(settings_ntp_valid("a.st1.ntp.br"));
    CHECK(!settings_ntp_valid("a..b"));
    CHECK(!settings_ntp_valid(".pool"));
    CHECK(!settings_ntp_valid("pool ntp"));
    CHECK(settings_ap_ssid_valid("Gravador da Sala"));
    CHECK(!settings_ap_ssid_valid(""));
    CHECK(settings_ap_pass_valid("abc def!"));
    CHECK(!settings_ap_pass_valid("1234567"));
    CHECK(settings_host_valid("gravador-2"));
    CHECK(!settings_host_valid("-x"));
    CHECK(!settings_host_valid("Grav"));
    CHECK(settings_user_valid("joao.silva"));
    CHECK(!settings_user_valid("a:b"));
    CHECK(!settings_user_valid("com espaco"));
    CHECK(settings_pass_valid("a:b:c!"));
    CHECK(!settings_pass_valid("abc"));
    CHECK(!settings_pass_valid("çççç"));
    char hex[65];
    memset(hex, 'a', 64);
    hex[64] = 0;
    CHECK(settings_wifi_valid("x", hex));
    hex[3] = 'z';
    CHECK(!settings_wifi_valid("x", hex));
    CHECK(!settings_wifi_valid("", "12345678"));
}

int main(void)
{
    round_trip();
    broken_file();
    partial_and_invalid();
    wifi_list();
    clock_and_ap();
    rules();
    if (failures) {
        printf("%d falha(s)\n", failures);
        return 1;
    }
    printf("settings_file: ok\n");
    return 0;
}

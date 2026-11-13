// Recorder web server: lists, plays, downloads and deletes recordings over the local network.
// Everything requires the username and password shown on the device (HTTP Basic).
// It's plain HTTP: the password keeps out the curious on the same network, not traffic sniffers.
#include "web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_netif_ip_addr.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mdns.h"

#include "app.h"
#include "logbuf.h"
#include "storage.h"
#include "i18n.h"
#include "rec_writer.h"
#include "net.h"
#include "ui.h"
#include "display.h"

#define MAX_RECORDINGS 60
#define SEND_BUFFER_BYTES (16 * 1024)

static const char *TAG = "web";

static httpd_handle_t s_server;
// Expected Authorization header value. Two buffers: the screen changes the password while the
// server may be checking a request, so only the index is swapped.
static char s_auth[2][128];
static volatile int s_auth_idx;

static const char PAGE[] =
    "<!doctype html><html lang=en><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>P4 Recorder</title><style>"
    ":root{color-scheme:dark}*{box-sizing:border-box}"
    "body{margin:0;background:#0f1317;color:#e8ecef;font:16px system-ui,sans-serif}"
    "main{max-width:760px;margin:0 auto;padding:20px 16px 120px}"
    "h1{font-size:26px;margin:0 0 4px}#st{color:#8a949e;margin:0 0 18px}"
    ".r{background:#1a2027;border-radius:14px;padding:14px 16px;margin:0 0 10px;display:flex;gap:12px;align-items:center;flex-wrap:wrap}"
    ".r.on{outline:2px solid #2fa24a}.t{flex:1;min-width:160px}.t b{display:block;font-size:17px}.t span{color:#8a949e;font-size:14px}"
    "button,a.b{font:inherit;border:0;border-radius:10px;padding:9px 14px;background:#242c35;color:#e8ecef;cursor:pointer;text-decoration:none}"
    "button.p{background:#2fa24a;color:#fff}button.d{background:none;border:1.5px solid #d63a3a;color:#d63a3a}"
    "#pl{position:fixed;left:0;right:0;bottom:0;background:#1a2027;padding:12px 16px;display:none}"
    "#pl div{max-width:760px;margin:0 auto}#pl b{display:block;margin:0 0 6px}audio{width:100%}"
    ".w{background:#3a2a12;color:#e0a33a;border-radius:14px;padding:14px 16px;margin:0 0 14px}"
    ".r input{width:22px;height:22px;accent-color:#2fa24a}#tb{display:none;margin:0 0 12px}"
    "#fw{margin:28px 0 0;color:#8a949e;font-size:14px}#fw b{color:#e8ecef}#fw input{display:none}#fm{margin:8px 0 0}"
    "</style><main><h1 id=h1></h1><p id=st></p><div id=w></div><div id=tb><button class=d id=dm></button></div><div id=l></div>"
    "<div id=fw><b>Firmware</b> <span id=fv></span><div><label class=b style='display:inline-block;margin-top:8px;background:#242c35;border-radius:10px;padding:9px 14px;color:#e8ecef;cursor:pointer'><span id=fu></span><input type=file id=ff accept='.bin'></label></div><div id=fm></div></div></main>"
    "<div id=pl><div><b id=pn></b><audio id=au controls></audio></div></div><script>"
    "const $=i=>document.getElementById(i);let L={},cur='',sig='';"
    "const t=(k,...a)=>{let i=0;return (L[k]||k).replace(/%[sd]/g,()=>a[i++])};"
    "const p2=n=>String(n).padStart(2,'0');"
    "const dec=x=>x.toFixed(1).replace('.',t('web.decimal'));"
    "const dur=s=>s>=3600?Math.floor(s/3600)+':'+p2(Math.floor(s/60)%60)+':'+p2(s%60):p2(Math.floor(s/60))+':'+p2(s%60);"
    "const mb=b=>b<1048576?Math.round(b/1024)+' KB':dec(b/1048576)+' MB';"
    "const gb=m=>dec(m/1024)+' GB';"
    "function dat(r){if(!r.mtime)return '';const d=new Date(r.mtime*1000);"
    "return d.getDate()+' '+t('month.'+(d.getMonth()+1))+' '+d.getFullYear()+', '+p2(d.getHours())+':'+p2(d.getMinutes())}"
    "const tit=r=>r.title||dat(r)||t('web.recording_n',r.id);"
    "function warn(x){const w=$('w');w.innerHTML='';if(!x)return;const d=document.createElement('div');d.className='w';d.textContent=x;w.appendChild(d)}"
    "function play(n,x){cur=n;$('pl').style.display='block';$('pn').textContent=x;$('au').src='/rec/'+n;$('au').play();mark()}"
    "function mark(){document.querySelectorAll('.r').forEach(e=>e.classList.toggle('on',e.dataset.n==cur))}"
    "async function ren(r){const x=prompt(t('web.rename_prompt'),r.title);if(x===null)return;"
    "const q=await fetch('/rec/'+r.id,{method:'PUT',body:x});if(!q.ok){alert(t('web.rename_fail',await q.text()));return}sig='';load()}"
    "const sel=new Set();"
    "function tb(){$('tb').style.display=sel.size?'block':'none';$('dm').textContent=t('web.delete_selected',sel.size)}"
    "async function delMany(){if(!confirm(t('web.delete_many_confirm',sel.size)))return;let bad=0;"
    "for(const n of [...sel]){const q=await fetch('/rec/'+n,{method:'DELETE'});if(q.ok){sel.delete(n);if(cur==n){$('au').pause();$('pl').style.display='none';cur=''}}else bad++}"
    "if(bad)alert(t('web.delete_many_fail',bad));sel.clear();tb();sig='';load()}"
    "async function del(n,x){if(!confirm(t('web.delete_confirm',x)))return;"
    "const q=await fetch('/rec/'+n,{method:'DELETE'});if(!q.ok){alert(t('web.delete_fail',await q.text()));return}"
    "if(cur==n){$('au').pause();$('pl').style.display='none';cur=''}sig='';load()}"
    "async function lang(){try{const n=await(await fetch('/api/lang',{cache:'no-store'})).json();if(n._codigo!==L._codigo){L=n;sig='';"
    "document.documentElement.lang=L._codigo;document.title=t('web.page_title');$('h1').textContent=t('web.page_title');$('fu').textContent=t('web.update_fw')}}catch(e){}}"
    "async function load(){let s,R;await lang();try{s=await(await fetch('/api/status')).json();R=await(await fetch('/api/recordings')).json()}"
    "catch(e){$('st').textContent=t('web.no_answer');return}"
    "$('st').textContent=(s.card_ok?t('web.space',gb(s.free_mb),gb(s.total_mb))+' · ':'')+t('web.version',s.version);"
    "$('fv').textContent=t('web.fw_version',s.version,s.build);$('fw').style.display=s.dev?'':'none';"
    "if(!s.time_ok)fetch('/api/time',{method:'POST',body:String(Math.floor(Date.now()/1000))});"
    "warn(s.card_on_host?t('web.card_host'):s.card_unreadable?t('web.card_unreadable'):!s.card_ok?t('web.no_card'):s.recording?t('web.recording_now',s.rec_name,dur(s.rec_seconds)):'');"
    "const k=JSON.stringify(R);if(k===sig)return;sig=k;const l=$('l');l.innerHTML='';"
    "for(const n of [...sel])if(!R.some(r=>r.id==n))sel.delete(n);$('dm').onclick=delMany;tb();"
    "if(!R.length&&s.card_ok){const p=document.createElement('p');p.style.color='#8a949e';p.textContent=t('web.empty');l.appendChild(p)}"
    "for(const r of R){const x=tit(r),e=document.createElement('div');e.className='r';e.dataset.n=r.id;"
    "e.innerHTML='<input type=checkbox><div class=t><b></b><span></span></div><button class=p></button><a class=b></a><button class=n></button><button class=d></button>';"
    "e.querySelector('b').textContent=x;"
    "e.querySelector('span').textContent=(r.title&&dat(r)?dat(r)+' · ':'')+dur(r.seconds)+' · '+mb(r.bytes)+' · REC_'+String(r.id).padStart(4,'0');"
    "const P=e.querySelector('.p'),a=e.querySelector('a'),N=e.querySelector('.n'),D=e.querySelector('.d');"
    "P.textContent=t('web.play');a.textContent=t('web.download');N.textContent=t('web.rename');D.textContent=t('web.delete');"
    "const c=e.querySelector('input');c.checked=sel.has(r.id);c.onchange=()=>{c.checked?sel.add(r.id):sel.delete(r.id);tb()};"
    "P.onclick=()=>play(r.id,x);a.href='/rec/'+r.id+'?dl=1';a.download=r.file;N.onclick=()=>ren(r);D.onclick=()=>del(r.id,x);l.appendChild(e)}mark()}"
    "$('ff').onchange=()=>{const f=$('ff').files[0];$('ff').value='';if(!f)return;"
    "if(!confirm(t('web.fw_confirm',f.name,mb(f.size))))return;"
    "const m=$('fm'),x=new XMLHttpRequest();x.open('POST','/api/firmware');"
    "x.upload.onprogress=e=>{if(e.lengthComputable)m.textContent=t('web.sending',Math.round(e.loaded*100/e.total)+'%')};"
    "x.onload=()=>{if(x.status!=200){m.textContent=t('web.fw_fail',x.responseText);return}"
    "m.textContent=t('web.fw_done');setTimeout(()=>{const q=setInterval(async()=>{try{"
    "const s=await(await fetch('/api/status',{cache:'no-store'})).json();clearInterval(q);"
    "m.textContent=t('web.fw_ready',s.version,s.build);load()}catch(e){}},3000)},4000)};"
    "x.onerror=()=>{m.textContent=t('web.conn_lost')};m.textContent=t('web.sending','0%');x.send(f)};"
    "load();setInterval(load,5000);</script></html>";

// ---------- Helpers ----------

// Lockout after wrong passwords, per client address: LOGIN_MAX_FAILS consecutive failures
// lock that address for LOGIN_LOCK_S. A request without a password (the browser's first) doesn't count.
#define LOGIN_MAX_FAILS 5
#define LOGIN_LOCK_S 300
#define LOGIN_SLOTS 8

typedef struct {
    uint32_t ip;
    uint8_t fails;
    int64_t locked_until_us;
    int64_t last_us;
} login_slot_t;

static login_slot_t s_login[LOGIN_SLOTS];
static portMUX_TYPE s_login_mux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t peer_ip(httpd_req_t *req)
{
    struct sockaddr_in6 addr;
    socklen_t len = sizeof(addr);
    if (getpeername(httpd_req_to_sockfd(req), (struct sockaddr *)&addr, &len) != 0) return 0;
    if (addr.sin6_family == AF_INET) return ((struct sockaddr_in *)&addr)->sin_addr.s_addr;
    return addr.sin6_addr.un.u32_addr[3]; // mapped IPv4 or tail of the IPv6
}

// Slot for the address (or the least used one). Call with s_login_mux held.
static login_slot_t *login_slot(uint32_t ip)
{
    login_slot_t *oldest = &s_login[0];
    for (int i = 0; i < LOGIN_SLOTS; i++) {
        if (s_login[i].ip == ip) return &s_login[i];
        if (s_login[i].last_us < oldest->last_us) oldest = &s_login[i];
    }
    memset(oldest, 0, sizeof(*oldest));
    oldest->ip = ip;
    return oldest;
}

static bool authorized(httpd_req_t *req)
{
    char hdr[128];
    uint32_t ip = peer_ip(req);
    int64_t now = esp_timer_get_time();
    int wait_s = 0;
    portENTER_CRITICAL(&s_login_mux);
    login_slot_t *slot = login_slot(ip);
    slot->last_us = now;
    if (slot->locked_until_us > now) wait_s = (int)((slot->locked_until_us - now) / 1000000) + 1;
    portEXIT_CRITICAL(&s_login_mux);
    if (wait_s) {
        char msg[160];
        snprintf(msg, sizeof(msg), T("web.err.locked"), (wait_s + 59) / 60);
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_sendstr(req, msg);
        return false;
    }
    bool has = httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) == ESP_OK;
    bool ok = has && strcmp(hdr, s_auth[s_auth_idx]) == 0;
    if (has) {
        portENTER_CRITICAL(&s_login_mux);
        slot = login_slot(ip);
        if (ok) {
            slot->fails = 0;
        } else if (++slot->fails >= LOGIN_MAX_FAILS) {
            slot->fails = 0;
            slot->locked_until_us = now + (int64_t)LOGIN_LOCK_S * 1000000;
        }
        portEXIT_CRITICAL(&s_login_mux);
        if (!ok) ESP_LOGW(TAG, "wrong password from " IPSTR, IP2STR((esp_ip4_addr_t *)&ip));
    }
    if (ok) return true;
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"P4 Recorder\"");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, T("web.err.login"));
    return false;
}

// Routes identify the recording by number: "/rec/23". Nothing from the request becomes a file
// path, so there is no way to escape the recordings folder.
static bool id_from_uri(const char *uri, int *id)
{
    const char *p = uri + strlen("/rec/");
    size_t len = strcspn(p, "?");
    if (len == 0 || len > 4) return false;
    int n = 0;
    for (size_t i = 0; i < len; i++) {
        if (p[i] < '0' || p[i] > '9') return false;
        n = n * 10 + (p[i] - '0');
    }
    *id = n;
    return true;
}

// File name for the download header, with accented characters (RFC 5987).
static void percent_encode(const char *in, char *out, size_t out_len)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in && o + 4 < out_len; in++) {
        unsigned char c = (unsigned char)*in;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = 0;
}

static esp_err_t fail(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, message);
}

static const char *card_problem(const app_status_t *st)
{
    return st->card_on_host ? T("web.err.card_host") : st->card_unreadable ? T("web.err.card_unreadable") : T("web.err.no_card");
}

// ---------- Routes ----------

static esp_err_t page_handler(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PAGE, sizeof(PAGE) - 1);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    app_status_t st;
    app_get_status(&st);
    const esp_app_desc_t *desc = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    char json[400];
    snprintf(json, sizeof(json),
             "{\"recording\":%s,\"rec_name\":\"%s\",\"rec_seconds\":%lu,\"card_ok\":%s,\"card_on_host\":%s,"
             "\"free_mb\":%lu,\"total_mb\":%lu,\"version\":\"%s\",\"build\":\"%s %s\","
             "\"card_unreadable\":%s,\"uptime_s\":%lu,\"reset_reason\":%d,\"partition\":\"%s\",\"dev\":%s,\"time_ok\":%s}",
             st.recording ? "true" : "false", st.rec_name, (unsigned long)st.rec_seconds, st.card_ok ? "true" : "false",
             st.card_on_host ? "true" : "false", (unsigned long)st.free_mb, (unsigned long)st.total_mb, APP_VERSION, desc->date,
             desc->time, st.card_unreadable ? "true" : "false", (unsigned long)(esp_timer_get_time() / 1000000), (int)esp_reset_reason(), running ? running->label : "?", app_dev_enabled() ? "true" : "false", net_last_time_sync() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

// ---------- Firmware update ----------
// POST /api/firmware with the .bin in the body (the same firmware.bin used for USB flashing).
// Writes to the other partition, verifies the image and reboots into it. If the new firmware
// doesn't confirm it booted (end of app_main), the next reboot rolls back to the previous one.

#define OTA_CHUNK 4096
#define OTA_HEADER_BYTES (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500)); // let the response reach the browser
    esp_restart();
}

// Checks the start of the file: a valid app image from the same project that is running.
static const char *check_header(const uint8_t *buf)
{
    const esp_image_header_t *img = (const esp_image_header_t *)buf;
    if (img->magic != ESP_IMAGE_HEADER_MAGIC) return T("web.err.fw_not_bin");
    if (img->chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID) return T("web.err.fw_chip");
    const esp_app_desc_t *desc = (const esp_app_desc_t *)(buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    if (desc->magic_word != ESP_APP_DESC_MAGIC_WORD) return T("web.err.fw_not_bin");
    if (strncmp(desc->project_name, esp_app_get_description()->project_name, sizeof(desc->project_name)) != 0) {
        return T("web.err.fw_project");
    }
    return NULL;
}

// Developer features turned off on the device: the endpoint does nothing.
static bool dev_allowed(httpd_req_t *req)
{
    if (app_dev_enabled()) return true;
    fail(req, "403 Forbidden", T("web.err.dev_off"));
    return false;
}

static esp_err_t firmware_handler(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    if (!dev_allowed(req)) return ESP_OK;
    app_status_t st;
    app_get_status(&st);
    if (st.recording) return fail(req, "409 Conflict", T("web.err.fw_recording"));
    if (st.updating) return fail(req, "409 Conflict", T("web.err.fw_busy"));

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) return fail(req, "500 Internal Server Error", T("web.err.fw_no_partition"));
    if (req->content_len < OTA_HEADER_BYTES || req->content_len > part->size) {
        return fail(req, "400 Bad Request", T("web.err.fw_size"));
    }

    uint8_t *buf = heap_caps_malloc(OTA_CHUNK, MALLOC_CAP_SPIRAM);
    if (!buf) return fail(req, "500 Internal Server Error", T("web.err.no_memory"));
    app_set_updating(true);
    ESP_LOGI(TAG, "firmware: receiving %u bytes for %s", (unsigned)req->content_len, part->label);

    esp_ota_handle_t ota = 0;
    bool begun = false;
    const char *problem = NULL;
    size_t got = 0, filled = 0;
    int timeouts = 0;
    while (got < req->content_len && !problem) {
        size_t want = req->content_len - got;
        if (want > OTA_CHUNK - filled) want = OTA_CHUNK - filled;
        int n = httpd_req_recv(req, (char *)buf + filled, want);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
        if (n <= 0) {
            problem = T("web.conn_lost");
            break;
        }
        timeouts = 0;
        got += n;
        filled += n;
        if (!begun) {
            if (filled < OTA_HEADER_BYTES && got < req->content_len) continue; // gather the header
            problem = check_header(buf);
            if (problem) break;
            // Erase the whole image area now, with the backlight off: on this board each flash
            // erase disturbs the display (it flashes blue), so it is done once instead of per chunk.
            display_backlight(false);
            esp_err_t begin_err = esp_ota_begin(part, req->content_len, &ota);
            display_backlight(true);
            if (begin_err != ESP_OK) {
                problem = T("web.err.fw_prepare");
                break;
            }
            begun = true;
        }
        if (filled == OTA_CHUNK || got == req->content_len) {
            if (esp_ota_write(ota, buf, filled) != ESP_OK) problem = T("web.err.fw_write");
            filled = 0;
        }
    }
    free(buf);

    if (!problem && esp_ota_end(ota) != ESP_OK) problem = T("web.err.fw_corrupt");
    else if (problem && begun) esp_ota_abort(ota);
    begun = false;
    if (!problem && esp_ota_set_boot_partition(part) != ESP_OK) problem = T("web.err.fw_activate");

    if (problem) {
        app_set_updating(false);
        ESP_LOGW(TAG, "firmware: %s", problem);
        return fail(req, "400 Bad Request", problem);
    }
    ESP_LOGI(TAG, "firmware: written to %s, restarting", part->label);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

// POST /api/time with seconds since 1970 (browser time): sets the clock when there is
// no internet time, as on the own network. This way recordings get the right date.
static esp_err_t time_handler(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    char body[24] = "";
    int n = req->content_len < sizeof(body) ? httpd_req_recv(req, body, req->content_len) : -1;
    if (n <= 0) return fail(req, "400 Bad Request", "?");
    body[n] = 0;
    bool set = net_set_time_from(atoll(body));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, set ? "{\"set\":true}" : "{\"set\":false}");
}

// Page texts in the device language.
static esp_err_t lang_handler(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    static const char *const PREFIXES[] = {"web.", "month."};
    char *json = i18n_export(PREFIXES, 2);
    if (!json) return fail(req, "500 Internal Server Error", T("web.err.no_memory"));
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

// Screen image (BMP). ?tela=<name> opens a screen first (see ui_capture_bmp).
static esp_err_t screen_handler(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    if (!dev_allowed(req)) return ESP_OK;
    char query[64] = "", name[32] = "";
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) httpd_query_key_value(query, "tela", name, sizeof(name));
    uint8_t *bmp;
    size_t len;
    if (!ui_capture_bmp(name, &bmp, &len)) return fail(req, "500 Internal Server Error", T("web.err.screen"));
    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = ESP_OK;
    for (size_t off = 0; off < len && err == ESP_OK; off += SEND_BUFFER_BYTES) {
        size_t n = len - off < SEND_BUFFER_BYTES ? len - off : SEND_BUFFER_BYTES;
        err = httpd_resp_send_chunk(req, (const char *)bmp + off, n);
    }
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, NULL, 0);
    free(bmp);
    return err;
}

// Recent device log (text), for diagnostics without the console USB.
static esp_err_t log_handler(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    if (!dev_allowed(req)) return ESP_OK;
    const size_t len = 16 * 1024;
    char *buf = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!buf) return fail(req, "500 Internal Server Error", T("web.err.no_memory"));
    size_t n = logbuf_copy(buf, len);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    esp_err_t err = httpd_resp_send(req, buf, n);
    free(buf);
    return err;
}

static esp_err_t list_handler(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    static app_recording_t recs[MAX_RECORDINGS]; // the server handles one request at a time
    app_status_t st;
    app_get_status(&st);
    int n = app_list_recordings(recs, MAX_RECORDINGS);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "[");
    bool first = true;
    for (int i = 0; i < n; i++) {
        // The recording in progress is still being written: leave it out of the list.
        if (st.recording && strncmp(recs[i].file, st.rec_name, 8) == 0) continue;
        // Name and file never contain quotes or backslashes (the name is cleaned when saved).
        char item[320];
        snprintf(item, sizeof(item), "%s{\"id\":%d,\"file\":\"%s\",\"title\":\"%s\",\"bytes\":%lu,\"seconds\":%lu,\"mtime\":%lld}",
                 first ? "" : ",", recs[i].id, recs[i].file, recs[i].title, (unsigned long)recs[i].bytes,
                 (unsigned long)recs[i].seconds, (long long)recs[i].mtime);
        httpd_resp_sendstr_chunk(req, item);
        first = false;
    }
    httpd_resp_sendstr_chunk(req, "]");
    return httpd_resp_sendstr_chunk(req, NULL);
}

// Sends the file with Content-Length and Range support, which the browser player
// uses to learn the duration and to seek to a point in the recording.
static esp_err_t file_handler(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    int id;
    if (!id_from_uri(req->uri, &id)) return fail(req, "404 Not Found", T("web.err.not_found"));

    app_status_t st;
    app_get_status(&st);
    if (!st.card_ok) return fail(req, "503 Service Unavailable", card_problem(&st));
    if (st.recording && id == atoi(st.rec_name + 4)) return fail(req, "409 Conflict", T("web.err.in_progress"));

    char path[128];
    if (!app_recording_path(id, path, sizeof(path))) return fail(req, "404 Not Found", T("web.err.not_found"));
    const char *name = path + sizeof(STORAGE_MOUNT_POINT); // after "/sdcard/"
    struct stat info;
    if (stat(path, &info) != 0) return fail(req, "404 Not Found", T("web.err.not_found"));
    long total = (long)info.st_size;

    long start = 0, end = total - 1;
    bool partial = false;
    char range[48];
    if (httpd_req_get_hdr_value_str(req, "Range", range, sizeof(range)) == ESP_OK && strncmp(range, "bytes=", 6) == 0) {
        const char *spec = range + 6;
        char *dash = strchr(spec, '-');
        if (dash) {
            if (dash == spec) { // "bytes=-N": the last N bytes
                long suffix = atol(dash + 1);
                if (suffix > 0) start = total > suffix ? total - suffix : 0;
            } else {
                start = atol(spec);
                if (dash[1]) end = atol(dash + 1);
            }
            if (end >= total) end = total - 1;
            if (start < 0 || start > end) {
                char cr[48];
                snprintf(cr, sizeof(cr), "bytes */%ld", total);
                httpd_resp_set_hdr(req, "Content-Range", cr);
                return fail(req, "416 Range Not Satisfiable", T("web.err.range"));
            }
            partial = true;
        }
    }

    FILE *f = fopen(path, "rb");
    if (!f) return fail(req, "404 Not Found", T("web.err.not_found"));
    uint8_t *buf = malloc(SEND_BUFFER_BYTES);
    if (!buf) {
        fclose(f);
        return fail(req, "500 Internal Server Error", T("web.err.no_memory"));
    }

    char query[16], dl[4] = "";
    bool download = httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
                    httpd_query_key_value(query, "dl", dl, sizeof(dl)) == ESP_OK && dl[0] == '1';

    long length = end - start + 1;
    int fmt = rec_format_from_name(name);
    const char *mime = fmt == REC_FMT_OPUS ? "audio/ogg; codecs=opus" : fmt == REC_FMT_MP3 ? "audio/mpeg" : "audio/wav";
    char head[640], encoded[300];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %ld\r\nAccept-Ranges: bytes\r\n"
                     "Cache-Control: no-store\r\n",
                     partial ? "206 Partial Content" : "200 OK", mime, length);
    if (partial) n += snprintf(head + n, sizeof(head) - n, "Content-Range: bytes %ld-%ld/%ld\r\n", start, end, total);
    if (download) {
        percent_encode(name, encoded, sizeof(encoded));
        n += snprintf(head + n, sizeof(head) - n, "Content-Disposition: attachment; filename=\"REC_%04d%s\"; filename*=UTF-8''%s\r\n", id,
                      rec_format_ext((rec_format_t)(fmt < 0 ? 0 : fmt)), encoded);
    }
    n += snprintf(head + n, sizeof(head) - n, "\r\n");

    esp_err_t result = ESP_OK;
    if (httpd_send(req, head, n) < 0) {
        result = ESP_FAIL;
    } else {
        fseek(f, start, SEEK_SET);
        long left = length;
        while (left > 0) {
            size_t want = left > SEND_BUFFER_BYTES ? SEND_BUFFER_BYTES : (size_t)left;
            size_t got = fread(buf, 1, want, f);
            if (got == 0) break;
            size_t sent = 0;
            while (sent < got) {
                int w = httpd_send(req, (const char *)buf + sent, got - sent);
                if (w <= 0) {
                    result = ESP_FAIL; // the browser closed the connection (normal when seeking in the audio)
                    break;
                }
                sent += (size_t)w;
            }
            if (result != ESP_OK) break;
            left -= (long)got;
        }
        if (left > 0) result = ESP_FAIL; // incomplete response: the connection must be closed
    }
    free(buf);
    fclose(f);
    return result;
}

static esp_err_t delete_handler(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    int id;
    if (!id_from_uri(req->uri, &id)) return fail(req, "404 Not Found", T("web.err.not_found"));
    app_status_t st;
    app_get_status(&st);
    if (!st.card_ok) return fail(req, "503 Service Unavailable", card_problem(&st));
    if (st.recording) return fail(req, "409 Conflict", T("web.err.delete_recording"));
    if (!app_delete_recording(id)) return fail(req, "404 Not Found", T("web.err.not_found"));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// PUT /rec/<number> with the new name in the body (UTF-8 text; empty removes the name).
static esp_err_t rename_handler(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    int id;
    if (!id_from_uri(req->uri, &id)) return fail(req, "404 Not Found", T("web.err.not_found"));
    char title[200];
    if (req->content_len >= sizeof(title)) return fail(req, "413 Payload Too Large", T("web.err.name_long"));
    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, title + got, req->content_len - got);
        if (r <= 0) return ESP_FAIL;
        got += (size_t)r;
    }
    title[got] = 0;

    app_status_t st;
    app_get_status(&st);
    if (!st.card_ok) return fail(req, "503 Service Unavailable", card_problem(&st));
    if (st.recording) return fail(req, "409 Conflict", T("web.err.rename_recording"));
    if (!app_rename_recording(id, title)) return fail(req, "404 Not Found", T("web.err.not_found"));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// ---------- Initialization ----------

void web_stop(void)
{
    if (!s_server) return;
    mdns_free();
    httpd_stop(s_server);
    s_server = NULL;
}

void web_set_hostname(const char *hostname)
{
    if (s_server) mdns_hostname_set(hostname);
}

void web_set_credentials(const char *user, const char *pass)
{
    char plain[80];
    unsigned char b64[112];
    size_t olen = 0;
    int plen = snprintf(plain, sizeof(plain), "%s:%s", user, pass);
    if (plen < 0 || plen >= (int)sizeof(plain)) return;
    if (mbedtls_base64_encode(b64, sizeof(b64) - 1, &olen, (const unsigned char *)plain, (size_t)plen) != 0) return;
    b64[olen] = 0;
    int next = !s_auth_idx;
    snprintf(s_auth[next], sizeof(s_auth[next]), "Basic %s", (char *)b64);
    s_auth_idx = next;
}

esp_err_t web_start(const char *user, const char *pass, const char *hostname)
{
    if (s_server) return ESP_OK;
    web_set_credentials(user, pass);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true; // browsers open several connections; recycle the oldest ones
    cfg.max_uri_handlers = 14;
    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) return err;

    static const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page_handler},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/lang", .method = HTTP_GET, .handler = lang_handler},
        {.uri = "/api/time", .method = HTTP_POST, .handler = time_handler},
        {.uri = "/api/log", .method = HTTP_GET, .handler = log_handler},
        {.uri = "/api/screen", .method = HTTP_GET, .handler = screen_handler},
        {.uri = "/api/firmware", .method = HTTP_POST, .handler = firmware_handler},
        {.uri = "/api/recordings", .method = HTTP_GET, .handler = list_handler},
        {.uri = "/rec/*", .method = HTTP_GET, .handler = file_handler},
        {.uri = "/rec/*", .method = HTTP_DELETE, .handler = delete_handler},
        {.uri = "/rec/*", .method = HTTP_PUT, .handler = rename_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) httpd_register_uri_handler(s_server, &routes[i]);

    // http://<hostname>.local on the local network.
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(hostname);
        mdns_instance_name_set("P4 Recorder");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    } else {
        ESP_LOGW(TAG, "mDNS unavailable; use the IP address");
    }
    return ESP_OK;
}

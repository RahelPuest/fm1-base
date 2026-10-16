/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (from fm1-radio) */
/* WiFi (FM1B_WIFI): a station on JieLi's WiFi libraries (wl_wifi_sta_sfc, wpasupplicant, hsm: Apache-2.0, binary
 * only) and the SDK's lwIP, on the shim (radio.c, ble/shim, ble/wifi). On, off, a scan list, joining, and the
 * networks joined kept in the app's store object OBJ_WIFI (six, the most recent first), joined again at the next
 * plat_wifi_on. The app's network code runs in a task of its own (plat_net_task) and talks lwIP's sockets.
 * Part of the unity build (unity.c). */
#include "shim.h"

/* ---- the library's API (include_lib/net/wifi/wifi_connect.h) */
enum { STA_MODE = 1, AP_MODE, SMP_CFG_MODE, MP_TEST_MODE, P2P_MODE, NONE_MODE };
enum {
    WIFI_EVENT_MODULE_INIT, WIFI_EVENT_MODULE_START, WIFI_EVENT_MODULE_STOP,
    WIFI_EVENT_MODULE_START_ERR,
    WIFI_EVENT_AP_START, WIFI_EVENT_AP_STOP,
    WIFI_EVENT_STA_START, WIFI_EVENT_STA_STOP,
    WIFI_EVENT_STA_SCAN_COMPLETED,
    WIFI_EVENT_STA_CONNECT_SUCC,
    WIFI_EVENT_STA_CONNECT_TIMEOUT_NOT_FOUND_SSID,
    WIFI_EVENT_STA_CONNECT_ASSOCIAT_FAIL,
    WIFI_EVENT_STA_CONNECT_ASSOCIAT_TIMEOUT,
    WIFI_EVENT_STA_DISCONNECT,
    WIFI_EVENT_SMP_CFG_START, WIFI_EVENT_SMP_CFG_STOP,
    WIFI_EVENT_SMP_CFG_TIMEOUT, WIFI_EVENT_SMP_CFG_COMPLETED,
    WIFI_EVENT_STA_NETWORK_STACK_DHCP_SUCC,
    WIFI_EVENT_STA_NETWORK_STACK_DHCP_TIMEOUT,
};
struct wifi_store_info {
    int mode;
    u8 pwd[2][64];
    u8 ssid[2][33];
    int p2p_role;
    u8 sta_cnt;
    u8 connect_best_network;
} __attribute__((packed));
struct wifi_scan_ssid_info {
    char ssid[32];
    unsigned int ssid_len;
    unsigned char mac_addr[6];
    char rssi, snr, rssi_db, rssi_rsv;
    unsigned int channel_number;
    unsigned char SignalStrength, SignalQuality, SupportedRates[16];
    char enc;
};
struct netif_info { u32 ip, gw, netmask; };
extern int wifi_on(void);
extern int wifi_off(void);
extern int wifi_is_on(void);
extern int wifi_enter_sta_mode(char *sta_ssid, char *sta_pwd);
extern void wifi_set_event_callback(int (*cb)(void *, int));
extern void wifi_set_store_ssid_cnt(u32 cnt);
extern void wifi_set_sta_connect_timeout(int sec);
extern int wifi_set_default_mode(struct wifi_store_info *parm, char force, char store);
extern char wifi_get_rssi(void);
extern int wifi_scan_req(void);
extern struct wifi_scan_ssid_info *wifi_get_scan_result(u32 *ssid_cnt);
extern void wifi_clear_scan_result(void);
extern void wifi_set_tx_rate_control_tab(u32 tab);
extern void lwip_get_netif_info(u8 is_wireless, struct netif_info *info);

/* the station's MAC: the BT address kept in syscfg (radio.c), made a locally administered unicast one; without it
 * the library joins as 00:00:00:00:00:00 */
int init_net_device_mac_addr(char *macaddr, char ap_mode)
{
    const uint8_t *b = bt_get_mac_addr();
    unsigned i;
    (void)ap_mode;
    for (i = 0; i < 6u; i++)
        macaddr[i] = (char)b[i];
    macaddr[0] = (char)(((uint8_t)macaddr[0] & 0xFC) | 0x02);
    macaddr[5] ^= 0x01;
    return 0;
}

/* ---- the networks joined (OBJ_WIFI), the most recent first */
#define NET_N 6
typedef struct { char ssid[33], pass[64]; } net_t;
static struct {
    uint32_t magic;
    net_t n[NET_N];
} nets;
#define NETS_MAGIC 0x57494632u                    /* "WIF2" */
static uint8_t nets_loaded;

static int w_eq(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}
static void w_cpy(char *d, const char *s, unsigned max)
{
    unsigned i;
    for (i = 0; i + 1u < max && s && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}
static void nets_load(void)
{
    if (nets_loaded)
        return;
    nets_loaded = 1;
    memset(&nets, 0, sizeof nets);
    if (plat_store_load(OBJ_WIFI, &nets, sizeof nets) != (int)sizeof nets || nets.magic != NETS_MAGIC) {
        memset(&nets, 0, sizeof nets);
        nets.magic = NETS_MAGIC;
    }
}
static void nets_save(void) { plat_store_save(OBJ_WIFI, &nets, sizeof nets); }
static int nets_count(void)
{
    int i;
    for (i = 0; i < NET_N && nets.n[i].ssid[0]; i++)
        ;
    return i;
}
static int nets_find(const char *ssid)
{
    int i;
    for (i = 0; i < NET_N && nets.n[i].ssid[0]; i++)
        if (w_eq(nets.n[i].ssid, ssid))
            return i;
    return -1;
}
/* to the front (joined): the others move down, the oldest falls off */
static void nets_front(const char *ssid, const char *pass)
{
    net_t e;
    int i = nets_find(ssid);
    memset(&e, 0, sizeof e);
    w_cpy(e.ssid, ssid, sizeof e.ssid);
    w_cpy(e.pass, pass, sizeof e.pass);
    if (i < 0)
        i = NET_N - 1;
    for (; i > 0; i--)
        nets.n[i] = nets.n[i - 1];
    nets.n[0] = e;
    nets_save();
}

/* ---- the state */
static volatile uint8_t wifi_st;                  /* WIFI_* (plat.h) */
static uint8_t wifi_powered;                      /* wifi_on() done, wifi_off() not since */
static uint32_t wifi_ip;
static char wifi_ssid[33], wifi_pass[64];         /* the network joined / being joined */
static const char *wifi_why = "";                 /* WIFI_FAILED: what went wrong */
static int8_t wifi_rssi;
static int wifi_try;                              /* joining the saved networks: the one being tried */
static volatile uint8_t wifi_join_due, wifi_scan_done;

static void wifi_join(const char *ssid, const char *pass)
{
    w_cpy(wifi_ssid, ssid, sizeof wifi_ssid);
    w_cpy(wifi_pass, pass, sizeof wifi_pass);
    wifi_ip = 0;
    wifi_why = "";
    wifi_st = WIFI_JOINING;
    wifi_enter_sta_mode(wifi_ssid, wifi_pass);
}

static int wifi_event_cb(void *ctx, int event)
{
    char lb[24] = "wifi ev ";
    (void)ctx;
    lb[8] = (char)('0' + event / 10 % 10);
    lb[9] = (char)('0' + event % 10);
    lb[10] = 0;
    shim_log(lb);
    switch (event) {
    case WIFI_EVENT_MODULE_INIT: {                /* not started yet: the network it starts on */
        static struct wifi_store_info m;
        memset(&m, 0, sizeof m);
        m.mode = STA_MODE;
        wifi_try = 0;
        if (nets_count()) {
            memcpy(m.ssid[0], nets.n[0].ssid, sizeof m.ssid[0] - 1u);
            memcpy(m.pwd[0], nets.n[0].pass, sizeof m.pwd[0] - 1u);
            w_cpy(wifi_ssid, nets.n[0].ssid, sizeof wifi_ssid);
            w_cpy(wifi_pass, nets.n[0].pass, sizeof wifi_pass);
        }
        wifi_set_sta_connect_timeout(20);
        wifi_set_default_mode(&m, 1, 0);
        break;
    }
    case WIFI_EVENT_MODULE_START:                 /* every rate allowed, 1M CCK .. MCS7, as the SDK's demo_wifi */
        wifi_set_tx_rate_control_tab(0xFFFFFu);
        break;
    case WIFI_EVENT_STA_START:
        wifi_st = wifi_ssid[0] ? WIFI_JOINING : WIFI_ON;
        break;
    case WIFI_EVENT_STA_SCAN_COMPLETED:
        wifi_scan_done = 1;
        break;
    case WIFI_EVENT_STA_CONNECT_SUCC:
        wifi_st = WIFI_ADDRESS;
        break;
    case WIFI_EVENT_STA_NETWORK_STACK_DHCP_SUCC: {
        struct netif_info ni = {0};
        lwip_get_netif_info(1, &ni);
        wifi_ip = ni.ip;
        wifi_st = WIFI_CONNECTED;
        wifi_join_due = 2;                        /* the main loop: this network to the front */
        break;
    }
    case WIFI_EVENT_STA_DISCONNECT:
        if (wifi_st == WIFI_CONNECTED || wifi_st == WIFI_ADDRESS) {
            wifi_st = WIFI_JOINING;               /* the library joins again by itself */
            wifi_ip = 0;
        }
        break;
    case WIFI_EVENT_STA_CONNECT_TIMEOUT_NOT_FOUND_SSID:
        if (wifi_try + 1 < nets_count()) {        /* the saved networks: the next one */
            wifi_try++;
            wifi_join_due = 1;
        } else {
            wifi_st = WIFI_FAILED;
            wifi_why = "not found";
        }
        break;
    case WIFI_EVENT_STA_CONNECT_ASSOCIAT_FAIL:
    case WIFI_EVENT_STA_CONNECT_ASSOCIAT_TIMEOUT:
        wifi_st = WIFI_FAILED;
        wifi_why = "wrong password?";
        break;
    case WIFI_EVENT_STA_NETWORK_STACK_DHCP_TIMEOUT:
        wifi_st = WIFI_FAILED;
        wifi_why = "no address";
        break;
    case WIFI_EVENT_MODULE_START_ERR:
        wifi_st = WIFI_FAILED;
        wifi_why = "radio did not start";
        break;
    default:
        break;
    }
    return 0;
}

/* ---- the scan list */
#define SCAN_N 12
typedef struct { char ssid[33]; int8_t rssi; uint8_t secured; } scan_t;
static scan_t scan_list[SCAN_N];
static int scan_n;

/* after WIFI_EVENT_STA_SCAN_COMPLETED (a scan of ours, or a join's): the results, strongest first, each network
 * once (the strongest of its access points) */
static void wifi_scan_collect(void)
{
    u32 n = 0, i;
    struct wifi_scan_ssid_info *r = wifi_get_scan_result(&n);
    scan_n = 0;
    for (i = 0; r && i < n && scan_n < SCAN_N; i++) {
        scan_t *s = &scan_list[scan_n];
        unsigned k, len = r[i].ssid_len < 32u ? r[i].ssid_len : 32u;
        int j;
        if (!len)
            continue;                             /* hidden */
        for (k = 0; k < len; k++)
            s->ssid[k] = r[i].ssid[k];
        s->ssid[k] = 0;
        for (j = 0; j < scan_n && !w_eq(scan_list[j].ssid, s->ssid); j++)
            ;
        if (j < scan_n)
            continue;
        s->rssi = (int8_t)r[i].rssi;
        s->secured = r[i].enc != 0;
        for (j = scan_n; j > 0 && scan_list[j - 1].rssi < s->rssi; j--) {
            scan_t t = scan_list[j];
            scan_list[j] = scan_list[j - 1];
            scan_list[j - 1] = t;
        }
        scan_n++;
    }
    if (r)
        wifi_clear_scan_result();
}

/* the main loop's turn: timers, every task that can run, then what the events left to do */
static void wifi_poll(void)
{
    if (wifi_st == WIFI_OFF)
        return;
    shim_timers();
    shim_run();
    radio_stuck_check();
    radio_settle(wifi_st >= WIFI_ON);
    if (wifi_join_due == 1 && wifi_try < nets_count()) {
        wifi_join_due = 0;
        wifi_join(nets.n[wifi_try].ssid, nets.n[wifi_try].pass);
    } else if (wifi_join_due == 2) {
        wifi_join_due = 0;
        nets_front(wifi_ssid, wifi_pass);         /* joined: kept, the most recent first */
    }
    if (wifi_st == WIFI_CONNECTED && wifi_is_on())
        wifi_rssi = (int8_t)wifi_get_rssi();
    if (wifi_scan_done) {
        wifi_scan_done = 0;
        wifi_scan_collect();
    }
}

/* ---- plat.h */
void plat_wifi_on(int on)
{
    nets_load();
    if (on && wifi_st == WIFI_OFF) {
        static uint8_t first = 1;
        wifi_ip = 0;
        wifi_ssid[0] = 0;
        wifi_why = "";
        wifi_rssi = 0;
        wifi_st = WIFI_STARTING;
        radio_begin("WiFi: starting");
        if (first) {
            first = 0;
            wifi_set_store_ssid_cnt(1);
            wifi_set_event_callback(wifi_event_cb);
        }
        wifi_powered = 1;
        if (wifi_on())
            shim_log("WiFi: wifi_on failed");
    } else if (!on && wifi_st != WIFI_OFF) {
        wifi_st = WIFI_OFF;
        wifi_ip = 0;
        if (wifi_powered) {
            wifi_powered = 0;
            wifi_off();
            shim_log("WiFi: off");
        }
        radio_clean_exit();
    }
}
int plat_wifi_state(void) { return wifi_st; }
const char *plat_wifi_why(void) { return wifi_st == WIFI_FAILED ? wifi_why : ""; }
const char *plat_wifi_ssid(void) { return wifi_ssid; }
uint32_t plat_wifi_ip(void) { return wifi_st == WIFI_CONNECTED ? wifi_ip : 0; }
int plat_wifi_rssi(void) { return wifi_st == WIFI_CONNECTED ? wifi_rssi : 0; }
int plat_wifi_scan(void) { return wifi_st >= WIFI_ON && wifi_st != WIFI_FAILED ? wifi_scan_req() : -1; }
int plat_wifi_found(int i, char *ssid, int *rssi, int *secured)
{
    if (i < 0 || i >= scan_n)
        return 0;
    if (ssid)
        w_cpy(ssid, scan_list[i].ssid, 33);
    if (rssi)
        *rssi = scan_list[i].rssi;
    if (secured)
        *secured = scan_list[i].secured;
    return 1;
}
int plat_wifi_join(const char *ssid, const char *pass)
{
    int i;
    nets_load();
    if (wifi_st == WIFI_OFF || wifi_st == WIFI_STARTING || !ssid || !ssid[0])
        return -1;
    i = nets_find(ssid);
    if (!pass)
        pass = i >= 0 ? nets.n[i].pass : "";
    wifi_try = NET_N;                             /* (by hand: no going on to the saved ones) */
    wifi_join(ssid, pass);
    return 0;
}
int plat_wifi_saved(int i, char *ssid)
{
    nets_load();
    if (i < 0 || i >= nets_count())
        return 0;
    if (ssid)
        w_cpy(ssid, nets.n[i].ssid, 33);
    return 1;
}
void plat_wifi_forget(int i)
{
    nets_load();
    if (i < 0 || i >= nets_count())
        return;
    for (; i < NET_N - 1; i++)
        nets.n[i] = nets.n[i + 1];
    memset(&nets.n[NET_N - 1], 0, sizeof nets.n[0]);
    nets_save();
}
int plat_net_task(void (*fn)(void *), void *arg, const char *name) { return task_create(fn, arg, name); }

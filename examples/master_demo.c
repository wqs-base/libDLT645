/*
 * master_demo.c - Example DL/T 645-2007 master.
 *
 * Two modes:
 *   master_demo loopback                 - in-process meter, no hardware
 *   master_demo <device> <addr> [di]     - talk to a real meter over a
 *                                          serial port (8E1)
 *
 * SPDX-License-Identifier: MIT
 */
#include "dlt645.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DLT645_HAVE_SERIAL
#include "dlt645_serial.h"
#endif

/* ------------------------------------------------------------------ */
/* In-memory meter used by the loopback mode                           */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  buf[2048];
    size_t   head, tail;
} fifo_t;

typedef struct {
    fifo_t   m2s, s2m;
    uint32_t now;
} link_t;

static int fifo_push(fifo_t *f, const uint8_t *d, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        size_t next = (f->head + 1u) % sizeof(f->buf);
        if (next == f->tail) break;
        f->buf[f->head] = d[i];
        f->head = next;
    }
    return (int)i;
}

static int fifo_pop(fifo_t *f, uint8_t *d, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (f->tail == f->head) break;
        d[i] = f->buf[f->tail];
        f->tail = (f->tail + 1u) % sizeof(f->buf);
    }
    return (int)i;
}

static int l_m_send(void *u, const uint8_t *d, size_t n, uint32_t t)
{ (void)t; return fifo_push(&((link_t *)u)->m2s, d, n); }
static int l_m_recv(void *u, uint8_t *d, size_t n, uint32_t t)
{ (void)t; return fifo_pop(&((link_t *)u)->s2m, d, n); }
static int l_s_send(void *u, const uint8_t *d, size_t n, uint32_t t)
{ (void)t; return fifo_push(&((link_t *)u)->s2m, d, n); }
static int l_s_recv(void *u, uint8_t *d, size_t n, uint32_t t)
{ (void)t; return fifo_pop(&((link_t *)u)->m2s, d, n); }
static uint32_t l_now(void *u) { return ((link_t *)u)->now; }
static void l_delay(void *u, uint32_t ms) { (void)ms; ((link_t *)u)->now++; }

static uint8_t g_energy[4] = { 0x78, 0x56, 0x34, 0x12 }; /* 123456.78 */

static dlt645_status_t demo_read(void *user, uint32_t di,
                                 const uint8_t **data, size_t *len)
{
    (void)user;
    if (di == 0x00000000u || di == 0x00010000u) {
        *data = g_energy;
        *len = 4;
        return DLT645_OK;
    }
    return DLT645_ERR_STATE;
}

/* ------------------------------------------------------------------ */
/* Master callbacks                                                    */
/* ------------------------------------------------------------------ */

static void on_frame(dlt645_master_t *m, const dlt645_frame_t *f, void *user)
{
    uint8_t plain[DLT645_MAX_DATA];
    size_t plen = 0;
    char name[128];
    (void)m;
    (void)user;

    if (dlt645_frame_data(f, plain, sizeof(plain), &plen) != DLT645_OK) {
        return;
    }
    if (plen >= 4u) {
        uint32_t di = dlt645_di_read(plain);
        dlt645_di_format_name(di, name, sizeof(name));
        printf("  <- %s (DI=0x%08X) ctrl=0x%02X\n", name, di, f->ctrl);
        if (plen > 4u) {
            char value[64];
            const dlt645_di_info_t *info = dlt645_di_lookup(di);
            uint8_t dec = (info != NULL) ? info->decimals : 2u;
            if (dlt645_bcd_format(&plain[4], (uint8_t)(plen - 4u), dec,
                                  value, sizeof(value)) == DLT645_OK) {
                printf("     value = %s %s\n", value,
                       (info != NULL) ? info->unit : "");
            }
        }
    }
}

static void on_done(dlt645_master_t *m, dlt645_status_t status, void *user)
{
    (void)m;
    (void)user;
    printf("  transaction finished: %s\n", dlt645_strerror(status));
}

/* ------------------------------------------------------------------ */

static int run_loopback(const char *addr_text, uint32_t di)
{
    link_t lk;
    dlt645_master_t master;
    dlt645_slave_t slave;
    dlt645_master_cfg_t cfg;
    dlt645_slave_model_t model;
    dlt645_addr_t addr;
    dlt645_port_t mp = { &lk, l_m_send, l_m_recv, l_now, l_delay };
    dlt645_port_t sp = { &lk, l_s_send, l_s_recv, l_now, l_delay };
    int i;

    memset(&lk, 0, sizeof(lk));
    memset(&cfg, 0, sizeof(cfg));
    cfg.retries = 2;
    cfg.response_timeout_ms = 200;
    cfg.byte_timeout_ms = 200;
    cfg.add_preamble = 1;

    memset(&model, 0, sizeof(model));
    model.on_read = demo_read;

    if (dlt645_addr_parse(addr_text, &addr) != DLT645_OK) {
        fprintf(stderr, "invalid meter address: %s\n", addr_text);
        return 2;
    }

    dlt645_master_init(&master, &mp, &cfg);
    dlt645_master_set_callbacks(&master, on_frame, on_done, NULL);
    dlt645_slave_init(&slave, &sp, &addr, &model);

    printf("loopback: reading DI=0x%08X from meter %s\n", di, addr_text);
    dlt645_master_build_read(&master, &addr, di);
    for (i = 0; i < 1000 && dlt645_master_busy(&master); i++) {
        lk.now++;
        dlt645_slave_poll(&slave, lk.now);
        dlt645_master_poll(&master, lk.now);
    }
    return (dlt645_master_get_result(&master) == DLT645_OK) ? 0 : 1;
}

#ifdef DLT645_HAVE_SERIAL
static int run_serial(const char *device, const char *addr_text, uint32_t di)
{
    dlt645_serial_t serial;
    dlt645_port_t port;
    dlt645_master_t master;
    dlt645_master_cfg_t cfg;
    dlt645_addr_t addr;
    dlt645_status_t st;

    if (dlt645_serial_open(&serial, device, 2400) != DLT645_OK) {
        fprintf(stderr, "cannot open %s\n", device);
        return 2;
    }
    dlt645_serial_port(&serial, &port);

    memset(&cfg, 0, sizeof(cfg));
    cfg.retries = 3;
    cfg.response_timeout_ms = 500;
    cfg.byte_timeout_ms = 500;
    cfg.add_preamble = 1;

    if (dlt645_addr_parse(addr_text, &addr) != DLT645_OK) {
        dlt645_serial_close(&serial);
        fprintf(stderr, "invalid meter address: %s\n", addr_text);
        return 2;
    }

    dlt645_master_init(&master, &port, &cfg);
    dlt645_master_set_callbacks(&master, on_frame, on_done, NULL);

    printf("serial: reading DI=0x%08X from meter %s on %s\n",
           di, addr_text, device);
    dlt645_master_build_read(&master, &addr, di);
    st = dlt645_master_run(&master, 3000);
    dlt645_serial_close(&serial);
    return (st == DLT645_OK) ? 0 : 1;
}
#endif

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "loopback") == 0) {
        const char *addr = (argc >= 3) ? argv[2] : "123456789012";
        uint32_t di = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 16)
                                  : 0x00010000u;
        return run_loopback(addr, di);
    }

#ifdef DLT645_HAVE_SERIAL
    if (argc >= 3) {
        uint32_t di = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 16)
                                  : 0x00010000u;
        return run_serial(argv[1], argv[2], di);
    }
#endif
    fprintf(stderr,
            "usage: %s loopback [address] [di-hex]\n"
#ifdef DLT645_HAVE_SERIAL
            "       %s <serial-device> <address> [di-hex]\n"
#endif
            , argv[0]
#ifdef DLT645_HAVE_SERIAL
            , argv[0]
#endif
            );
    return 2;
}

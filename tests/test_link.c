/*
 * test_link.c - Master/slave integration over an in-memory loopback.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dlt645_slave.h"
#include "dlt645_master.h"
#include "dlt645_codec.h"
#include "test_util.h"

/* ------------------------------------------------------------------ */
/* In-memory byte link                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t buf[1024];
    size_t  head;
    size_t  tail;
} fifo_t;

typedef struct {
    fifo_t   m2s;
    fifo_t   s2m;
    uint32_t now;
} link_t;

static int fifo_push(fifo_t *f, const uint8_t *d, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        size_t next = (f->head + 1u) % sizeof(f->buf);
        if (next == f->tail) {
            break;
        }
        f->buf[f->head] = d[i];
        f->head = next;
    }
    return (int)i;
}

static int fifo_pop(fifo_t *f, uint8_t *d, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (f->tail == f->head) {
            break;
        }
        d[i] = f->buf[f->tail];
        f->tail = (f->tail + 1u) % sizeof(f->buf);
    }
    return (int)i;
}

static int link_m_send(void *u, const uint8_t *d, size_t n, uint32_t t)
{
    (void)t;
    return fifo_push(&((link_t *)u)->m2s, d, n);
}
static int link_m_recv(void *u, uint8_t *d, size_t n, uint32_t t)
{
    (void)t;
    return fifo_pop(&((link_t *)u)->s2m, d, n);
}
static int link_s_send(void *u, const uint8_t *d, size_t n, uint32_t t)
{
    (void)t;
    return fifo_push(&((link_t *)u)->s2m, d, n);
}
static int link_s_recv(void *u, uint8_t *d, size_t n, uint32_t t)
{
    (void)t;
    return fifo_pop(&((link_t *)u)->m2s, d, n);
}
static uint32_t link_now(void *u)
{
    return ((link_t *)u)->now;
}
static void link_delay(void *u, uint32_t ms)
{
    (void)ms;
    ((link_t *)u)->now += 1u;
}

/* ------------------------------------------------------------------ */
/* Test meter data model                                               */
/* ------------------------------------------------------------------ */

static uint8_t g_meter_data[300];
static int     g_read_mode; /* 0 = 300 bytes, 1 = no data */

static dlt645_status_t model_read(void *u, uint32_t di,
                                  const uint8_t **data, size_t *len)
{
    (void)u;
    if (g_read_mode == 1) {
        return DLT645_ERR_STATE;
    }
    if (di == 0x00010000u) {
        *data = g_meter_data;
        *len = sizeof(g_meter_data);
        return DLT645_OK;
    }
    return DLT645_ERR_STATE;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void test_builders(void)
{
    dlt645_addr_t addr;
    uint8_t frame[64];
    size_t len;

    CHECK_EQ_INT(dlt645_addr_parse("123456789012", &addr), DLT645_OK);

    len = dlt645_build_read_frame(&addr, 0x00010000u, frame, sizeof(frame));
    {
        static const uint8_t expect[] = {
            0x68, 0x12, 0x90, 0x78, 0x56, 0x34, 0x12, 0x68,
            0x11, 0x04, 0x33, 0x33, 0x34, 0x33, 0x68, 0x16
        };
        CHECK_EQ_INT(len, sizeof(expect));
        CHECK_EQ_MEM(frame, expect, sizeof(expect));
    }

    /* read address uses AA..AA, L=0 -> 12 bytes */
    len = dlt645_build_read_addr_frame(NULL, frame, sizeof(frame));
    CHECK_EQ_INT(len, 12u);
    CHECK_EQ_INT(frame[1], 0xAA);
    CHECK_EQ_INT(frame[8], 0x13);

    /* broadcast time frame carries the sync address */
    {
        dlt645_time_t t = {24, 1, 1, 0, 0, 0};
        len = dlt645_build_broadcast_time_frame(&t, frame, sizeof(frame));
        CHECK_EQ_INT(frame[1], 0x99);
        CHECK_EQ_INT(frame[8], 0x08);
        CHECK_EQ_INT(frame[9], 0x06);
    }
}

/* Drive master and slave polls together. */
static void pump(link_t *lk, dlt645_master_t *m, dlt645_slave_t *s,
                 int max_ticks)
{
    int i;
    for (i = 0; i < max_ticks && dlt645_master_busy(m); i++) {
        lk->now++;
        dlt645_slave_poll(s, lk->now);
        dlt645_master_poll(m, lk->now);
    }
}

static uint8_t g_rx_accum[512];
static size_t  g_rx_accum_len;
static int     g_rx_frames;

static void on_frame(dlt645_master_t *m, const dlt645_frame_t *f, void *user)
{
    uint8_t plain[DLT645_MAX_DATA];
    size_t plen = 0;
    size_t vlen;
    (void)m;
    (void)user;
    g_rx_frames++;
    if (dlt645_frame_data(f, plain, sizeof(plain), &plen) != DLT645_OK ||
        plen <= 4u) {
        return;
    }
    vlen = plen - 4u;
    if (dlt645_ctrl_func(f->ctrl) == DLT645_FUNC_READ_FOLLOW) {
        vlen -= 1u; /* trailing frame sequence number */
    }
    if (g_rx_accum_len + vlen <= sizeof(g_rx_accum)) {
        memcpy(&g_rx_accum[g_rx_accum_len], &plain[4], vlen);
        g_rx_accum_len += vlen;
    }
}

static void test_read_300_bytes(void)
{
    link_t lk;
    dlt645_master_t master;
    dlt645_slave_t slave;
    dlt645_master_cfg_t cfg;
    dlt645_slave_model_t model;
    dlt645_addr_t addr;
    size_t i;

    memset(&lk, 0, sizeof(lk));
    for (i = 0; i < sizeof(g_meter_data); i++) {
        g_meter_data[i] = (uint8_t)(i & 0xFFu);
    }
    g_read_mode = 0;
    g_rx_accum_len = 0;
    g_rx_frames = 0;

    memset(&cfg, 0, sizeof(cfg));
    cfg.retries = 2;
    cfg.response_timeout_ms = 50;
    cfg.byte_timeout_ms = 50;
    cfg.add_preamble = 1;

    memset(&model, 0, sizeof(model));
    model.on_read = model_read;

    (void)dlt645_addr_parse("123456789012", &addr);

    dlt645_port_t mp = { &lk, link_m_send, link_m_recv, link_now, link_delay };
    dlt645_port_t sp = { &lk, link_s_send, link_s_recv, link_now, link_delay };

    dlt645_master_init(&master, &mp, &cfg);
    dlt645_master_set_callbacks(&master, on_frame, NULL, NULL);
    dlt645_slave_init(&slave, &sp, &addr, &model);

    CHECK_EQ_INT(dlt645_master_build_read(&master, &addr, 0x00010000u),
                 DLT645_OK);
    pump(&lk, &master, &slave, 200);

    CHECK_EQ_INT(dlt645_master_get_state(&master), DLT645_MASTER_DONE);
    CHECK_EQ_INT(dlt645_master_get_result(&master), DLT645_OK);
    CHECK_EQ_INT(g_rx_frames, 2); /* B1 then 92 */
    CHECK_EQ_INT(g_rx_accum_len, 300);
    CHECK_EQ_MEM(g_rx_accum, g_meter_data, 300);
}

static void test_read_no_data(void)
{
    link_t lk;
    dlt645_master_t master;
    dlt645_slave_t slave;
    dlt645_master_cfg_t cfg;
    dlt645_slave_model_t model;
    dlt645_addr_t addr;

    memset(&lk, 0, sizeof(lk));
    g_read_mode = 1;

    memset(&cfg, 0, sizeof(cfg));
    cfg.retries = 2;
    cfg.response_timeout_ms = 50;
    cfg.byte_timeout_ms = 50;
    cfg.add_preamble = 1;

    memset(&model, 0, sizeof(model));
    model.on_read = model_read;

    (void)dlt645_addr_parse("123456789012", &addr);

    dlt645_port_t mp = { &lk, link_m_send, link_m_recv, link_now, link_delay };
    dlt645_port_t sp = { &lk, link_s_send, link_s_recv, link_now, link_delay };

    dlt645_master_init(&master, &mp, &cfg);
    dlt645_slave_init(&slave, &sp, &addr, &model);

    CHECK_EQ_INT(dlt645_master_build_read(&master, &addr, 0x00010000u),
                 DLT645_OK);
    pump(&lk, &master, &slave, 200);

    /* slave answers D1H with the "no data" error bit */
    CHECK_EQ_INT(dlt645_master_get_result(&master), DLT645_ERR_SLAVE);
}

static void test_read_address(void)
{
    link_t lk;
    dlt645_master_t master;
    dlt645_slave_t slave;
    dlt645_master_cfg_t cfg;
    dlt645_slave_model_t model;
    dlt645_addr_t addr;

    memset(&lk, 0, sizeof(lk));
    memset(&cfg, 0, sizeof(cfg));
    cfg.retries = 1;
    cfg.response_timeout_ms = 50;
    cfg.byte_timeout_ms = 50;
    cfg.add_preamble = 1;
    memset(&model, 0, sizeof(model));
    (void)dlt645_addr_parse("123456789012", &addr);

    dlt645_port_t mp = { &lk, link_m_send, link_m_recv, link_now, link_delay };
    dlt645_port_t sp = { &lk, link_s_send, link_s_recv, link_now, link_delay };

    dlt645_master_init(&master, &mp, &cfg);
    dlt645_slave_init(&slave, &sp, &addr, &model);

    CHECK_EQ_INT(dlt645_master_build_read_addr(&master, NULL), DLT645_OK);
    pump(&lk, &master, &slave, 100);
    CHECK_EQ_INT(dlt645_master_get_result(&master), DLT645_OK);
}

int main(void)
{
    test_builders();
    test_read_300_bytes();
    test_read_no_data();
    test_read_address();
    TEST_REPORT("test_link");
}

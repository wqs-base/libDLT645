/*
 * slave_demo.c - Example DL/T 645-2007 meter (slave).
 *
 * Usage: slave_demo <serial-device> [address]
 *
 * Serves a small in-memory data model.  Requires the serial adapter.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dlt645.h"

#include <stdio.h>
#include <string.h>

#ifdef DLT645_HAVE_SERIAL
#include "dlt645_serial.h"
#endif

/* ------------------------------------------------------------------ */
/* Minimal meter data model                                            */
/* ------------------------------------------------------------------ */

static uint8_t g_total_energy[4] = { 0x78, 0x56, 0x34, 0x12 }; /* 123456.78 */
static uint8_t g_voltage[2]      = { 0x00, 0x22 };             /* 220.0 V   */
static uint8_t g_run_status[2]   = { 0x00, 0x00 };

static dlt645_status_t model_read(void *user, uint32_t di,
                                  const uint8_t **data, size_t *len)
{
    (void)user;
    switch (di) {
    case 0x00000000u: /* 组合有功总电能 */
    case 0x00010000u: /* 正向有功总电能 */
        *data = g_total_energy; *len = 4; return DLT645_OK;
    case 0x02010100u: /* A 相电压 */
        *data = g_voltage; *len = 2; return DLT645_OK;
    case 0x04000000u: /* 电表运行状态字（示例） */
        *data = g_run_status; *len = 2; return DLT645_OK;
    default:
        return DLT645_ERR_STATE;
    }
}

static dlt645_status_t model_write(void *user, uint32_t di,
                                   const uint8_t *data, size_t len)
{
    (void)user;
    if (di == 0x00000000u && len == 4u) {
        memcpy(g_total_energy, data, 4);
        return DLT645_OK;
    }
    return DLT645_ERR_STATE;
}

static dlt645_status_t model_auth(void *user, uint8_t pa,
                                  const uint8_t pwd[3], const uint8_t op[4])
{
    (void)user; (void)op;
    /* default password 000000, permission 0 */
    if (pa == 0u && pwd[0] == 0u && pwd[1] == 0u && pwd[2] == 0u) {
        return DLT645_OK;
    }
    return DLT645_ERR_STATE;
}

static void on_event(void *user, const dlt645_frame_t *req,
                     const uint8_t *resp, size_t resp_len)
{
    (void)user;
    printf("request ctrl=0x%02X -> response %u bytes\n", req->ctrl,
           (unsigned)resp_len);
}

int main(int argc, char **argv)
{
    dlt645_addr_t addr;
    dlt645_slave_t slave;
    dlt645_slave_model_t model;
    dlt645_port_t port;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <serial-device> [address]\n", argv[0]);
        return 2;
    }
    if (dlt645_addr_parse(argc >= 3 ? argv[2] : "000000000001", &addr)
        != DLT645_OK) {
        fprintf(stderr, "invalid address\n");
        return 2;
    }

#ifndef DLT645_HAVE_SERIAL
    (void)port;
    fprintf(stderr, "built without the serial adapter; nothing to do\n");
    return 1;
#else
    {
        dlt645_serial_t serial;
        if (dlt645_serial_open(&serial, argv[1], 2400) != DLT645_OK) {
            fprintf(stderr, "cannot open %s\n", argv[1]);
            return 1;
        }
        dlt645_serial_port(&serial, &port);

        memset(&model, 0, sizeof(model));
        model.on_read = model_read;
        model.on_write = model_write;
        model.on_auth = model_auth;

        dlt645_slave_init(&slave, &port, &addr, &model);
        dlt645_slave_set_event_cb(&slave, on_event, NULL);

        printf("meter %s listening on %s (8E1, 2400 bps)\n",
               argc >= 3 ? argv[2] : "000000000001", argv[1]);
        (void)dlt645_slave_run(&slave, 0); /* run forever */
        dlt645_serial_close(&serial);
    }
    return 0;
#endif
}

/*
 * dlt645_slave.h - Slave (meter) side: request parsing, authorisation
 *                  and response generation.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DLT645_SLAVE_H
#define DLT645_SLAVE_H

#include "dlt645_types.h"
#include "dlt645_frame.h"
#include "dlt645_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Data model callbacks.  They may be NULL when the corresponding
 * function is not supported, in which case the slave answers with an
 * "other error" (or "no data") response.
 */

/* Return a pointer to the model-owned data for `di`.  The pointer must
 * stay valid until the next call.  The slave copies from it. */
typedef dlt645_status_t (*dlt645_slave_read_fn)(void *user, uint32_t di,
                                                const uint8_t **data,
                                                size_t *len);

/* Store `len` plain data bytes for `di`. */
typedef dlt645_status_t (*dlt645_slave_write_fn)(void *user, uint32_t di,
                                                 const uint8_t *data,
                                                 size_t len);

/* Validate password `pwd` with permission level `pa` and operator code
 * `op`.  Return DLT645_OK to authorise. */
typedef dlt645_status_t (*dlt645_slave_auth_fn)(void *user, uint8_t pa,
                                                const uint8_t pwd[3],
                                                const uint8_t op[4]);

typedef dlt645_status_t (*dlt645_slave_write_addr_fn)(void *user,
                                                      const dlt645_addr_t *addr);

typedef dlt645_status_t (*dlt645_slave_time_fn)(void *user,
                                                const dlt645_time_t *t);

typedef dlt645_status_t (*dlt645_slave_freeze_fn)(void *user, uint8_t month,
                                                  uint8_t day, uint8_t hour,
                                                  uint8_t minute);

typedef dlt645_status_t (*dlt645_slave_baud_fn)(void *user, uint8_t feature);

typedef dlt645_status_t (*dlt645_slave_pwd_fn)(void *user, uint32_t di,
                                               uint8_t pa, const uint8_t pwd[3]);

/* kind is one of DLT645_FUNC_DEMAND_RESET / _METER_CLEAR / _EVENT_CLEAR. */
typedef dlt645_status_t (*dlt645_slave_clear_fn)(void *user, uint8_t kind,
                                                 uint32_t di);

typedef struct {
    void                      *user;
    dlt645_slave_read_fn       on_read;
    dlt645_slave_write_fn      on_write;
    dlt645_slave_auth_fn       on_auth;
    dlt645_slave_write_addr_fn on_write_addr;
    dlt645_slave_time_fn       on_broadcast_time;
    dlt645_slave_freeze_fn     on_freeze;
    dlt645_slave_baud_fn       on_change_baud;
    dlt645_slave_pwd_fn        on_change_pwd;
    dlt645_slave_clear_fn      on_clear;
} dlt645_slave_model_t;

/* Optional notification for every handled frame. */
typedef void (*dlt645_slave_event_cb)(void *user, const dlt645_frame_t *req,
                                      const uint8_t *resp, size_t resp_len);

typedef struct dlt645_slave {
    dlt645_port_t            port;
    dlt645_slave_model_t     model;
    dlt645_addr_t            addr;
    dlt645_rx_parser_t       rx;

    uint8_t  tx[DLT645_MAX_FRAME + DLT645_PREAMBLE_LEN];
    size_t   tx_len;
    size_t   tx_pos;

    /* follow-up read continuation state */
    uint32_t      follow_di;
    size_t        follow_pos;
    int           follow_active;

    dlt645_slave_event_cb on_event;
    void                 *event_user;
} dlt645_slave_t;

void dlt645_slave_init(dlt645_slave_t *s, const dlt645_port_t *port,
                       const dlt645_addr_t *addr,
                       const dlt645_slave_model_t *model);

void dlt645_slave_set_address(dlt645_slave_t *s, const dlt645_addr_t *addr);
void dlt645_slave_set_event_cb(dlt645_slave_t *s,
                               dlt645_slave_event_cb cb, void *user);

/*
 * Feed received bytes and process requests.  Returns the number of
 * response frames queued (0 or 1) for the bytes consumed.  The queued
 * response is transmitted by dlt645_slave_poll() / _run().
 */
void dlt645_slave_poll(dlt645_slave_t *s, uint32_t now_ms);

/* Blocking convenience: handle requests until `timeout_ms` elapses
 * (0 = run forever).  Returns DLT645_OK on timeout, or the status of
 * the last transmission error. */
dlt645_status_t dlt645_slave_run(dlt645_slave_t *s, uint32_t timeout_ms);

/* Process one already-decoded request and write the response frame into
 * `out`.  Returns the response length, or 0 when no response is due
 * (e.g. broadcast commands).  Useful for unit tests and/or custom I/O. */
size_t dlt645_slave_handle_frame(dlt645_slave_t *s, const dlt645_frame_t *req,
                                 uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* DLT645_SLAVE_H */

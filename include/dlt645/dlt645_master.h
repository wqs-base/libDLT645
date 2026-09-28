/*
 * dlt645_master.h - Master (client) side: request building and a
 *                   non-blocking / blocking transaction engine.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DLT645_MASTER_H
#define DLT645_MASTER_H

#include "dlt645_types.h"
#include "dlt645_frame.h"
#include "dlt645_port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DLT645_MASTER_IDLE = 0,
    DLT645_MASTER_TX,
    DLT645_MASTER_WAIT,
    DLT645_MASTER_DONE
} dlt645_master_state_t;

typedef struct {
    uint8_t  retries;             /* retransmissions, default 3          */
    uint32_t response_timeout_ms; /* wait for a complete reply, def 500  */
    uint32_t byte_timeout_ms;     /* inter-byte gap, default 500         */
    int      add_preamble;        /* send 4 x 0xFE first, default 1      */
} dlt645_master_cfg_t;

struct dlt645_master;

/* Called for every valid response frame (including intermediate
 * follow-up frames) while a transaction is running. */
typedef void (*dlt645_master_frame_cb)(struct dlt645_master *m,
                                       const dlt645_frame_t *frame,
                                       void *user);

/* Called once when the transaction finishes. */
typedef void (*dlt645_master_done_cb)(struct dlt645_master *m,
                                      dlt645_status_t status,
                                      void *user);

typedef struct dlt645_master {
    dlt645_port_t        port;
    dlt645_master_cfg_t  cfg;

    dlt645_master_frame_cb on_frame;
    dlt645_master_done_cb  on_done;
    void                  *user;

    uint8_t  tx[DLT645_MAX_FRAME + DLT645_PREAMBLE_LEN];
    size_t   tx_len;
    size_t   tx_pos;

    dlt645_rx_parser_t rx;

    dlt645_master_state_t state;
    dlt645_status_t       result;

    uint32_t deadline;
    uint32_t last_rx_ms;
    uint8_t  retries_left;

    /* active transaction description (for automatic follow-up reads) */
    uint8_t   func;          /* request function code                  */
    uint32_t  di;            /* data identifier of a read transaction  */
    uint8_t   seq;           /* last follow-up sequence number sent    */
    uint8_t   expect_addr[DLT645_ADDR_LEN];
    int       check_addr;
    int       silent;        /* broadcast: no response expected        */
} dlt645_master_t;

/* Initialise with default configuration.  `cfg` may be NULL. */
void dlt645_master_init(dlt645_master_t *m, const dlt645_port_t *port,
                        const dlt645_master_cfg_t *cfg);

void dlt645_master_set_callbacks(dlt645_master_t *m,
                                 dlt645_master_frame_cb on_frame,
                                 dlt645_master_done_cb on_done,
                                 void *user);

/* ------------------------------------------------------------------ */
/* Transaction control                                                 */
/* ------------------------------------------------------------------ */

/* Send a pre-built frame as the next transaction.  `func` is the
 * application function code (DLT645_FUNC_*), used to match responses.
 * `expect_follow` enables automatic 12H follow-up reads for reads. */
dlt645_status_t dlt645_master_start(dlt645_master_t *m,
                                    const uint8_t *frame, size_t len,
                                    uint8_t func, uint32_t di);

/* Drive the state machine.  Call as often as possible (or from a timer
 * with `now_ms` in milliseconds). */
void dlt645_master_poll(dlt645_master_t *m, uint32_t now_ms);

/* Run to completion using port.delay_ms()/now_ms(). */
dlt645_status_t dlt645_master_run(dlt645_master_t *m, uint32_t timeout_ms);

int                   dlt645_master_busy(const dlt645_master_t *m);
dlt645_master_state_t dlt645_master_get_state(const dlt645_master_t *m);
dlt645_status_t       dlt645_master_get_result(const dlt645_master_t *m);
void                  dlt645_master_abort(dlt645_master_t *m);

/* ------------------------------------------------------------------ */
/* High level request builders (also start the transaction)            */
/* ------------------------------------------------------------------ */

dlt645_status_t dlt645_master_build_read(dlt645_master_t *m,
                                         const dlt645_addr_t *addr,
                                         uint32_t di);

dlt645_status_t dlt645_master_build_read_follow(dlt645_master_t *m,
                                                const dlt645_addr_t *addr,
                                                uint32_t di, uint8_t seq);

dlt645_status_t dlt645_master_build_read_addr(dlt645_master_t *m,
                                              const dlt645_addr_t *target);

dlt645_status_t dlt645_master_build_write(dlt645_master_t *m,
                                          const dlt645_addr_t *addr,
                                          uint32_t di,
                                          uint8_t pa, const uint8_t pwd[3],
                                          const uint8_t op[4],
                                          const uint8_t *data, size_t dlen);

dlt645_status_t dlt645_master_build_write_addr(dlt645_master_t *m,
                                               const dlt645_addr_t *new_addr);

dlt645_status_t dlt645_master_build_broadcast_time(dlt645_master_t *m,
                                                   const dlt645_time_t *t);

/* month/day/hour/minute as plain (binary) values. */
dlt645_status_t dlt645_master_build_freeze(dlt645_master_t *m,
                                           const dlt645_addr_t *addr,
                                           uint8_t month, uint8_t day,
                                           uint8_t hour, uint8_t minute,
                                           int broadcast);

dlt645_status_t dlt645_master_build_change_baud(dlt645_master_t *m,
                                                const dlt645_addr_t *addr,
                                                uint8_t feature);

dlt645_status_t dlt645_master_build_change_pwd(dlt645_master_t *m,
                                               const dlt645_addr_t *addr,
                                               uint32_t di,
                                               uint8_t pa_old,
                                               const uint8_t pwd_old[3],
                                               uint8_t pa_new,
                                               const uint8_t pwd_new[3]);

dlt645_status_t dlt645_master_build_demand_reset(dlt645_master_t *m,
                                                 const dlt645_addr_t *addr,
                                                 uint8_t pa, const uint8_t pwd[3],
                                                 const uint8_t op[4]);

dlt645_status_t dlt645_master_build_meter_clear(dlt645_master_t *m,
                                                const dlt645_addr_t *addr,
                                                uint8_t pa, const uint8_t pwd[3],
                                                const uint8_t op[4]);

/* di == 0xFFFFFFFF clears all events, otherwise a specific category. */
dlt645_status_t dlt645_master_build_event_clear(dlt645_master_t *m,
                                                const dlt645_addr_t *addr,
                                                uint8_t pa, const uint8_t pwd[3],
                                                const uint8_t op[4],
                                                uint32_t di);

/* ------------------------------------------------------------------ */
/* Standalone frame builders (no engine), useful for tests/tools)      */
/* ------------------------------------------------------------------ */

size_t dlt645_build_read_frame(const dlt645_addr_t *addr, uint32_t di,
                               uint8_t *out, size_t cap);
size_t dlt645_build_read_follow_frame(const dlt645_addr_t *addr, uint32_t di,
                                      uint8_t seq, uint8_t *out, size_t cap);
size_t dlt645_build_read_addr_frame(const dlt645_addr_t *target,
                                    uint8_t *out, size_t cap);
size_t dlt645_build_write_frame(const dlt645_addr_t *addr, uint32_t di,
                                uint8_t pa, const uint8_t pwd[3],
                                const uint8_t op[4],
                                const uint8_t *data, size_t dlen,
                                uint8_t *out, size_t cap);
size_t dlt645_build_write_addr_frame(const dlt645_addr_t *new_addr,
                                     uint8_t *out, size_t cap);
size_t dlt645_build_broadcast_time_frame(const dlt645_time_t *t,
                                         uint8_t *out, size_t cap);
size_t dlt645_build_freeze_frame(const dlt645_addr_t *addr,
                                 uint8_t month, uint8_t day,
                                 uint8_t hour, uint8_t minute,
                                 uint8_t *out, size_t cap);
size_t dlt645_build_change_baud_frame(const dlt645_addr_t *addr,
                                      uint8_t feature,
                                      uint8_t *out, size_t cap);
size_t dlt645_build_change_pwd_frame(const dlt645_addr_t *addr, uint32_t di,
                                     uint8_t pa_old, const uint8_t pwd_old[3],
                                     uint8_t pa_new, const uint8_t pwd_new[3],
                                     uint8_t *out, size_t cap);
size_t dlt645_build_demand_reset_frame(const dlt645_addr_t *addr,
                                       uint8_t pa, const uint8_t pwd[3],
                                       const uint8_t op[4],
                                       uint8_t *out, size_t cap);
size_t dlt645_build_meter_clear_frame(const dlt645_addr_t *addr,
                                      uint8_t pa, const uint8_t pwd[3],
                                      const uint8_t op[4],
                                      uint8_t *out, size_t cap);
size_t dlt645_build_event_clear_frame(const dlt645_addr_t *addr,
                                      uint8_t pa, const uint8_t pwd[3],
                                      const uint8_t op[4], uint32_t di,
                                      uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* DLT645_MASTER_H */

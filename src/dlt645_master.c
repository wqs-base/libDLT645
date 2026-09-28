/*
 * dlt645_master.c - Master (client) request building and transaction
 *                   engine (non-blocking core + blocking wrapper).
 *
 * SPDX-License-Identifier: MIT
 */
#include "dlt645_master.h"
#include "dlt645_codec.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Small frame-building helpers                                        */
/* ------------------------------------------------------------------ */

static size_t encode_frame(const dlt645_addr_t *addr, uint8_t ctrl,
                           const uint8_t *data, uint8_t dlen, int preamble,
                           uint8_t *out, size_t cap)
{
    dlt645_encode_opts_t opts;
    size_t len = 0;
    opts.add_preamble = preamble;
    if (dlt645_frame_encode(addr, ctrl, data, dlen, &opts, out, cap, &len)
        != DLT645_OK) {
        return 0;
    }
    return len;
}

/* Locate the first 0x68 in `buf` and decode the frame that follows. */
static dlt645_status_t decode_any(const uint8_t *buf, size_t len,
                                  dlt645_frame_t *frame, size_t *offset)
{
    size_t i;
    if (buf == NULL || frame == NULL) {
        return DLT645_ERR_ARG;
    }
    for (i = 0; i < len; i++) {
        if (buf[i] == DLT645_FRAME_START) {
            dlt645_status_t st = dlt645_frame_decode(&buf[i], len - i, frame);
            if (st == DLT645_OK) {
                if (offset != NULL) {
                    *offset = i;
                }
                return DLT645_OK;
            }
        }
    }
    return DLT645_ERR_FORMAT;
}

/* ------------------------------------------------------------------ */
/* Standalone builders                                                 */
/* ------------------------------------------------------------------ */

#define BODY_MAX (4u + 4u + 4u + DLT645_WRITE_MAX_DATA)

size_t dlt645_build_read_frame(const dlt645_addr_t *addr, uint32_t di,
                               uint8_t *out, size_t cap)
{
    uint8_t body[4];
    if (addr == NULL || out == NULL) {
        return 0;
    }
    dlt645_di_write(di, body);
    return encode_frame(addr, DLT645_FUNC_READ_DATA, body, 4, 0, out, cap);
}

size_t dlt645_build_read_follow_frame(const dlt645_addr_t *addr, uint32_t di,
                                      uint8_t seq, uint8_t *out, size_t cap)
{
    uint8_t body[5];
    if (addr == NULL || out == NULL) {
        return 0;
    }
    dlt645_di_write(di, body);
    body[4] = seq;
    return encode_frame(addr, DLT645_FUNC_READ_FOLLOW, body, 5, 0, out, cap);
}

size_t dlt645_build_read_addr_frame(const dlt645_addr_t *target,
                                    uint8_t *out, size_t cap)
{
    dlt645_addr_t wild;
    (void)target;
    if (out == NULL) {
        return 0;
    }
    dlt645_addr_wildcard(&wild);
    return encode_frame(&wild, DLT645_FUNC_READ_ADDR, NULL, 0, 0, out, cap);
}

size_t dlt645_build_write_frame(const dlt645_addr_t *addr, uint32_t di,
                                uint8_t pa, const uint8_t pwd[3],
                                const uint8_t op[4],
                                const uint8_t *data, size_t dlen,
                                uint8_t *out, size_t cap)
{
    uint8_t body[BODY_MAX];
    size_t n = 0;
    if (addr == NULL || out == NULL || pwd == NULL || op == NULL) {
        return 0;
    }
    if (dlen > DLT645_WRITE_MAX_DATA) {
        return 0;
    }
    dlt645_di_write(di, &body[n]);
    n += 4;
    body[n++] = pa;
    memcpy(&body[n], pwd, 3);
    n += 3;
    memcpy(&body[n], op, 4);
    n += 4;
    if (dlen > 0u) {
        if (data == NULL) {
            return 0;
        }
        memcpy(&body[n], data, dlen);
        n += dlen;
    }
    return encode_frame(addr, DLT645_FUNC_WRITE_DATA, body, (uint8_t)n, 0,
                        out, cap);
}

size_t dlt645_build_write_addr_frame(const dlt645_addr_t *new_addr,
                                     uint8_t *out, size_t cap)
{
    dlt645_addr_t wild;
    if (new_addr == NULL || out == NULL) {
        return 0;
    }
    dlt645_addr_wildcard(&wild);
    return encode_frame(&wild, DLT645_FUNC_WRITE_ADDR, new_addr->b,
                        DLT645_ADDR_LEN, 0, out, cap);
}

size_t dlt645_build_broadcast_time_frame(const dlt645_time_t *t,
                                         uint8_t *out, size_t cap)
{
    dlt645_addr_t bc;
    uint8_t body[6];
    if (t == NULL || out == NULL) {
        return 0;
    }
    if (dlt645_time_to_broadcast(t, body) != DLT645_OK) {
        return 0;
    }
    dlt645_addr_broadcast(&bc);
    return encode_frame(&bc, DLT645_FUNC_BROADCAST_TIME, body, 6, 0, out, cap);
}

size_t dlt645_build_freeze_frame(const dlt645_addr_t *addr,
                                 uint8_t month, uint8_t day,
                                 uint8_t hour, uint8_t minute,
                                 uint8_t *out, size_t cap)
{
    uint8_t body[4];
    if (addr == NULL || out == NULL) {
        return 0;
    }
    /* wire order: mm hh DD MM (each packed BCD) */
    body[0] = dlt645_bin_to_bcd(minute);
    body[1] = dlt645_bin_to_bcd(hour);
    body[2] = dlt645_bin_to_bcd(day);
    body[3] = dlt645_bin_to_bcd(month);
    return encode_frame(addr, DLT645_FUNC_FREEZE, body, 4, 0, out, cap);
}

size_t dlt645_build_change_baud_frame(const dlt645_addr_t *addr,
                                      uint8_t feature,
                                      uint8_t *out, size_t cap)
{
    if (addr == NULL || out == NULL) {
        return 0;
    }
    return encode_frame(addr, DLT645_FUNC_CHANGE_BAUD, &feature, 1, 0,
                        out, cap);
}

size_t dlt645_build_change_pwd_frame(const dlt645_addr_t *addr, uint32_t di,
                                     uint8_t pa_old, const uint8_t pwd_old[3],
                                     uint8_t pa_new, const uint8_t pwd_new[3],
                                     uint8_t *out, size_t cap)
{
    uint8_t body[12];
    if (addr == NULL || out == NULL || pwd_old == NULL || pwd_new == NULL) {
        return 0;
    }
    dlt645_di_write(di, &body[0]);
    body[4] = pa_old;
    memcpy(&body[5], pwd_old, 3);
    body[8] = pa_new;
    memcpy(&body[9], pwd_new, 3);
    return encode_frame(addr, DLT645_FUNC_CHANGE_PWD, body, 12, 0, out, cap);
}

static size_t build_pwd_op_frame(const dlt645_addr_t *addr, uint8_t ctrl,
                                 uint8_t pa, const uint8_t pwd[3],
                                 const uint8_t op[4], uint32_t di,
                                 int with_di, uint8_t *out, size_t cap)
{
    uint8_t body[12];
    size_t n = 0;
    if (addr == NULL || out == NULL || pwd == NULL || op == NULL) {
        return 0;
    }
    body[n++] = pa;
    memcpy(&body[n], pwd, 3);
    n += 3;
    memcpy(&body[n], op, 4);
    n += 4;
    if (with_di) {
        dlt645_di_write(di, &body[n]);
        n += 4;
    }
    return encode_frame(addr, ctrl, body, (uint8_t)n, 0, out, cap);
}

size_t dlt645_build_demand_reset_frame(const dlt645_addr_t *addr,
                                       uint8_t pa, const uint8_t pwd[3],
                                       const uint8_t op[4],
                                       uint8_t *out, size_t cap)
{
    return build_pwd_op_frame(addr, DLT645_FUNC_DEMAND_RESET, pa, pwd, op,
                              0, 0, out, cap);
}

size_t dlt645_build_meter_clear_frame(const dlt645_addr_t *addr,
                                      uint8_t pa, const uint8_t pwd[3],
                                      const uint8_t op[4],
                                      uint8_t *out, size_t cap)
{
    return build_pwd_op_frame(addr, DLT645_FUNC_METER_CLEAR, pa, pwd, op,
                              0, 0, out, cap);
}

size_t dlt645_build_event_clear_frame(const dlt645_addr_t *addr,
                                      uint8_t pa, const uint8_t pwd[3],
                                      const uint8_t op[4], uint32_t di,
                                      uint8_t *out, size_t cap)
{
    return build_pwd_op_frame(addr, DLT645_FUNC_EVENT_CLEAR, pa, pwd, op,
                              di, 1, out, cap);
}

/* ------------------------------------------------------------------ */
/* Engine                                                              */
/* ------------------------------------------------------------------ */

void dlt645_master_init(dlt645_master_t *m, const dlt645_port_t *port,
                        const dlt645_master_cfg_t *cfg)
{
    if (m == NULL) {
        return;
    }
    memset(m, 0, sizeof(*m));
    if (port != NULL) {
        m->port = *port;
    }
    if (cfg != NULL) {
        m->cfg = *cfg;
    } else {
        m->cfg.retries = 3;
        m->cfg.response_timeout_ms = 500;
        m->cfg.byte_timeout_ms = 500;
        m->cfg.add_preamble = 1;
    }
    dlt645_rx_init(&m->rx);
    m->state = DLT645_MASTER_IDLE;
    m->result = DLT645_OK;
}

void dlt645_master_set_callbacks(dlt645_master_t *m,
                                 dlt645_master_frame_cb on_frame,
                                 dlt645_master_done_cb on_done,
                                 void *user)
{
    if (m == NULL) {
        return;
    }
    m->on_frame = on_frame;
    m->on_done = on_done;
    m->user = user;
}

int dlt645_master_busy(const dlt645_master_t *m)
{
    return (m != NULL && (m->state == DLT645_MASTER_TX ||
                          m->state == DLT645_MASTER_WAIT)) ? 1 : 0;
}

dlt645_master_state_t dlt645_master_get_state(const dlt645_master_t *m)
{
    return (m != NULL) ? m->state : DLT645_MASTER_IDLE;
}

dlt645_status_t dlt645_master_get_result(const dlt645_master_t *m)
{
    return (m != NULL) ? m->result : DLT645_ERR_ARG;
}

void dlt645_master_abort(dlt645_master_t *m)
{
    if (m == NULL) {
        return;
    }
    m->state = DLT645_MASTER_IDLE;
}

static void master_finish(dlt645_master_t *m, dlt645_status_t status)
{
    m->result = status;
    m->state = DLT645_MASTER_DONE;
    if (m->on_done != NULL) {
        m->on_done(m, status, m->user);
    }
}

static void master_begin_tx(dlt645_master_t *m, uint32_t now_ms)
{
    m->tx_pos = 0;
    m->retries_left = m->cfg.retries;
    m->state = DLT645_MASTER_TX;
    m->deadline = now_ms + m->cfg.response_timeout_ms;
    dlt645_rx_reset(&m->rx);
    m->last_rx_ms = now_ms;
}

dlt645_status_t dlt645_master_start(dlt645_master_t *m,
                                    const uint8_t *frame, size_t len,
                                    uint8_t func, uint32_t di)
{
    dlt645_frame_t f;
    dlt645_status_t st;

    if (m == NULL || frame == NULL || len == 0u || len > sizeof(m->tx)) {
        return DLT645_ERR_ARG;
    }
    if (dlt645_master_busy(m)) {
        return DLT645_ERR_STATE;
    }

    memcpy(m->tx, frame, len);
    m->tx_len = len;
    m->func = func;
    m->di = di;
    m->seq = 0;
    m->silent = 0;

    st = decode_any(frame, len, &f, NULL);
    if (st == DLT645_OK) {
        memcpy(m->expect_addr, f.addr.b, DLT645_ADDR_LEN);
        m->check_addr = 1;
        if (dlt645_addr_is_broadcast(&f.addr)) {
            m->silent = 1;
        }
    } else {
        m->check_addr = 0;
    }

    master_begin_tx(m, (m->port.now_ms != NULL) ? m->port.now_ms(m->port.user)
                                                : 0u);
    return DLT645_OK;
}

static void master_build_follow_frame(dlt645_master_t *m)
{
    dlt645_addr_t addr;
    uint8_t body[5];
    dlt645_encode_opts_t opts;

    memcpy(addr.b, m->expect_addr, DLT645_ADDR_LEN);
    dlt645_di_write(m->di, body);
    m->seq = (uint8_t)(m->seq + 1u);
    if (m->seq == 0u) {
        m->seq = 1u;
    }
    body[4] = m->seq;
    opts.add_preamble = m->cfg.add_preamble;
    m->tx_len = 0;
    /* The follow-up request always carries a concrete (or wildcard)
     * address; use the address from the original request. */
    if (dlt645_frame_encode(&addr, DLT645_FUNC_READ_FOLLOW, body, 5, &opts,
                            m->tx, sizeof(m->tx), &m->tx_len) != DLT645_OK) {
        m->tx_len = 0;
        master_finish(m, DLT645_ERR_ARG);
        return;
    }
    /* Subsequent responses now use function code 0x12. */
    m->func = DLT645_FUNC_READ_FOLLOW;
}

static int master_handle_response(dlt645_master_t *m, uint32_t now_ms)
{
    const dlt645_frame_t *f = dlt645_rx_frame(&m->rx);
    uint8_t func = dlt645_ctrl_func(f->ctrl);
    dlt645_addr_t addr;

    memcpy(addr.b, f->addr.b, DLT645_ADDR_LEN);

    if (func != m->func) {
        return 0; /* not our response; keep waiting */
    }
    if (m->check_addr && !dlt645_addr_match((const dlt645_addr_t *)m->expect_addr,
                                            &addr)) {
        return 0;
    }

    if (m->on_frame != NULL) {
        m->on_frame(m, f, m->user);
    }

    if (dlt645_ctrl_is_error(f->ctrl)) {
        master_finish(m, DLT645_ERR_SLAVE);
        return 1;
    }

    if (dlt645_ctrl_has_followup(f->ctrl) &&
        (func == DLT645_FUNC_READ_DATA || func == DLT645_FUNC_READ_FOLLOW)) {
        /* Follow-up reads address the concrete meter, so latch the
         * address reported by the slave. */
        memcpy(m->expect_addr, f->addr.b, DLT645_ADDR_LEN);
        master_build_follow_frame(m);
        if (m->state == DLT645_MASTER_DONE) {
            return 1;
        }
        master_begin_tx(m, now_ms);
        return 1;
    }

    master_finish(m, DLT645_OK);
    return 1;
}

void dlt645_master_poll(dlt645_master_t *m, uint32_t now_ms)
{
    uint8_t buf[32];

    if (m == NULL) {
        return;
    }

    if (m->state == DLT645_MASTER_TX) {
        if (m->port.send == NULL) {
            master_finish(m, DLT645_ERR_UNSUPPORTED);
            return;
        }
        if (m->tx_len == 0u) {
            master_finish(m, DLT645_ERR_ARG);
            return;
        }
        {
            int n = m->port.send(m->port.user, &m->tx[m->tx_pos],
                                 m->tx_len - m->tx_pos,
                                 m->cfg.response_timeout_ms);
            if (n < 0) {
                master_finish(m, DLT645_ERR_IO);
                return;
            }
            m->tx_pos += (size_t)n;
        }
        if (m->tx_pos >= m->tx_len) {
            if (m->silent) {
                master_finish(m, DLT645_OK);
                return;
            }
            m->state = DLT645_MASTER_WAIT;
            m->deadline = now_ms + m->cfg.response_timeout_ms;
            m->last_rx_ms = now_ms;
            dlt645_rx_reset(&m->rx);
        }
        return;
    }

    if (m->state != DLT645_MASTER_WAIT) {
        return;
    }

    /* Drain available bytes. */
    if (m->port.recv != NULL) {
        int n;
        do {
            n = m->port.recv(m->port.user, buf, sizeof(buf), 0);
            if (n > 0) {
                int i;
                for (i = 0; i < n; i++) {
                    (void)dlt645_rx_feed(&m->rx, buf[i]);
                }
                m->last_rx_ms = now_ms;
                if (dlt645_rx_frame_ready(&m->rx)) {
                    if (master_handle_response(m, now_ms)) {
                        return;
                    }
                }
            }
        } while (n > 0);
    }

    /* Timeouts.  A partial frame that stalls triggers a retry too. */
    if (m->rx.state != DLT645_RX_IDLE &&
        m->rx.state != DLT645_RX_COMPLETE) {
        if ((now_ms - m->last_rx_ms) > m->cfg.byte_timeout_ms) {
            dlt645_rx_reset(&m->rx);
        }
    }
    if ((now_ms - m->last_rx_ms) > m->cfg.response_timeout_ms ||
        (int32_t)(now_ms - m->deadline) >= 0) {
        if (m->retries_left > 0u) {
            m->retries_left--;
            master_begin_tx(m, now_ms);
        } else {
            master_finish(m, DLT645_ERR_TIMEOUT);
        }
    }
}

dlt645_status_t dlt645_master_run(dlt645_master_t *m, uint32_t timeout_ms)
{
    uint32_t start;

    if (m == NULL) {
        return DLT645_ERR_ARG;
    }
    if (m->port.now_ms == NULL) {
        return DLT645_ERR_UNSUPPORTED;
    }
    start = m->port.now_ms(m->port.user);
    while (dlt645_master_busy(m)) {
        uint32_t now = m->port.now_ms(m->port.user);
        dlt645_master_poll(m, now);
        if (timeout_ms != 0u && (uint32_t)(now - start) >= timeout_ms) {
            dlt645_master_abort(m);
            m->result = DLT645_ERR_TIMEOUT;
            return m->result;
        }
        if (m->port.delay_ms != NULL) {
            m->port.delay_ms(m->port.user, 1);
        }
    }
    return m->result;
}

/* ------------------------------------------------------------------ */
/* High level builders that start a transaction                        */
/* ------------------------------------------------------------------ */

dlt645_status_t dlt645_master_build_read(dlt645_master_t *m,
                                         const dlt645_addr_t *addr,
                                         uint32_t di)
{
    uint8_t body[4];
    uint8_t frame[DLT645_MAX_FRAME + DLT645_PREAMBLE_LEN];
    size_t len;
    dlt645_encode_opts_t opts;

    if (m == NULL || addr == NULL) {
        return DLT645_ERR_ARG;
    }
    dlt645_di_write(di, body);
    opts.add_preamble = m->cfg.add_preamble;
    if (dlt645_frame_encode(addr, DLT645_FUNC_READ_DATA, body, 4, &opts,
                            frame, sizeof(frame), &len) != DLT645_OK) {
        return DLT645_ERR_ARG;
    }
    return dlt645_master_start(m, frame, len, DLT645_FUNC_READ_DATA, di);
}

dlt645_status_t dlt645_master_build_read_follow(dlt645_master_t *m,
                                                const dlt645_addr_t *addr,
                                                uint32_t di, uint8_t seq)
{
    uint8_t frame[DLT645_MAX_FRAME + DLT645_PREAMBLE_LEN];
    size_t len;

    if (m == NULL || addr == NULL) {
        return DLT645_ERR_ARG;
    }
    len = dlt645_build_read_follow_frame(addr, di, seq, frame, sizeof(frame));
    if (len == 0u) {
        return DLT645_ERR_ARG;
    }
    return dlt645_master_start(m, frame, len, DLT645_FUNC_READ_FOLLOW, di);
}

dlt645_status_t dlt645_master_build_read_addr(dlt645_master_t *m,
                                              const dlt645_addr_t *target)
{
    uint8_t frame[DLT645_MAX_FRAME + DLT645_PREAMBLE_LEN];
    size_t len;

    if (m == NULL) {
        return DLT645_ERR_ARG;
    }
    len = dlt645_build_read_addr_frame(target, frame, sizeof(frame));
    if (len == 0u) {
        return DLT645_ERR_ARG;
    }
    /* Response address is always concrete and may differ from AA..AA. */
    {
        dlt645_status_t st = dlt645_master_start(m, frame, len,
                                                 DLT645_FUNC_READ_ADDR, 0);
        m->check_addr = 0;
        return st;
    }
}

dlt645_status_t dlt645_master_build_write(dlt645_master_t *m,
                                          const dlt645_addr_t *addr,
                                          uint32_t di,
                                          uint8_t pa, const uint8_t pwd[3],
                                          const uint8_t op[4],
                                          const uint8_t *data, size_t dlen)
{
    uint8_t frame[DLT645_MAX_FRAME + DLT645_PREAMBLE_LEN];
    size_t len;

    if (m == NULL || addr == NULL) {
        return DLT645_ERR_ARG;
    }
    len = dlt645_build_write_frame(addr, di, pa, pwd, op, data, dlen,
                                   frame, sizeof(frame));
    if (len == 0u) {
        return DLT645_ERR_ARG;
    }
    return dlt645_master_start(m, frame, len, DLT645_FUNC_WRITE_DATA, 0);
}

dlt645_status_t dlt645_master_build_write_addr(dlt645_master_t *m,
                                               const dlt645_addr_t *new_addr)
{
    uint8_t frame[DLT645_MAX_FRAME + DLT645_PREAMBLE_LEN];
    size_t len;

    if (m == NULL || new_addr == NULL) {
        return DLT645_ERR_ARG;
    }
    len = dlt645_build_write_addr_frame(new_addr, frame, sizeof(frame));
    if (len == 0u) {
        return DLT645_ERR_ARG;
    }
    return dlt645_master_start(m, frame, len, DLT645_FUNC_WRITE_ADDR, 0);
}

dlt645_status_t dlt645_master_build_broadcast_time(dlt645_master_t *m,
                                                   const dlt645_time_t *t)
{
    uint8_t frame[DLT645_MAX_FRAME + DLT645_PREAMBLE_LEN];
    size_t len;

    if (m == NULL || t == NULL) {
        return DLT645_ERR_ARG;
    }
    len = dlt645_build_broadcast_time_frame(t, frame, sizeof(frame));
    if (len == 0u) {
        return DLT645_ERR_ARG;
    }
    return dlt645_master_start(m, frame, len, DLT645_FUNC_BROADCAST_TIME, 0);
}

dlt645_status_t dlt645_master_build_freeze(dlt645_master_t *m,
                                           const dlt645_addr_t *addr,
                                           uint8_t month, uint8_t day,
                                           uint8_t hour, uint8_t minute,
                                           int broadcast)
{
    dlt645_addr_t a;
    uint8_t frame[DLT645_MAX_FRAME + DLT645_PREAMBLE_LEN];
    size_t len;

    if (m == NULL) {
        return DLT645_ERR_ARG;
    }
    if (broadcast) {
        dlt645_addr_broadcast(&a);
    } else if (addr != NULL) {
        a = *addr;
    } else {
        return DLT645_ERR_ARG;
    }
    len = dlt645_build_freeze_frame(&a, month, day, hour, minute,
                                    frame, sizeof(frame));
    if (len == 0u) {
        return DLT645_ERR_ARG;
    }
    return dlt645_master_start(m, frame, len, DLT645_FUNC_FREEZE, 0);
}

dlt645_status_t dlt645_master_build_change_baud(dlt645_master_t *m,
                                                const dlt645_addr_t *addr,
                                                uint8_t feature)
{
    uint8_t frame[DLT645_MAX_FRAME + DLT645_PREAMBLE_LEN];
    size_t len;

    if (m == NULL || addr == NULL) {
        return DLT645_ERR_ARG;
    }
    len = dlt645_build_change_baud_frame(addr, feature, frame, sizeof(frame));
    if (len == 0u) {
        return DLT645_ERR_ARG;
    }
    return dlt645_master_start(m, frame, len, DLT645_FUNC_CHANGE_BAUD, 0);
}

dlt645_status_t dlt645_master_build_change_pwd(dlt645_master_t *m,
                                               const dlt645_addr_t *addr,
                                               uint32_t di,
                                               uint8_t pa_old,
                                               const uint8_t pwd_old[3],
                                               uint8_t pa_new,
                                               const uint8_t pwd_new[3])
{
    uint8_t frame[DLT645_MAX_FRAME + DLT645_PREAMBLE_LEN];
    size_t len;

    if (m == NULL || addr == NULL) {
        return DLT645_ERR_ARG;
    }
    len = dlt645_build_change_pwd_frame(addr, di, pa_old, pwd_old,
                                        pa_new, pwd_new, frame, sizeof(frame));
    if (len == 0u) {
        return DLT645_ERR_ARG;
    }
    return dlt645_master_start(m, frame, len, DLT645_FUNC_CHANGE_PWD, 0);
}

dlt645_status_t dlt645_master_build_demand_reset(dlt645_master_t *m,
                                                 const dlt645_addr_t *addr,
                                                 uint8_t pa, const uint8_t pwd[3],
                                                 const uint8_t op[4])
{
    uint8_t frame[DLT645_MAX_FRAME + DLT645_PREAMBLE_LEN];
    size_t len;

    if (m == NULL || addr == NULL) {
        return DLT645_ERR_ARG;
    }
    len = dlt645_build_demand_reset_frame(addr, pa, pwd, op, frame, sizeof(frame));
    if (len == 0u) {
        return DLT645_ERR_ARG;
    }
    return dlt645_master_start(m, frame, len, DLT645_FUNC_DEMAND_RESET, 0);
}

dlt645_status_t dlt645_master_build_meter_clear(dlt645_master_t *m,
                                                const dlt645_addr_t *addr,
                                                uint8_t pa, const uint8_t pwd[3],
                                                const uint8_t op[4])
{
    uint8_t frame[DLT645_MAX_FRAME + DLT645_PREAMBLE_LEN];
    size_t len;

    if (m == NULL || addr == NULL) {
        return DLT645_ERR_ARG;
    }
    len = dlt645_build_meter_clear_frame(addr, pa, pwd, op, frame, sizeof(frame));
    if (len == 0u) {
        return DLT645_ERR_ARG;
    }
    return dlt645_master_start(m, frame, len, DLT645_FUNC_METER_CLEAR, 0);
}

dlt645_status_t dlt645_master_build_event_clear(dlt645_master_t *m,
                                                const dlt645_addr_t *addr,
                                                uint8_t pa, const uint8_t pwd[3],
                                                const uint8_t op[4],
                                                uint32_t di)
{
    uint8_t frame[DLT645_MAX_FRAME + DLT645_PREAMBLE_LEN];
    size_t len;

    if (m == NULL || addr == NULL) {
        return DLT645_ERR_ARG;
    }
    len = dlt645_build_event_clear_frame(addr, pa, pwd, op, di,
                                         frame, sizeof(frame));
    if (len == 0u) {
        return DLT645_ERR_ARG;
    }
    return dlt645_master_start(m, frame, len, DLT645_FUNC_EVENT_CLEAR, 0);
}

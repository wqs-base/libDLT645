/*
 * dlt645_slave.c - Slave (meter) request handling and response building.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dlt645_slave.h"
#include "dlt645_codec.h"

#include <string.h>

void dlt645_slave_init(dlt645_slave_t *s, const dlt645_port_t *port,
                       const dlt645_addr_t *addr,
                       const dlt645_slave_model_t *model)
{
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
    if (port != NULL) {
        s->port = *port;
    }
    if (model != NULL) {
        s->model = *model;
    }
    if (addr != NULL) {
        s->addr = *addr;
    }
    dlt645_rx_init(&s->rx);
}

void dlt645_slave_set_address(dlt645_slave_t *s, const dlt645_addr_t *addr)
{
    if (s != NULL && addr != NULL) {
        s->addr = *addr;
    }
}

void dlt645_slave_set_event_cb(dlt645_slave_t *s,
                               dlt645_slave_event_cb cb, void *user)
{
    if (s != NULL) {
        s->on_event = cb;
        s->event_user = user;
    }
}

/* ------------------------------------------------------------------ */
/* Response helpers                                                    */
/* ------------------------------------------------------------------ */

static size_t build_resp(const dlt645_addr_t *addr, uint8_t ctrl,
                         const uint8_t *data, uint8_t dlen,
                         uint8_t *out, size_t cap)
{
    dlt645_encode_opts_t opts;
    size_t len = 0;
    opts.add_preamble = 0;
    if (dlt645_frame_encode(addr, ctrl, data, dlen, &opts, out, cap, &len)
        != DLT645_OK) {
        return 0;
    }
    return len;
}

static size_t build_error(const dlt645_slave_t *s, const dlt645_frame_t *req,
                          uint8_t errbit, uint8_t *out, size_t cap)
{
    uint8_t ctrl = dlt645_ctrl_error_of(req->ctrl);
    return build_resp(&s->addr, ctrl, &errbit, 1, out, cap);
}

/* ------------------------------------------------------------------ */
/* Request dispatch                                                    */
/* ------------------------------------------------------------------ */

static size_t handle_read_data(dlt645_slave_t *s, const dlt645_frame_t *req,
                               const uint8_t *plain, size_t plen,
                               uint8_t *out, size_t cap)
{
    uint32_t di;
    const uint8_t *data = NULL;
    size_t len = 0;
    size_t chunk;
    uint8_t body[DLT645_MAX_DATA];
    const size_t max_chunk = DLT645_MAX_DATA - 4u;

    if (plen < 4u) {
        return build_error(s, req, DLT645_ERRBIT_OTHER, out, cap);
    }
    if (s->model.on_read == NULL) {
        return build_error(s, req, DLT645_ERRBIT_NO_DATA, out, cap);
    }
    di = dlt645_di_read(plain);
    if (s->model.on_read(s->model.user, di, &data, &len) != DLT645_OK ||
        data == NULL) {
        return build_error(s, req, DLT645_ERRBIT_NO_DATA, out, cap);
    }

    chunk = (len > max_chunk) ? max_chunk : len;
    dlt645_di_write(di, body);
    if (chunk > 0u) {
        memcpy(&body[4], data, chunk);
    }

    if (chunk < len) {
        s->follow_active = 1;
        s->follow_di = di;
        s->follow_pos = chunk;
        return build_resp(&s->addr, dlt645_ctrl_follow_of(0x91),
                          body, (uint8_t)(4u + chunk), out, cap);
    }
    s->follow_active = 0;
    return build_resp(&s->addr, 0x91, body, (uint8_t)(4u + chunk), out, cap);
}

static size_t handle_read_follow(dlt645_slave_t *s, const dlt645_frame_t *req,
                                 const uint8_t *plain, size_t plen,
                                 uint8_t *out, size_t cap)
{
    uint32_t di;
    uint8_t seq;
    const uint8_t *data = NULL;
    size_t len = 0;
    size_t start;
    size_t chunk;
    uint8_t body[DLT645_MAX_DATA];
    const size_t max_chunk = DLT645_MAX_DATA - 5u;
    int more;

    if (plen < 5u) {
        return build_error(s, req, DLT645_ERRBIT_OTHER, out, cap);
    }
    if (s->model.on_read == NULL) {
        return build_error(s, req, DLT645_ERRBIT_NO_DATA, out, cap);
    }
    di = dlt645_di_read(plain);
    seq = plain[4];
    if (s->model.on_read(s->model.user, di, &data, &len) != DLT645_OK ||
        data == NULL) {
        return build_error(s, req, DLT645_ERRBIT_NO_DATA, out, cap);
    }

    if (s->follow_active && s->follow_di == di) {
        start = s->follow_pos;
    } else {
        start = 0;
    }
    if (start > len) {
        start = len;
    }
    chunk = (len - start > max_chunk) ? max_chunk : (len - start);
    more = (start + chunk < len) ? 1 : 0;

    dlt645_di_write(di, body);
    if (chunk > 0u) {
        memcpy(&body[4], data + start, chunk);
    }
    body[4 + chunk] = seq;

    s->follow_active = more;
    s->follow_di = di;
    s->follow_pos = start + chunk;

    return build_resp(&s->addr, (uint8_t)(more ? 0xB2 : 0x92),
                      body, (uint8_t)(5u + chunk), out, cap);
}

static size_t handle_read_addr(dlt645_slave_t *s, const dlt645_frame_t *req,
                               uint8_t *out, size_t cap)
{
    (void)req;
    return build_resp(&s->addr, 0x93, s->addr.b, DLT645_ADDR_LEN, out, cap);
}

static size_t handle_write_data(dlt645_slave_t *s, const dlt645_frame_t *req,
                                const uint8_t *plain, size_t plen,
                                uint8_t *out, size_t cap)
{
    uint32_t di;
    uint8_t pa;
    const uint8_t *pwd;
    const uint8_t *op;
    const uint8_t *value;
    size_t vlen;

    if (plen < 12u) {
        return build_error(s, req, DLT645_ERRBIT_OTHER, out, cap);
    }
    di = dlt645_di_read(plain);
    pa = plain[4];
    pwd = &plain[5];
    op = &plain[8];
    value = &plain[12];
    vlen = plen - 12u;

    if (s->model.on_auth != NULL &&
        s->model.on_auth(s->model.user, pa, pwd, op) != DLT645_OK) {
        return build_error(s, req, DLT645_ERRBIT_PASSWORD, out, cap);
    }
    if (s->model.on_write == NULL ||
        s->model.on_write(s->model.user, di, value, vlen) != DLT645_OK) {
        return build_error(s, req, DLT645_ERRBIT_OTHER, out, cap);
    }
    return build_resp(&s->addr, 0x94, NULL, 0, out, cap);
}

static size_t handle_write_addr(dlt645_slave_t *s, const uint8_t *plain,
                                size_t plen, uint8_t *out, size_t cap)
{
    dlt645_addr_t new_addr;
    size_t len;

    if (plen != DLT645_ADDR_LEN) {
        return 0; /* read/write address errors are not answered */
    }
    memcpy(new_addr.b, plain, DLT645_ADDR_LEN);
    if (s->model.on_write_addr != NULL) {
        (void)s->model.on_write_addr(s->model.user, &new_addr);
    }
    /* Answer from the newly assigned address. */
    len = build_resp(&new_addr, 0x95, NULL, 0, out, cap);
    s->addr = new_addr;
    return len;
}

static size_t handle_freeze(dlt645_slave_t *s, const dlt645_frame_t *req,
                            const uint8_t *plain, size_t plen,
                            uint8_t *out, size_t cap)
{
    uint8_t minute, hour, day, month;
    int broadcast = dlt645_addr_is_broadcast(&req->addr);

    if (plen < 4u) {
        return broadcast ? 0u
                         : build_error(s, req, DLT645_ERRBIT_OTHER, out, cap);
    }
    minute = dlt645_bcd_to_bin(plain[0]);
    hour   = dlt645_bcd_to_bin(plain[1]);
    day    = dlt645_bcd_to_bin(plain[2]);
    month  = dlt645_bcd_to_bin(plain[3]);
    if (s->model.on_freeze != NULL) {
        (void)s->model.on_freeze(s->model.user, month, day, hour, minute);
    }
    if (broadcast) {
        return 0u;
    }
    return build_resp(&s->addr, 0x96, NULL, 0, out, cap);
}

static size_t handle_change_baud(dlt645_slave_t *s, const dlt645_frame_t *req,
                                 const uint8_t *plain, size_t plen,
                                 uint8_t *out, size_t cap)
{
    uint8_t feature;

    if (plen != 1u) {
        return build_error(s, req, DLT645_ERRBIT_OTHER, out, cap);
    }
    feature = plain[0];
    if (s->model.on_change_baud == NULL ||
        s->model.on_change_baud(s->model.user, feature) != DLT645_OK) {
        return build_error(s, req, DLT645_ERRBIT_BAUD, out, cap);
    }
    return build_resp(&s->addr, 0x97, &feature, 1, out, cap);
}

static size_t handle_change_pwd(dlt645_slave_t *s, const dlt645_frame_t *req,
                                const uint8_t *plain, size_t plen,
                                uint8_t *out, size_t cap)
{
    uint32_t di;
    uint8_t pa_old, pa_new;
    const uint8_t *pwd_old;
    const uint8_t *pwd_new;
    uint8_t body[4];
    static const uint8_t no_op[4] = {0, 0, 0, 0};

    if (plen != 12u) {
        return build_error(s, req, DLT645_ERRBIT_OTHER, out, cap);
    }
    di = dlt645_di_read(plain);
    pa_old = plain[4];
    pwd_old = &plain[5];
    pa_new = plain[8];
    pwd_new = &plain[9];

    if (s->model.on_auth != NULL &&
        s->model.on_auth(s->model.user, pa_old, pwd_old, no_op) != DLT645_OK) {
        return build_error(s, req, DLT645_ERRBIT_PASSWORD, out, cap);
    }
    if (s->model.on_change_pwd != NULL &&
        s->model.on_change_pwd(s->model.user, di, pa_new, pwd_new) != DLT645_OK) {
        return build_error(s, req, DLT645_ERRBIT_OTHER, out, cap);
    }
    body[0] = pa_new;
    memcpy(&body[1], pwd_new, 3);
    return build_resp(&s->addr, 0x98, body, 4, out, cap);
}

static size_t handle_clear(dlt645_slave_t *s, const dlt645_frame_t *req,
                           const uint8_t *plain, size_t plen,
                           uint8_t *out, size_t cap)
{
    uint8_t func = dlt645_ctrl_func(req->ctrl);
    size_t need;
    uint8_t resp_ctrl;
    uint8_t pa;
    const uint8_t *pwd;
    const uint8_t *op;
    uint32_t di = 0;

    switch (func) {
    case DLT645_FUNC_DEMAND_RESET: need = 8u;  resp_ctrl = 0x99; break;
    case DLT645_FUNC_METER_CLEAR:  need = 8u;  resp_ctrl = 0x9A; break;
    case DLT645_FUNC_EVENT_CLEAR:  need = 12u; resp_ctrl = 0x9B; break;
    default: return 0u;
    }
    if (plen != need) {
        return build_error(s, req, DLT645_ERRBIT_OTHER, out, cap);
    }
    pa = plain[0];
    pwd = &plain[1];
    op = &plain[4];
    if (func == DLT645_FUNC_EVENT_CLEAR) {
        di = dlt645_di_read(&plain[8]);
    }

    if (s->model.on_auth != NULL &&
        s->model.on_auth(s->model.user, pa, pwd, op) != DLT645_OK) {
        return build_error(s, req, DLT645_ERRBIT_PASSWORD, out, cap);
    }
    if (s->model.on_clear != NULL) {
        (void)s->model.on_clear(s->model.user, func, di);
    }
    return build_resp(&s->addr, resp_ctrl, NULL, 0, out, cap);
}

size_t dlt645_slave_handle_frame(dlt645_slave_t *s, const dlt645_frame_t *req,
                                 uint8_t *out, size_t cap)
{
    uint8_t plain[DLT645_MAX_DATA];
    size_t plen = 0;
    uint8_t func;
    size_t len = 0;
    int broadcast;

    if (s == NULL || req == NULL || out == NULL) {
        return 0u;
    }
    if (dlt645_ctrl_is_response(req->ctrl)) {
        return 0u; /* ignore frames emitted by masters/slaves */
    }
    if (dlt645_frame_data(req, plain, sizeof(plain), &plen) != DLT645_OK) {
        return 0u;
    }

    broadcast = dlt645_addr_is_broadcast(&req->addr);
    if (!broadcast && !dlt645_addr_match(&req->addr, &s->addr)) {
        return 0u; /* addressed to another meter */
    }

    func = dlt645_ctrl_func(req->ctrl);
    switch (func) {
    case DLT645_FUNC_BROADCAST_TIME:
        if (plen >= 6u && s->model.on_broadcast_time != NULL) {
            dlt645_time_t t;
            if (dlt645_time_from_broadcast(plain, &t) == DLT645_OK) {
                (void)s->model.on_broadcast_time(s->model.user, &t);
            }
        }
        return 0u; /* never answered */

    case DLT645_FUNC_READ_DATA:
        len = handle_read_data(s, req, plain, plen, out, cap);
        break;
    case DLT645_FUNC_READ_FOLLOW:
        len = handle_read_follow(s, req, plain, plen, out, cap);
        break;
    case DLT645_FUNC_READ_ADDR:
        len = handle_read_addr(s, req, out, cap);
        break;
    case DLT645_FUNC_WRITE_DATA:
        len = handle_write_data(s, req, plain, plen, out, cap);
        break;
    case DLT645_FUNC_WRITE_ADDR:
        len = handle_write_addr(s, plain, plen, out, cap);
        break;
    case DLT645_FUNC_FREEZE:
        len = handle_freeze(s, req, plain, plen, out, cap);
        break;
    case DLT645_FUNC_CHANGE_BAUD:
        len = handle_change_baud(s, req, plain, plen, out, cap);
        break;
    case DLT645_FUNC_CHANGE_PWD:
        len = handle_change_pwd(s, req, plain, plen, out, cap);
        break;
    case DLT645_FUNC_DEMAND_RESET:
    case DLT645_FUNC_METER_CLEAR:
    case DLT645_FUNC_EVENT_CLEAR:
        len = handle_clear(s, req, plain, plen, out, cap);
        break;
    default:
        len = build_error(s, req, DLT645_ERRBIT_OTHER, out, cap);
        break;
    }

    if (len > 0u && s->on_event != NULL) {
        s->on_event(s->event_user, req, out, len);
    }
    return len;
}

/* ------------------------------------------------------------------ */
/* Engine                                                              */
/* ------------------------------------------------------------------ */

void dlt645_slave_poll(dlt645_slave_t *s, uint32_t now_ms)
{
    uint8_t buf[32];

    (void)now_ms;
    if (s == NULL) {
        return;
    }

    /* Transmit any pending response first. */
    if (s->tx_len > 0u) {
        if (s->port.send == NULL) {
            s->tx_len = 0;
            return;
        }
        {
            int n = s->port.send(s->port.user, &s->tx[s->tx_pos],
                                 s->tx_len - s->tx_pos, 100u);
            if (n < 0) {
                s->tx_len = 0;
                s->tx_pos = 0;
                return;
            }
            s->tx_pos += (size_t)n;
        }
        if (s->tx_pos >= s->tx_len) {
            s->tx_len = 0;
            s->tx_pos = 0;
        }
        return;
    }

    if (s->port.recv == NULL) {
        return;
    }

    {
        int n;
        do {
            n = s->port.recv(s->port.user, buf, sizeof(buf), 0);
            if (n > 0) {
                int i;
                for (i = 0; i < n; i++) {
                    (void)dlt645_rx_feed(&s->rx, buf[i]);
                    if (dlt645_rx_frame_ready(&s->rx)) {
                        const dlt645_frame_t *req = dlt645_rx_frame(&s->rx);
                        s->tx_len = dlt645_slave_handle_frame(s, req, s->tx,
                                                              sizeof(s->tx));
                        s->tx_pos = 0;
                        dlt645_rx_reset(&s->rx);
                        if (s->tx_len > 0u) {
                            return; /* send the response on the next poll */
                        }
                    }
                }
            }
        } while (n > 0);
    }
}

dlt645_status_t dlt645_slave_run(dlt645_slave_t *s, uint32_t timeout_ms)
{
    uint32_t start;

    if (s == NULL) {
        return DLT645_ERR_ARG;
    }
    if (s->port.now_ms == NULL) {
        return DLT645_ERR_UNSUPPORTED;
    }
    start = s->port.now_ms(s->port.user);
    for (;;) {
        uint32_t now = s->port.now_ms(s->port.user);
        dlt645_slave_poll(s, now);
        if (timeout_ms != 0u && (uint32_t)(now - start) >= timeout_ms) {
            return DLT645_OK;
        }
        if (s->port.delay_ms != NULL) {
            s->port.delay_ms(s->port.user, 1);
        }
    }
}

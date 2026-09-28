/*
 * dlt645_frame.c - Frame assembly, parsing and streaming reception.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dlt645_frame.h"
#include "dlt645_codec.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Encoding                                                            */
/* ------------------------------------------------------------------ */

dlt645_status_t dlt645_frame_encode(const dlt645_addr_t *addr,
                                    uint8_t ctrl,
                                    const uint8_t *data,
                                    uint8_t data_len,
                                    const dlt645_encode_opts_t *opts,
                                    uint8_t *out, size_t cap,
                                    size_t *out_len)
{
    size_t pos = 0;
    size_t i;
    int preamble = (opts != NULL && opts->add_preamble) ? 1 : 0;

    if (addr == NULL || out == NULL || out_len == NULL) {
        return DLT645_ERR_ARG;
    }
    if (data_len > DLT645_MAX_DATA) {
        return DLT645_ERR_ARG;
    }
    if (data_len > 0u && data == NULL) {
        return DLT645_ERR_ARG;
    }
    if (cap < (size_t)(preamble ? DLT645_PREAMBLE_LEN : 0u) + 12u + data_len) {
        return DLT645_ERR_NOMEM;
    }

    if (preamble) {
        for (i = 0; i < DLT645_PREAMBLE_LEN; i++) {
            out[pos++] = DLT645_PREAMBLE_BYTE;
        }
    }

    out[pos++] = DLT645_FRAME_START;
    for (i = 0; i < DLT645_ADDR_LEN; i++) {
        out[pos++] = addr->b[i];
    }
    out[pos++] = DLT645_FRAME_START;
    out[pos++] = ctrl;
    out[pos++] = data_len;
    for (i = 0; i < data_len; i++) {
        out[pos++] = (uint8_t)(data[i] + DLT645_TRANSFORM);
    }

    out[pos] = dlt645_checksum(&out[preamble ? DLT645_PREAMBLE_LEN : 0u],
                               pos - (preamble ? DLT645_PREAMBLE_LEN : 0u));
    pos++;
    out[pos++] = DLT645_FRAME_END;

    *out_len = pos;
    return DLT645_OK;
}

dlt645_status_t dlt645_frame_encode_simple(const dlt645_addr_t *addr,
                                           uint8_t ctrl,
                                           const uint8_t *data,
                                           uint8_t data_len,
                                           uint8_t *out, size_t cap,
                                           size_t *out_len)
{
    return dlt645_frame_encode(addr, ctrl, data, data_len, NULL,
                               out, cap, out_len);
}

/* ------------------------------------------------------------------ */
/* Decoding                                                            */
/* ------------------------------------------------------------------ */

dlt645_status_t dlt645_frame_decode(const uint8_t *buf, size_t len,
                                    dlt645_frame_t *frame)
{
    uint8_t data_len;
    size_t expect;
    size_t i;
    uint8_t cs;

    if (buf == NULL || frame == NULL) {
        return DLT645_ERR_ARG;
    }
    if (len < 12u) {
        return DLT645_ERR_FORMAT;
    }
    if (buf[0] != DLT645_FRAME_START || buf[7] != DLT645_FRAME_START ||
        buf[len - 1u] != DLT645_FRAME_END) {
        return DLT645_ERR_FORMAT;
    }

    data_len = buf[9];
    if (data_len > DLT645_MAX_DATA) {
        return DLT645_ERR_FORMAT;
    }
    expect = 12u + (size_t)data_len;
    if (len != expect) {
        return DLT645_ERR_FORMAT;
    }

    cs = dlt645_checksum(buf, expect - 2u);
    if (cs != buf[expect - 2u]) {
        return DLT645_ERR_CHECKSUM;
    }

    memset(frame, 0, sizeof(*frame));
    memcpy(frame->addr.b, &buf[1], DLT645_ADDR_LEN);
    frame->ctrl = buf[8];
    frame->len = data_len;
    for (i = 0; i < data_len; i++) {
        frame->data[i] = buf[10u + i];
    }
    return DLT645_OK;
}

dlt645_status_t dlt645_frame_data(const dlt645_frame_t *frame,
                                  uint8_t *out, size_t cap, size_t *out_len)
{
    if (frame == NULL || out == NULL || out_len == NULL) {
        return DLT645_ERR_ARG;
    }
    if ((size_t)frame->len > cap) {
        return DLT645_ERR_NOMEM;
    }
    memcpy(out, frame->data, frame->len);
    dlt645_data_decode(out, frame->len);
    *out_len = frame->len;
    return DLT645_OK;
}

/* ------------------------------------------------------------------ */
/* Control code helpers                                                */
/* ------------------------------------------------------------------ */

int dlt645_ctrl_is_response(uint8_t ctrl)
{
    return (ctrl & DLT645_CTRL_DIR_MASK) ? 1 : 0;
}

int dlt645_ctrl_is_error(uint8_t ctrl)
{
    return (ctrl & DLT645_CTRL_ERR_MASK) ? 1 : 0;
}

int dlt645_ctrl_has_followup(uint8_t ctrl)
{
    return (ctrl & DLT645_CTRL_FOLLOW_MASK) ? 1 : 0;
}

uint8_t dlt645_ctrl_func(uint8_t ctrl)
{
    return (uint8_t)(ctrl & DLT645_CTRL_FUNC_MASK);
}

uint8_t dlt645_ctrl_response_of(uint8_t req)
{
    return (uint8_t)(req | DLT645_CTRL_DIR_MASK);
}

uint8_t dlt645_ctrl_error_of(uint8_t req)
{
    return (uint8_t)(req | DLT645_CTRL_DIR_MASK | DLT645_CTRL_ERR_MASK);
}

uint8_t dlt645_ctrl_follow_of(uint8_t resp)
{
    return (uint8_t)(resp | DLT645_CTRL_FOLLOW_MASK);
}

/* ------------------------------------------------------------------ */
/* Streaming receiver                                                  */
/* ------------------------------------------------------------------ */

void dlt645_rx_init(dlt645_rx_parser_t *p)
{
    if (p != NULL) {
        memset(p, 0, sizeof(*p));
        p->state = DLT645_RX_IDLE;
    }
}

void dlt645_rx_reset(dlt645_rx_parser_t *p)
{
    if (p != NULL) {
        p->state = DLT645_RX_IDLE;
        p->index = 0;
        p->cs = 0;
    }
}

int dlt645_rx_frame_ready(const dlt645_rx_parser_t *p)
{
    return (p != NULL && p->state == DLT645_RX_COMPLETE) ? 1 : 0;
}

const dlt645_frame_t *dlt645_rx_frame(const dlt645_rx_parser_t *p)
{
    return (p != NULL) ? &p->frame : NULL;
}

/*
 * Reset to IDLE.  If the byte that caused the abort is itself a frame
 * start, begin a new frame with it so a start byte is never lost.
 */
static dlt645_status_t rx_start_or_idle(dlt645_rx_parser_t *p, uint8_t byte)
{
    if (byte == DLT645_FRAME_START) {
        p->state = DLT645_RX_ADDR;
        p->index = 0;
        p->cs = DLT645_FRAME_START;
    } else {
        p->state = DLT645_RX_IDLE;
    }
    return DLT645_OK;
}

dlt645_status_t dlt645_rx_feed(dlt645_rx_parser_t *p, uint8_t byte)
{
    if (p == NULL) {
        return DLT645_ERR_ARG;
    }

    switch (p->state) {
    case DLT645_RX_IDLE:
        /* Preamble (0xFE) and other noise are ignored until 0x68. */
        if (byte == DLT645_FRAME_START) {
            p->state = DLT645_RX_ADDR;
            p->index = 0;
            p->cs = DLT645_FRAME_START;
        }
        return DLT645_OK;

    case DLT645_RX_ADDR:
        p->frame.addr.b[p->index++] = byte;
        p->cs = (uint8_t)(p->cs + byte);
        if (p->index == DLT645_ADDR_LEN) {
            p->state = DLT645_RX_START2;
        }
        return DLT645_OK;

    case DLT645_RX_START2:
        if (byte != DLT645_FRAME_START) {
            /* The frame start failed; recompute from scratch. */
            dlt645_rx_reset(p);
            return rx_start_or_idle(p, byte);
        }
        p->state = DLT645_RX_CTRL;
        p->cs = (uint8_t)(p->cs + DLT645_FRAME_START);
        return DLT645_OK;

    case DLT645_RX_CTRL:
        p->frame.ctrl = byte;
        p->cs = (uint8_t)(p->cs + byte);
        p->state = DLT645_RX_LEN;
        return DLT645_OK;

    case DLT645_RX_LEN:
        p->frame.len = byte;
        p->cs = (uint8_t)(p->cs + byte);
        if (p->frame.len > DLT645_MAX_DATA) {
            dlt645_rx_reset(p);
            return DLT645_ERR_FORMAT;
        }
        p->index = 0;
        p->state = (p->frame.len == 0u) ? DLT645_RX_CS : DLT645_RX_DATA;
        return DLT645_OK;

    case DLT645_RX_DATA:
        p->frame.data[p->index++] = byte;
        p->cs = (uint8_t)(p->cs + byte);
        if (p->index == p->frame.len) {
            p->state = DLT645_RX_CS;
        }
        return DLT645_OK;

    case DLT645_RX_CS:
        if (byte != p->cs) {
            dlt645_rx_reset(p);
            return rx_start_or_idle(p, byte);
        }
        p->state = DLT645_RX_END;
        return DLT645_OK;

    case DLT645_RX_END:
        if (byte != DLT645_FRAME_END) {
            dlt645_rx_reset(p);
            return rx_start_or_idle(p, byte);
        }
        p->state = DLT645_RX_COMPLETE;
        return DLT645_OK;

    case DLT645_RX_COMPLETE:
    default:
        /* A previous frame is still pending; drop it and restart. */
        dlt645_rx_reset(p);
        return dlt645_rx_feed(p, byte);
    }
}

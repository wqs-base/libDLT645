/*
 * dlt645_frame.h - Frame assembly, parsing and streaming reception.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DLT645_FRAME_H
#define DLT645_FRAME_H

#include "dlt645_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A decoded frame.  `data` holds the wire (already +0x33 transformed)
 * data field; use dlt645_frame_data() to obtain the plain form. */
typedef struct {
    dlt645_addr_t addr;
    uint8_t       ctrl;
    uint8_t       len;
    uint8_t       data[DLT645_MAX_DATA];
} dlt645_frame_t;

/* Options accepted by dlt645_frame_encode(). */
typedef struct {
    int add_preamble; /* prepend four 0xFE wake-up bytes */
} dlt645_encode_opts_t;

/*
 * Encode a frame from a plain (untransformed) data field.  The function
 * applies the +0x33 transformation and computes the checksum.  Returns
 * the number of bytes written via `*out_len`.
 */
dlt645_status_t dlt645_frame_encode(const dlt645_addr_t *addr,
                                    uint8_t ctrl,
                                    const uint8_t *data,
                                    uint8_t data_len,
                                    const dlt645_encode_opts_t *opts,
                                    uint8_t *out, size_t cap,
                                    size_t *out_len);

/* Convenience wrapper using default options (no preamble). */
dlt645_status_t dlt645_frame_encode_simple(const dlt645_addr_t *addr,
                                           uint8_t ctrl,
                                           const uint8_t *data,
                                           uint8_t data_len,
                                           uint8_t *out, size_t cap,
                                           size_t *out_len);

/*
 * Decode and validate a complete frame (must start at the first 0x68 and
 * end at 0x16, no preamble).  Returns DLT645_ERR_CHECKSUM / _FORMAT on a
 * malformed frame.
 */
dlt645_status_t dlt645_frame_decode(const uint8_t *buf, size_t len,
                                    dlt645_frame_t *frame);

/* Copy the untransformed (plain) data field of `frame` into `out`. */
dlt645_status_t dlt645_frame_data(const dlt645_frame_t *frame,
                                  uint8_t *out, size_t cap, size_t *out_len);

/* ------------------------------------------------------------------ */
/* Control code helpers                                                */
/* ------------------------------------------------------------------ */

int     dlt645_ctrl_is_response(uint8_t ctrl);
int     dlt645_ctrl_is_error(uint8_t ctrl);
int     dlt645_ctrl_has_followup(uint8_t ctrl);
uint8_t dlt645_ctrl_func(uint8_t ctrl);
uint8_t dlt645_ctrl_response_of(uint8_t req);
uint8_t dlt645_ctrl_error_of(uint8_t req);
uint8_t dlt645_ctrl_follow_of(uint8_t resp);

/* ------------------------------------------------------------------ */
/* Streaming receiver                                                  */
/* ------------------------------------------------------------------ */

typedef enum {
    DLT645_RX_IDLE = 0,
    DLT645_RX_ADDR,
    DLT645_RX_START2,
    DLT645_RX_CTRL,
    DLT645_RX_LEN,
    DLT645_RX_DATA,
    DLT645_RX_CS,
    DLT645_RX_END,
    DLT645_RX_COMPLETE
} dlt645_rx_state_t;

typedef struct {
    dlt645_rx_state_t state;
    dlt645_frame_t    frame;
    uint8_t           index;
    uint8_t           cs;
} dlt645_rx_parser_t;

void dlt645_rx_init(dlt645_rx_parser_t *p);
void dlt645_rx_reset(dlt645_rx_parser_t *p);

/*
 * Feed a single received byte.  Returns DLT645_OK while a frame is being
 * assembled and after a frame completes; returns DLT645_ERR_CHECKSUM or
 * DLT645_ERR_FORMAT when a corrupt frame was discarded (the parser then
 * continues looking for the next frame).
 */
dlt645_status_t dlt645_rx_feed(dlt645_rx_parser_t *p, uint8_t byte);

int dlt645_rx_frame_ready(const dlt645_rx_parser_t *p);
const dlt645_frame_t *dlt645_rx_frame(const dlt645_rx_parser_t *p);

#ifdef __cplusplus
}
#endif

#endif /* DLT645_FRAME_H */

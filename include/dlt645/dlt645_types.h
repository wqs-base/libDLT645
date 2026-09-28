/*
 * dlt645_types.h - Common types, constants and status codes for the
 *                  DL/T 645-2007 multi-function watt-hour meter
 *                  communication protocol library.
 *
 * SPDX-License-Identifier: MIT
 *
 * C99, no dynamic memory allocation.
 */
#ifndef DLT645_TYPES_H
#define DLT645_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Protocol constants                                                  */
/* ------------------------------------------------------------------ */

#define DLT645_FRAME_START      0x68u   /* frame start byte              */
#define DLT645_FRAME_END        0x16u   /* frame end byte                */
#define DLT645_PREAMBLE_BYTE    0xFEu   /* wake-up preamble byte         */
#define DLT645_PREAMBLE_LEN     4u      /* number of preamble bytes      */
#define DLT645_TRANSFORM        0x33u   /* data-field +/- offset         */

#define DLT645_ADDR_LEN         6u      /* address field bytes           */
#define DLT645_ADDR_DIGITS      12u     /* address decimal digits        */

/* Address wildcard / broadcast values (BCD, low byte first). */
#define DLT645_ADDR_WILDCARD    0xAAu   /* high-byte wildcard in缩位寻址  */
#define DLT645_ADDR_BROADCAST   0x99u   /* 999999999999H broadcast value */

/* Data field limits from clause 5.2.4. */
#define DLT645_MAX_DATA         200u    /* read data max L               */
#define DLT645_WRITE_MAX_DATA   50u     /* write data max L              */

/* Largest possible frame on the wire (start + addr + start + ctrl +
 * len + data + cs + end).  Preamble is handled separately. */
#define DLT645_MAX_FRAME        (1u + DLT645_ADDR_LEN + 1u + 1u + 1u + \
                                 DLT645_MAX_DATA + 1u + 1u)

/* Password and operator code sizes. */
#define DLT645_PASSWORD_LEN     3u
#define DLT645_OPCODE_LEN       4u

/* Control code bit masks (clause 5.2.3). */
#define DLT645_CTRL_DIR_MASK    0x80u   /* D7: 0 master, 1 slave          */
#define DLT645_CTRL_ERR_MASK    0x40u   /* D6: error response             */
#define DLT645_CTRL_FOLLOW_MASK 0x20u   /* D5: more data follows          */
#define DLT645_CTRL_FUNC_MASK   0x1Fu   /* D4..D0: function code          */

/* ------------------------------------------------------------------ */
/* Function codes (D4..D0 of the control byte)                         */
/* ------------------------------------------------------------------ */

typedef enum {
    DLT645_FUNC_RESERVED     = 0x00,
    DLT645_FUNC_BROADCAST_TIME = 0x08,
    DLT645_FUNC_READ_DATA    = 0x11,
    DLT645_FUNC_READ_FOLLOW  = 0x12,
    DLT645_FUNC_READ_ADDR    = 0x13,
    DLT645_FUNC_WRITE_DATA   = 0x14,
    DLT645_FUNC_WRITE_ADDR   = 0x15,
    DLT645_FUNC_FREEZE       = 0x16,
    DLT645_FUNC_CHANGE_BAUD  = 0x17,
    DLT645_FUNC_CHANGE_PWD   = 0x18,
    DLT645_FUNC_DEMAND_RESET = 0x19,
    DLT645_FUNC_METER_CLEAR  = 0x1A,
    DLT645_FUNC_EVENT_CLEAR  = 0x1B
} dlt645_func_t;

/* ------------------------------------------------------------------ */
/* Error information word bits (Appendix C, error information word)    */
/* ------------------------------------------------------------------ */

#define DLT645_ERRBIT_OTHER         0x01u /* 其他错误                     */
#define DLT645_ERRBIT_NO_DATA       0x02u /* 无请求数据                   */
#define DLT645_ERRBIT_PASSWORD      0x04u /* 密码错/未授权                */
#define DLT645_ERRBIT_BAUD          0x08u /* 通信速率不能更改             */
#define DLT645_ERRBIT_YEAR_ZONE     0x10u /* 年时区数超                   */
#define DLT645_ERRBIT_DAY_SEGMENT   0x20u /* 日时段数超                   */
#define DLT645_ERRBIT_RATE          0x40u /* 费率数超                     */

/* ------------------------------------------------------------------ */
/* Status codes                                                        */
/* ------------------------------------------------------------------ */

typedef enum {
    DLT645_OK               =  0,
    DLT645_ERR_ARG          = -1,  /* invalid argument                  */
    DLT645_ERR_NOMEM        = -2,  /* buffer too small                  */
    DLT645_ERR_IO           = -3,  /* port I/O failure                  */
    DLT645_ERR_TIMEOUT      = -4,  /* no (complete) response in time    */
    DLT645_ERR_CHECKSUM     = -5,  /* frame checksum mismatch           */
    DLT645_ERR_FORMAT       = -6,  /* malformed frame                   */
    DLT645_ERR_ADDR         = -7,  /* address did not match             */
    DLT645_ERR_FUNC         = -8,  /* unexpected function/control code  */
    DLT645_ERR_SLAVE        = -9,  /* slave reported an error (see ERR) */
    DLT645_ERR_STATE        = -10, /* operation errors                  */
    DLT645_ERR_UNSUPPORTED  = -11, /* feature not compiled in           */
    DLT645_ERR_SEQ          = -12  /* frame sequence number mismatch    */
} dlt645_status_t;

/* Human readable message for a status code. */
const char *dlt645_strerror(dlt645_status_t status);

/* ------------------------------------------------------------------ */
/* Address helper type                                                 */
/* ------------------------------------------------------------------ */

/*
 * A communication address is 6 BCD bytes transmitted low byte first.
 * addr[0] holds the two least-significant decimal digits.
 */
typedef struct {
    uint8_t b[DLT645_ADDR_LEN];
} dlt645_addr_t;

/* ------------------------------------------------------------------ */
/* Date / time                                                         */
/* ------------------------------------------------------------------ */

/*
 * Calendar time used by broadcast time synchronisation and the date /
 * time data items.  All fields are plain binary values in their natural
 * range; the codec converts to / from packed BCD as required.
 */
typedef struct {
    uint8_t  year;   /* 0..99  (20xx)     */
    uint8_t  month;  /* 1..12             */
    uint8_t  day;    /* 1..31             */
    uint8_t  hour;   /* 0..23             */
    uint8_t  minute; /* 0..59             */
    uint8_t  second; /* 0..59             */
} dlt645_time_t;

#ifdef __cplusplus
}
#endif

#endif /* DLT645_TYPES_H */

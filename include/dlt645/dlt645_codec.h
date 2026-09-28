/*
 * dlt645_codec.h - BCD / address / time / data-item encoding helpers.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DLT645_CODEC_H
#define DLT645_CODEC_H

#include "dlt645_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Single byte BCD                                                     */
/* ------------------------------------------------------------------ */

/* Convert a packed BCD byte (0x00..0x99) to binary. Returns 0xFF when
 * the byte contains a non-BCD nibble. */
uint8_t dlt645_bcd_to_bin(uint8_t bcd);

/* Convert binary 0..99 to packed BCD (0x00..0x99). Values > 99 are
 * clamped to 0xFF. */
uint8_t dlt645_bin_to_bcd(uint8_t bin);

/* ------------------------------------------------------------------ */
/* Communication address                                               */
/* ------------------------------------------------------------------ */

/*
 * Parse a 12 digit decimal address string (e.g. "123456789012") into
 * the wire representation (low byte first, two BCD digits per byte).
 * Shorter strings are right-aligned and left padded with '0'.  The
 * wildcard character 'A'/'a' maps to 0xAA.
 *
 * Returns DLT645_OK, DLT645_ERR_ARG or DLT645_ERR_FORMAT.
 */
dlt645_status_t dlt645_addr_parse(const char *text, dlt645_addr_t *addr);

/*
 * Format an address into `out` (must hold at least 13 bytes).  Byte A5
 * is printed first so the result reads as the 12 decimal digits.
 */
void dlt645_addr_format(const dlt645_addr_t *addr, char *out);

/* Build the broadcast address (all 0x99 bytes). */
void dlt645_addr_broadcast(dlt645_addr_t *addr);

/* Build the point-to-point wildcard address (all 0xAA bytes). */
void dlt645_addr_wildcard(dlt645_addr_t *addr);

/* True when addr is the 999999999999H broadcast address. */
int dlt645_addr_is_broadcast(const dlt645_addr_t *addr);

/* True when every byte is 0xAA. */
int dlt645_addr_is_wildcard(const dlt645_addr_t *addr);

/*
 * Match a frame address against a local address, honouring缩位寻址
 * wildcards (0xAA high bytes).  `pattern` may contain 0xAA bytes in the
 * upper positions which match any value; `local` is the concrete
 * address.  Returns non-zero on match.
 */
int dlt645_addr_match(const dlt645_addr_t *pattern,
                      const dlt645_addr_t *local);

/* ------------------------------------------------------------------ */
/* Data identifier                                                     */
/* ------------------------------------------------------------------ */

/*
 * A data identifier is four bytes DI3 DI2 DI1 DI0.  It is stored in a
 * uint32_t as DI3<<24 | DI2<<16 | DI1<<8 | DI0, i.e. reading the number
 * as hexadecimal yields the identifier as printed in Appendix A.  On
 * the wire the bytes are transmitted DI0 first (little endian).
 */
uint32_t dlt645_di_make(uint8_t di3, uint8_t di2, uint8_t di1, uint8_t di0);

/* Write DI0..DI3 (wire order, little endian) into a 4 byte buffer. */
void dlt645_di_write(uint32_t di, uint8_t out[4]);

/* Read DI0..DI3 (wire order) into the canonical uint32_t form. */
uint32_t dlt645_di_read(const uint8_t in[4]);

/* ------------------------------------------------------------------ */
/* Packed BCD data items                                               */
/* ------------------------------------------------------------------ */

/*
 * Decode a packed BCD data item transmitted low byte first.
 *
 * `nbytes`   number of bytes in `data`.
 * `decimals` number of implied decimal places (0..nibbles-1).
 * `out`      receives the unscaled integer value (already scaled by
 *            10^decimals, sign applied).
 * `negative` optional, receives 1 when the value is negative.
 *
 * The sign is carried in bit 7 of the most significant byte (nibble
 * bit 3).  Returns DLT645_OK or DLT645_ERR_FORMAT.
 */
dlt645_status_t dlt645_bcd_decode(const uint8_t *data, uint8_t nbytes,
                                  uint8_t decimals, int64_t *out,
                                  int *negative);

/*
 * Encode an integer value (already scaled by 10^decimals) into a packed
 * BCD data item of `nbytes` bytes, low byte first.  A negative value
 * sets the sign bit of the most significant byte.
 */
dlt645_status_t dlt645_bcd_encode(int64_t value, uint8_t decimals,
                                  uint8_t *out, uint8_t nbytes);

/*
 * Format a packed BCD data item as a decimal string, e.g. "123456.78"
 * or "-0012.5".  `out` must hold at least nbytes*2 + 2 bytes.
 */
dlt645_status_t dlt645_bcd_format(const uint8_t *data, uint8_t nbytes,
                                  uint8_t decimals, char *out, size_t cap);

/* ------------------------------------------------------------------ */
/* Date / time                                                         */
/* ------------------------------------------------------------------ */

/*
 * Broadcast time synchronisation data field layout (6 bytes):
 *   ss mm hh DD MM YY   (transmitted lowest field first)
 */
dlt645_status_t dlt645_time_to_broadcast(const dlt645_time_t *t,
                                         uint8_t out[6]);
dlt645_status_t dlt645_time_from_broadcast(const uint8_t in[6],
                                           dlt645_time_t *t);

/*
 * Generic packed BCD calendar encode/decode for the "日期及时间"
 * variable data items.  The 5 byte form is YY MM DD hh mm and the 6
 * byte form is YY MM DD hh mm ss (wire order reversed: ss mm hh DD MM
 * [YY]).
 */
dlt645_status_t dlt645_time_to_bcd6(const dlt645_time_t *t, uint8_t out[6]);
dlt645_status_t dlt645_time_from_bcd6(const uint8_t in[6], dlt645_time_t *t);

/* ------------------------------------------------------------------ */
/* Misc helpers                                                        */
/* ------------------------------------------------------------------ */

/* Sum of buf[0..len) modulo 256 (frame checksum input). */
uint8_t dlt645_checksum(const uint8_t *buf, size_t len);

/* Apply / remove the 0x33 data-field transformation in place. */
void dlt645_data_encode(uint8_t *data, size_t len);
void dlt645_data_decode(uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* DLT645_CODEC_H */

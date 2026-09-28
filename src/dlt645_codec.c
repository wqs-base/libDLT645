/*
 * dlt645_codec.c - BCD / address / time / data-item encoding helpers.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dlt645_codec.h"

#include <string.h>

/* ------------------------------------------------------------------ */

const char *dlt645_strerror(dlt645_status_t status)
{
    switch (status) {
    case DLT645_OK:              return "ok";
    case DLT645_ERR_ARG:         return "invalid argument";
    case DLT645_ERR_NOMEM:       return "buffer too small";
    case DLT645_ERR_IO:          return "port I/O failure";
    case DLT645_ERR_TIMEOUT:     return "timeout";
    case DLT645_ERR_CHECKSUM:    return "checksum mismatch";
    case DLT645_ERR_FORMAT:      return "malformed frame";
    case DLT645_ERR_ADDR:        return "address mismatch";
    case DLT645_ERR_FUNC:        return "unexpected control code";
    case DLT645_ERR_SLAVE:       return "slave error response";
    case DLT645_ERR_STATE:       return "invalid state";
    case DLT645_ERR_UNSUPPORTED: return "unsupported";
    case DLT645_ERR_SEQ:         return "sequence mismatch";
    default:                     return "unknown error";
    }
}

/* ------------------------------------------------------------------ */
/* Single byte BCD                                                     */
/* ------------------------------------------------------------------ */

uint8_t dlt645_bcd_to_bin(uint8_t bcd)
{
    uint8_t hi = (uint8_t)(bcd >> 4);
    uint8_t lo = (uint8_t)(bcd & 0x0Fu);
    if (hi > 9u || lo > 9u) {
        return 0xFFu;
    }
    return (uint8_t)(hi * 10u + lo);
}

uint8_t dlt645_bin_to_bcd(uint8_t bin)
{
    if (bin > 99u) {
        return 0xFFu;
    }
    return (uint8_t)(((bin / 10u) << 4) | (bin % 10u));
}

/* ------------------------------------------------------------------ */
/* Communication address                                               */
/* ------------------------------------------------------------------ */

dlt645_status_t dlt645_addr_parse(const char *text, dlt645_addr_t *addr)
{
    size_t len;
    size_t i;

    if (text == NULL || addr == NULL) {
        return DLT645_ERR_ARG;
    }

    len = strlen(text);
    if (len > DLT645_ADDR_DIGITS) {
        return DLT645_ERR_FORMAT;
    }

    /* Right align into a 12 character scratch buffer, padded with '0'. */
    char norm[DLT645_ADDR_DIGITS];
    for (i = 0; i < DLT645_ADDR_DIGITS; i++) {
        norm[i] = '0';
    }
    for (i = 0; i < len; i++) {
        norm[DLT645_ADDR_DIGITS - len + i] = text[i];
    }

    /* Low byte first: A0 takes the two least significant digits. */
    for (i = 0; i < DLT645_ADDR_LEN; i++) {
        size_t idx = DLT645_ADDR_DIGITS - 2u * (i + 1u);
        char c1 = norm[idx];
        char c2 = norm[idx + 1];
        if ((c1 == 'A' || c1 == 'a') && (c2 == 'A' || c2 == 'a')) {
            addr->b[i] = DLT645_ADDR_WILDCARD;
        } else if (c1 >= '0' && c1 <= '9' && c2 >= '0' && c2 <= '9') {
            addr->b[i] = (uint8_t)(((c1 - '0') << 4) | (c2 - '0'));
        } else {
            return DLT645_ERR_FORMAT;
        }
    }
    return DLT645_OK;
}

void dlt645_addr_format(const dlt645_addr_t *addr, char *out)
{
    int i;
    if (addr == NULL || out == NULL) {
        return;
    }
    for (i = 0; i < (int)DLT645_ADDR_LEN; i++) {
        uint8_t byte = addr->b[DLT645_ADDR_LEN - 1u - (unsigned)i];
        if (byte == DLT645_ADDR_WILDCARD) {
            out[i * 2] = 'A';
            out[i * 2 + 1] = 'A';
        } else {
            out[i * 2]     = (char)('0' + ((byte >> 4) & 0x0Fu));
            out[i * 2 + 1] = (char)('0' + (byte & 0x0Fu));
        }
    }
    out[DLT645_ADDR_DIGITS] = '\0';
}

void dlt645_addr_broadcast(dlt645_addr_t *addr)
{
    if (addr != NULL) {
        memset(addr->b, DLT645_ADDR_BROADCAST, DLT645_ADDR_LEN);
    }
}

void dlt645_addr_wildcard(dlt645_addr_t *addr)
{
    if (addr != NULL) {
        memset(addr->b, DLT645_ADDR_WILDCARD, DLT645_ADDR_LEN);
    }
}

int dlt645_addr_is_broadcast(const dlt645_addr_t *addr)
{
    int i;
    if (addr == NULL) {
        return 0;
    }
    for (i = 0; i < (int)DLT645_ADDR_LEN; i++) {
        if (addr->b[i] != DLT645_ADDR_BROADCAST) {
            return 0;
        }
    }
    return 1;
}

int dlt645_addr_is_wildcard(const dlt645_addr_t *addr)
{
    int i;
    if (addr == NULL) {
        return 0;
    }
    for (i = 0; i < (int)DLT645_ADDR_LEN; i++) {
        if (addr->b[i] != DLT645_ADDR_WILDCARD) {
            return 0;
        }
    }
    return 1;
}

int dlt645_addr_match(const dlt645_addr_t *pattern,
                      const dlt645_addr_t *local)
{
    int i;
    int seen_concrete = 0;
    if (pattern == NULL || local == NULL) {
        return 0;
    }
    if (dlt645_addr_is_broadcast(pattern)) {
        return 1;
    }
    /* 缩位寻址: 0xAA is only valid for the high bytes; scanning from the
     * most significant byte down, once a concrete byte is seen every
     * lower byte must be concrete too. */
    for (i = (int)DLT645_ADDR_LEN - 1; i >= 0; i--) {
        uint8_t p = pattern->b[i];
        if (p == DLT645_ADDR_WILDCARD) {
            if (seen_concrete) {
                return 0;
            }
            continue;
        }
        seen_concrete = 1;
        if (p != local->b[i]) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Data identifier                                                     */
/* ------------------------------------------------------------------ */

uint32_t dlt645_di_make(uint8_t di3, uint8_t di2, uint8_t di1, uint8_t di0)
{
    return ((uint32_t)di3 << 24) | ((uint32_t)di2 << 16) |
           ((uint32_t)di1 << 8) | (uint32_t)di0;
}

void dlt645_di_write(uint32_t di, uint8_t out[4])
{
    out[0] = (uint8_t)(di & 0xFFu);
    out[1] = (uint8_t)((di >> 8) & 0xFFu);
    out[2] = (uint8_t)((di >> 16) & 0xFFu);
    out[3] = (uint8_t)((di >> 24) & 0xFFu);
}

uint32_t dlt645_di_read(const uint8_t in[4])
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

/* ------------------------------------------------------------------ */
/* Packed BCD data items                                               */
/* ------------------------------------------------------------------ */

dlt645_status_t dlt645_bcd_decode(const uint8_t *data, uint8_t nbytes,
                                  uint8_t decimals, int64_t *out,
                                  int *negative)
{
    int64_t value = 0;
    int64_t scale = 1;
    int sign;
    unsigned i;

    (void)decimals;

    if (data == NULL || out == NULL || nbytes == 0u) {
        return DLT645_ERR_ARG;
    }

    sign = (data[nbytes - 1u] & 0x80u) ? 1 : 0;

    for (i = 0; i < nbytes; i++) {
        uint8_t lo = (uint8_t)(data[i] & 0x0Fu);
        uint8_t hi = (uint8_t)((data[i] >> 4) & 0x0Fu);
        if (i == (unsigned)(nbytes - 1u) && sign) {
            hi &= 0x07u;
        }
        if (lo > 9u || hi > 9u) {
            return DLT645_ERR_FORMAT;
        }
        value += (int64_t)lo * scale;
        scale *= 10;
        value += (int64_t)hi * scale;
        scale *= 10;
    }

    *out = value;
    if (negative != NULL) {
        *negative = sign;
    }
    return DLT645_OK;
}

dlt645_status_t dlt645_bcd_encode(int64_t value, uint8_t decimals,
                                  uint8_t *out, uint8_t nbytes)
{
    int negative = 0;
    uint64_t mag;
    unsigned i;

    (void)decimals;

    if (out == NULL || nbytes == 0u) {
        return DLT645_ERR_ARG;
    }
    if (value < 0) {
        negative = 1;
        mag = (uint64_t)(-value);
    } else {
        mag = (uint64_t)value;
    }

    memset(out, 0, nbytes);
    for (i = 0; i < nbytes; i++) {
        uint8_t lo = (uint8_t)(mag % 10u);
        mag /= 10u;
        uint8_t hi = (uint8_t)(mag % 10u);
        mag /= 10u;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    if (mag != 0u) {
        return DLT645_ERR_FORMAT; /* does not fit */
    }
    if (negative) {
        out[nbytes - 1u] |= 0x80u;
    }
    return DLT645_OK;
}

dlt645_status_t dlt645_bcd_format(const uint8_t *data, uint8_t nbytes,
                                  uint8_t decimals, char *out, size_t cap)
{
    int64_t value;
    int negative = 0;
    char digits[DLT645_MAX_DATA * 2u + 1u];
    size_t ndigits = (size_t)nbytes * 2u;
    size_t pos = 0;
    dlt645_status_t st;
    size_t i;

    if (data == NULL || out == NULL || nbytes == 0u) {
        return DLT645_ERR_ARG;
    }
    if (decimals > ndigits) {
        return DLT645_ERR_ARG;
    }

    st = dlt645_bcd_decode(data, nbytes, decimals, &value, &negative);
    if (st != DLT645_OK) {
        return st;
    }

    /* Render the BCD digits directly (avoids integer width limits).
     * Build least-significant first, then reverse to the natural
     * most-significant-first reading order. */
    for (i = 0; i < ndigits; i++) {
        uint8_t nib = (uint8_t)(data[i / 2u] >> ((i % 2u) ? 4u : 0u)) & 0x0Fu;
        if (negative && i == ndigits - 1u) {
            nib &= 0x07u; /* the sign lives in bit 3 of the top nibble */
        }
        digits[ndigits - 1u - i] = (char)('0' + nib);
    }
    digits[ndigits] = '\0';

    if (negative) {
        if (pos + 1u >= cap) {
            return DLT645_ERR_NOMEM;
        }
        out[pos++] = '-';
    }

    /* Strip leading zeros but keep at least one integer digit. */
    {
        size_t int_len = ndigits - decimals;
        size_t start = 0;
        while (start + 1u < int_len && digits[start] == '0') {
            start++;
        }
        for (i = start; i < ndigits; i++) {
            if (decimals > 0u && i == int_len) {
                if (pos + 1u >= cap) {
                    return DLT645_ERR_NOMEM;
                }
                out[pos++] = '.';
            }
            if (pos + 1u >= cap) {
                return DLT645_ERR_NOMEM;
            }
            out[pos++] = digits[i];
        }
    }

    out[pos] = '\0';
    return DLT645_OK;
}

/* ------------------------------------------------------------------ */
/* Date / time                                                         */
/* ------------------------------------------------------------------ */

dlt645_status_t dlt645_time_to_broadcast(const dlt645_time_t *t,
                                         uint8_t out[6])
{
    if (t == NULL || out == NULL) {
        return DLT645_ERR_ARG;
    }
    if (t->year > 99u || t->month < 1u || t->month > 12u ||
        t->day < 1u || t->day > 31u || t->hour > 23u ||
        t->minute > 59u || t->second > 59u) {
        return DLT645_ERR_ARG;
    }
    out[0] = dlt645_bin_to_bcd(t->second);
    out[1] = dlt645_bin_to_bcd(t->minute);
    out[2] = dlt645_bin_to_bcd(t->hour);
    out[3] = dlt645_bin_to_bcd(t->day);
    out[4] = dlt645_bin_to_bcd(t->month);
    out[5] = dlt645_bin_to_bcd(t->year);
    return DLT645_OK;
}

dlt645_status_t dlt645_time_from_broadcast(const uint8_t in[6],
                                           dlt645_time_t *t)
{
    if (in == NULL || t == NULL) {
        return DLT645_ERR_ARG;
    }
    t->second = dlt645_bcd_to_bin(in[0]);
    t->minute = dlt645_bcd_to_bin(in[1]);
    t->hour   = dlt645_bcd_to_bin(in[2]);
    t->day    = dlt645_bcd_to_bin(in[3]);
    t->month  = dlt645_bcd_to_bin(in[4]);
    t->year   = dlt645_bcd_to_bin(in[5]);
    if (t->second > 59u || t->minute > 59u || t->hour > 23u ||
        t->day < 1u || t->day > 31u || t->month < 1u || t->month > 12u ||
        t->year > 99u) {
        return DLT645_ERR_FORMAT;
    }
    return DLT645_OK;
}

dlt645_status_t dlt645_time_to_bcd6(const dlt645_time_t *t, uint8_t out[6])
{
    return dlt645_time_to_broadcast(t, out);
}

dlt645_status_t dlt645_time_from_bcd6(const uint8_t in[6], dlt645_time_t *t)
{
    return dlt645_time_from_broadcast(in, t);
}

/* ------------------------------------------------------------------ */
/* Misc helpers                                                        */
/* ------------------------------------------------------------------ */

uint8_t dlt645_checksum(const uint8_t *buf, size_t len)
{
    uint8_t sum = 0;
    size_t i;
    if (buf == NULL) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        sum = (uint8_t)(sum + buf[i]);
    }
    return sum;
}

void dlt645_data_encode(uint8_t *data, size_t len)
{
    size_t i;
    if (data == NULL) {
        return;
    }
    for (i = 0; i < len; i++) {
        data[i] = (uint8_t)(data[i] + DLT645_TRANSFORM);
    }
}

void dlt645_data_decode(uint8_t *data, size_t len)
{
    size_t i;
    if (data == NULL) {
        return;
    }
    for (i = 0; i < len; i++) {
        data[i] = (uint8_t)(data[i] - DLT645_TRANSFORM);
    }
}

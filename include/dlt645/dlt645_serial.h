/*
 * dlt645_serial.h - Optional cross-platform serial port (HAL) adapter.
 *
 * This is a convenience reference implementation of dlt645_port_t for
 * POSIX (Linux/macOS) and Windows.  The core library does not depend on
 * it; projects with their own UART driver should ignore it.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DLT645_SERIAL_H
#define DLT645_SERIAL_H

#include "dlt645_port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    intptr_t handle;   /* fd (POSIX) or HANDLE (Windows)  */
    uint32_t start_ms; /* tick at open, for now_ms()      */
    int      is_open;
} dlt645_serial_t;

/*
 * Open `device` (e.g. "COM3" or "/dev/ttyUSB0") at `baud` (2400 by
 * default) using the DL/T 645 line settings: 8 data bits, even parity,
 * 1 stop bit.  Returns DLT645_OK or an error.
 */
dlt645_status_t dlt645_serial_open(dlt645_serial_t *s, const char *device,
                                   uint32_t baud);

void dlt645_serial_close(dlt645_serial_t *s);

/* Fill in a dlt645_port_t backed by this serial handle. */
void dlt645_serial_port(dlt645_serial_t *s, dlt645_port_t *port);

#ifdef __cplusplus
}
#endif

#endif /* DLT645_SERIAL_H */

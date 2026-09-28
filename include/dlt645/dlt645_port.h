/*
 * dlt645_port.h - Platform abstraction (HAL) for the DL/T 645 library.
 *
 * The core library never touches a UART, socket or operating system.
 * Instead the application provides a dlt645_port_t through which all
 * byte I/O and timing is performed.  This keeps the library portable
 * across bare-metal MCUs, RTOS tasks and desktop operating systems.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DLT645_PORT_H
#define DLT645_PORT_H

#include "dlt645_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Port callbacks.  All callbacks receive the opaque `user` pointer.
 *
 * send()      Transmit exactly `len` bytes.  May block for up to
 *             `timeout_ms`.  Returns the number of bytes written
 *             (== len on success) or a negative value on error.
 *
 * recv()      Non-blocking receive.  Copy up to `len` available bytes
 *             into `data` and return the number copied.  Returns 0 when
 *             no bytes are currently available and a negative value on
 *             error.  `timeout_ms` is a lower bound hint for blocking
 *             implementations and may be ignored by non-blocking ones.
 *
 * now_ms()    Monotonic millisecond counter.  Required by the
 *             non-blocking engines.
 *
 * delay_ms()  Sleep for `timeout_ms` milliseconds.  Required by the
 *             blocking convenience helpers.
 *
 * Any callback may be NULL when the corresponding feature is unused.
 */
typedef struct dlt645_port {
    void *user;

    int      (*send)(void *user, const uint8_t *data, size_t len,
                     uint32_t timeout_ms);
    int      (*recv)(void *user, uint8_t *data, size_t len,
                     uint32_t timeout_ms);
    uint32_t (*now_ms)(void *user);
    void     (*delay_ms)(void *user, uint32_t ms);
} dlt645_port_t;

#ifdef __cplusplus
}
#endif

#endif /* DLT645_PORT_H */

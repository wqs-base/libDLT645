/*
 * dlt645_serial.c - POSIX / Windows serial port adapter.
 *
 * Optional reference implementation of dlt645_port_t.  Line settings
 * follow DL/T 645: 8 data bits, even parity, 1 stop bit (8E1).
 *
 * SPDX-License-Identifier: MIT
 */
#include "dlt645_serial.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)

#include <windows.h>

dlt645_status_t dlt645_serial_open(dlt645_serial_t *s, const char *device,
                                   uint32_t baud)
{
    char path[64];
    HANDLE h;
    DCB dcb;
    COMMTIMEOUTS to;

    if (s == NULL || device == NULL) {
        return DLT645_ERR_ARG;
    }
    memset(s, 0, sizeof(*s));

    if (strncmp(device, "\\\\.\\", 4) == 0) {
        (void)snprintf(path, sizeof(path), "%s", device);
    } else {
        (void)snprintf(path, sizeof(path), "\\\\.\\%s", device);
    }

    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return DLT645_ERR_IO;
    }

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        return DLT645_ERR_IO;
    }
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = EVENPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = TRUE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        return DLT645_ERR_IO;
    }

    /* Non-blocking reads: return whatever is available immediately. */
    to.ReadIntervalTimeout = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant = 0;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 1000;
    (void)SetCommTimeouts(h, &to);
    (void)SetupComm(h, 4096, 4096);
    (void)PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    s->handle = (intptr_t)h;
    s->start_ms = (uint32_t)GetTickCount64();
    s->is_open = 1;
    return DLT645_OK;
}

void dlt645_serial_close(dlt645_serial_t *s)
{
    if (s != NULL && s->is_open) {
        CloseHandle((HANDLE)s->handle);
        s->is_open = 0;
    }
}

static int serial_send(void *user, const uint8_t *data, size_t len,
                       uint32_t timeout_ms)
{
    dlt645_serial_t *s = (dlt645_serial_t *)user;
    DWORD written = 0;
    (void)timeout_ms;
    if (s == NULL || !s->is_open) {
        return -1;
    }
    if (!WriteFile((HANDLE)s->handle, data, (DWORD)len, &written, NULL)) {
        return -1;
    }
    return (int)written;
}

static int serial_recv(void *user, uint8_t *data, size_t len,
                       uint32_t timeout_ms)
{
    dlt645_serial_t *s = (dlt645_serial_t *)user;
    DWORD got = 0;
    (void)timeout_ms;
    if (s == NULL || !s->is_open) {
        return -1;
    }
    if (!ReadFile((HANDLE)s->handle, data, (DWORD)len, &got, NULL)) {
        return -1;
    }
    return (int)got;
}

static uint32_t serial_now_ms(void *user)
{
    (void)user;
    return (uint32_t)GetTickCount64();
}

static void serial_delay_ms(void *user, uint32_t ms)
{
    (void)user;
    Sleep(ms);
}

#else /* POSIX */

#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>

static speed_t posix_baud(uint32_t baud)
{
    switch (baud) {
    case 600:    return B600;
    case 1200:   return B1200;
    case 2400:   return B2400;
    case 4800:   return B4800;
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    default:     return B2400;
    }
}

dlt645_status_t dlt645_serial_open(dlt645_serial_t *s, const char *device,
                                   uint32_t baud)
{
    int fd;
    struct termios tio;

    if (s == NULL || device == NULL) {
        return DLT645_ERR_ARG;
    }
    memset(s, 0, sizeof(*s));

    fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return DLT645_ERR_IO;
    }
    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return DLT645_ERR_IO;
    }

    cfmakeraw(&tio);
    cfsetispeed(&tio, posix_baud(baud));
    cfsetospeed(&tio, posix_baud(baud));
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cflag |= PARENB;   /* even parity */
    tio.c_cflag &= ~PARODD;
    tio.c_cflag &= ~CSTOPB;  /* one stop bit */
    tio.c_cflag &= ~CRTSCTS;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return DLT645_ERR_IO;
    }
    tcflush(fd, TCIOFLUSH);

    s->handle = (intptr_t)fd;
    s->is_open = 1;
    return DLT645_OK;
}

void dlt645_serial_close(dlt645_serial_t *s)
{
    if (s != NULL && s->is_open) {
        close((int)s->handle);
        s->is_open = 0;
    }
}

static int serial_send(void *user, const uint8_t *data, size_t len,
                       uint32_t timeout_ms)
{
    dlt645_serial_t *s = (dlt645_serial_t *)user;
    size_t sent = 0;
    int fd;
    if (s == NULL || !s->is_open) {
        return -1;
    }
    fd = (int)s->handle;
    while (sent < len) {
        ssize_t n = write(fd, data + sent, len - sent);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            fd_set wfds;
            struct timeval tv;
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            tv.tv_sec = (time_t)(timeout_ms / 1000u);
            tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
            if (select(fd + 1, NULL, &wfds, NULL, &tv) <= 0) {
                return (sent > 0) ? (int)sent : -1;
            }
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        return (sent > 0) ? (int)sent : -1;
    }
    (void)tcdrain(fd);
    return (int)sent;
}

static int serial_recv(void *user, uint8_t *data, size_t len,
                       uint32_t timeout_ms)
{
    dlt645_serial_t *s = (dlt645_serial_t *)user;
    int fd;
    (void)timeout_ms;
    if (s == NULL || !s->is_open) {
        return -1;
    }
    fd = (int)s->handle;
    for (;;) {
        ssize_t n = read(fd, data, len);
        if (n >= 0) {
            return (int)n;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
}

static uint32_t serial_now_ms(void *user)
{
    struct timespec ts;
    (void)user;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u +
                      (uint64_t)(ts.tv_nsec / 1000000));
}

static void serial_delay_ms(void *user, uint32_t ms)
{
    struct timespec ts;
    (void)user;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000u);
    nanosleep(&ts, NULL);
}

#endif

void dlt645_serial_port(dlt645_serial_t *s, dlt645_port_t *port)
{
    if (s == NULL || port == NULL) {
        return;
    }
    memset(port, 0, sizeof(*port));
    port->user = s;
    port->send = serial_send;
    port->recv = serial_recv;
    port->now_ms = serial_now_ms;
    port->delay_ms = serial_delay_ms;
}

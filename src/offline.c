/* Copyright (c) 2025-2026 gugueh (Kirv Studio). All rights reserved. */
/* Newlib's generic file descriptors also support sockets. This application
 * cannot create or use sockets: replace the three linked socket fallbacks
 * with errors, and enforce absence of networking imports in verify_vpk.py.
 * No call is forwarded to the real network library. */
#include <stddef.h>
#include <psp2/net/net.h>

int __wrap_sceNetRecv(int socket, void *buffer, unsigned int length, int flags)
{
    (void)socket; (void)buffer; (void)length; (void)flags;
    return (int)SCE_NET_ERROR_EOPNOTSUPP;
}

int __wrap_sceNetSend(int socket, const void *buffer, unsigned int length, int flags)
{
    (void)socket; (void)buffer; (void)length; (void)flags;
    return (int)SCE_NET_ERROR_EOPNOTSUPP;
}

int __wrap_sceNetSocketClose(int socket)
{
    (void)socket;
    return (int)SCE_NET_ERROR_EOPNOTSUPP;
}

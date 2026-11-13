/* gfopennetkit, Amiga side: bsdsocket.library TCP and gfopentls.library TLS.
 * MIT licence, (c) 2026 AmiCradle Team. */
#ifndef GFOPENNETKIT_NETKIT_AMIGA_H
#define GFOPENNETKIT_NETKIT_AMIGA_H

#include "gfopennetkit/netkit.h"

struct Library;
struct OTContext;

/* TCP connect (blocking resolve and connect, then non-blocking I/O) through
 * the calling task's bsdsocket.library base. */
int nk_amiga_tcp(struct Library *socketBase, const char *host, uint16_t port,
                 NKTransport *out);

/* TLS on top of a connected nk_amiga_tcp transport (also one that went
 * through a SOCKS5 proxy first). `verify` is an OTV_* mode, `pin` a SHA-256
 * certificate fingerprint or NULL. Call nk_tls_handshake() until NK_OK. */
int nk_amiga_tls(struct Library *tlsBase, struct OTContext *ctx,
                 const char *host, long verify, const uint8_t *pin,
                 NKTransport *tcp, NKTransport *out);

#endif

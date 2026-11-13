/* gfopennetkit: shared network parts for GFOpenNews, GFOpenIRC and GFOpenTorrent.
 * MIT licence, (c) 2026 AmiCradle Team.
 *
 * Plain C99 with no Amiga or POSIX types in this header, so the same code is
 * built for the Amiga (bsdsocket.library + gfopentls.library, src/amiga/)
 * and for the host unit tests (POSIX sockets, src/nk_posix.c).
 *
 * Nothing here reads a clock or a random number on its own: the caller passes
 * the time (milliseconds, any origin) and, for backoff jitter, a random value.
 * That keeps every test free of clock and timing dependencies.
 */
#ifndef GFOPENNETKIT_NETKIT_H
#define GFOPENNETKIT_NETKIT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NK_VERSION_MAJOR 0
#define NK_VERSION_MINOR 1

/* ---- results ------------------------------------------------------------ */
enum {
    NK_OK          =  0,
    NK_WOULDBLOCK  = -1,  /* non-blocking transport: try again when ready */
    NK_EOF         = -2,  /* the peer closed the connection */
    NK_EIO         = -3,  /* transport or file failed */
    NK_ENOMEM      = -4,
    NK_ETOOLONG    = -5,  /* a line over the cap; the reader skipped it */
    NK_EPROTO      = -6,  /* the peer broke the protocol (SOCKS5 reply etc.) */
    NK_ETIMEDOUT   = -7,
    NK_EARG        = -8,
    NK_ECANCELLED  = -9,  /* nk_conn_cancel() was called (QUIT, "cancel all") */
    NK_EREFUSED    = -10, /* proxy or peer refused */
    NK_ECORRUPT    = -11  /* a journal record failed its check */
};

const char *nk_strerror(int err);

/* ---- CRC32 (IEEE 802.3, reflected, as zlib, PNG, yEnc and PAR2 use) ------
 * Our own table-driven code. nk_crc32_update(0, ...) starts a new CRC. */
uint32_t nk_crc32_update(uint32_t crc, const void *data, size_t len);
/* CRC of A||B from crc(A), crc(B) and len(B), for yEnc parts assembled out of
 * order and for PAR2 slices. */
uint32_t nk_crc32_combine(uint32_t crc_a, uint32_t crc_b, uint64_t len_b);

/* ---- transport ------------------------------------------------------------
 * Moves bytes. TCP, TLS over TCP, a SOCKS5 tunnel and the test pipes all look
 * like this. read/write answer bytes moved (>0), NK_WOULDBLOCK, NK_EOF or
 * NK_EIO. pending answers bytes already decrypted and waiting (TLS), so the
 * caller can drain them before it sleeps in WaitSelect()/poll(). */
typedef struct NKTransport {
    void *ctx;
    long (*read)(void *ctx, void *buf, long len);
    long (*write)(void *ctx, const void *buf, long len);
    long (*pending)(void *ctx);           /* may be NULL: nothing buffered */
    void (*close)(void *ctx);             /* frees ctx; may be NULL */
    int  (*fd)(void *ctx);                /* socket for select; may be NULL */
} NKTransport;

void nk_transport_close(NKTransport *t);

/* ---- line reader ----------------------------------------------------------
 * Splits a byte stream into lines at LF, dropping a CR before it. A line
 * longer than max_line is never handed out: the reader answers NK_ETOOLONG
 * once and throws bytes away up to the next LF. The buffer is the caller's. */
typedef struct NKLineReader {
    char  *buf;
    size_t cap;        /* bytes in buf */
    size_t start;      /* first unread byte */
    size_t len;        /* bytes held from start */
    size_t max_line;   /* longest line handed out, without CR LF */
    size_t scan;       /* bytes from start already searched for LF */
    int    skipping;   /* inside an over-long line */
} NKLineReader;

void   nk_lr_init(NKLineReader *lr, char *buf, size_t cap, size_t max_line);
/* Where to put new bytes, and how many fit (compacts the buffer first). */
char  *nk_lr_space(NKLineReader *lr, size_t *room);
void   nk_lr_commit(NKLineReader *lr, size_t n);
/* 1 = line and len set (not NUL-terminated; valid until the next call),
 * 0 = need more bytes, NK_ETOOLONG = a line was dropped. */
int    nk_lr_next(NKLineReader *lr, const char **line, size_t *len);
/* Raw bytes held after the last line (for binary bodies after a header). */
size_t nk_lr_buffered(const NKLineReader *lr);
size_t nk_lr_take(NKLineReader *lr, void *out, size_t max);

/* ---- reconnect backoff ----------------------------------------------------
 * Exponential with a cap and "equal jitter": the wait is half the step plus a
 * random part of the other half. rnd is any 32-bit random value. */
typedef struct NKBackoff {
    uint32_t base_ms, max_ms, step_ms;
    uint32_t attempts;
} NKBackoff;

void     nk_backoff_init(NKBackoff *b, uint32_t base_ms, uint32_t max_ms);
uint32_t nk_backoff_next(NKBackoff *b, uint32_t rnd);
void     nk_backoff_reset(NKBackoff *b);

/* ---- connection -----------------------------------------------------------
 * A transport plus a line reader, an output queue, timeouts and cancel. All
 * calls are non-blocking; the app's event loop waits on nk_conn_fd() (or the
 * TLS pending count) and calls nk_conn_pump(). */
typedef struct NKConn NKConn;

typedef struct NKConnConfig {
    size_t   in_cap;         /* line buffer, default 16 KB */
    size_t   max_line;       /* default 8190: IRC 512+tags 8191, NNTP 998 */
    size_t   out_cap;        /* output queue limit, default 64 KB */
    uint32_t idle_timeout_ms;/* 0 = none: no bytes read for this long */
} NKConnConfig;

NKConn *nk_conn_new(const NKConnConfig *cfg, NKTransport t, uint32_t now_ms);
void    nk_conn_free(NKConn *c);              /* closes the transport */
/* Queue bytes; NK_ENOMEM if the queue would pass out_cap. */
int     nk_conn_send(NKConn *c, const void *data, size_t len);
/* Queue a line, adding CR LF. Refuses CR or LF inside (NK_EARG): no
 * command injection through a nick, subject or group name. */
int     nk_conn_sendline(NKConn *c, const char *line);
int     nk_conn_sendf(NKConn *c, const char *fmt, ...);
/* Write what the transport takes, read what it has. Answers NK_OK,
 * NK_EOF, NK_EIO, NK_ETIMEDOUT or NK_ECANCELLED. */
int     nk_conn_pump(NKConn *c, uint32_t now_ms);
/* Next line, as nk_lr_next. */
int     nk_conn_line(NKConn *c, const char **line, size_t *len);
size_t  nk_conn_outq(const NKConn *c);       /* bytes still to write */
int     nk_conn_fd(const NKConn *c);         /* -1 when unknown */
NKLineReader *nk_conn_reader(NKConn *c);
/* Thread- and signal-safe flag: the next pump answers NK_ECANCELLED. */
void    nk_conn_cancel(NKConn *c);

/* ---- SOCKS5 (RFC 1928, user/pass RFC 1929) --------------------------------
 * Runs over an already connected transport to the proxy, then hands the same
 * transport on as the tunnel. Non-blocking: call nk_socks5_step until it
 * answers 1 (tunnel up) or an error. */
typedef struct NKSocks5 {
    int      state;
    char     host[256];
    uint16_t port;
    char     user[256], pass[256];
    uint8_t  buf[600];
    size_t   have, want, sent, out_len;
} NKSocks5;

int nk_socks5_init(NKSocks5 *s, const char *host, uint16_t port,
                   const char *user, const char *pass);
int nk_socks5_step(NKSocks5 *s, NKTransport *t);

/* ---- journal ----------------------------------------------------------------
 * A crash-safe append-only log of text records (no LF inside a record). Each
 * line is "<crc32 hex8> <record>\n". On open, records are replayed in order;
 * a torn or damaged tail is cut off, a damaged record in the middle is
 * skipped and counted. Compaction writes the live records to <path>.new and
 * swaps it in, with a recovery path for a crash at any point of the swap
 * (AmigaDOS Rename() won't replace a file, so the swap is three steps). */
typedef struct NKJournal NKJournal;
typedef int (*NKJournalReplay)(void *user, const char *rec, size_t len);

typedef struct NKJournalStats {
    unsigned long records, skipped, truncated_bytes;
} NKJournalStats;

NKJournal *nk_journal_open(const char *path, NKJournalReplay fn, void *user,
                           NKJournalStats *stats, int *err);
int  nk_journal_append(NKJournal *j, const char *rec, size_t len);
/* Flushes to the file system (fflush; fsync where the host has it). */
int  nk_journal_sync(NKJournal *j);
/* Rewrites the journal with only the records `next` hands out (answering 1
 * per record, 0 at the end). */
typedef int (*NKJournalNext)(void *user, const char **rec, size_t *len);
int  nk_journal_compact(NKJournal *j, NKJournalNext next, void *user);
unsigned long nk_journal_records(const NKJournal *j);
void nk_journal_close(NKJournal *j);

/* ---- TLS over a transport ---------------------------------------------------
 * The TLS library (gfopentls.library on the Amiga) is reached through this
 * table, so the adapter's handling of "would block", clean close and cut-off
 * close is the same code on the Amiga and in the host tests. Each op answers
 * as OT_* does: >= 0 done, otherwise an OTERR_* value (see NK_TLSERR_*). */
#define NK_TLSERR_CLOSED     (-4)  /* peer closed without close_notify */
#define NK_TLSERR_WOULDBLOCK (-5)

typedef struct NKTlsOps {
    long (*handshake)(void *tls);
    long (*read)(void *tls, void *buf, long len);
    long (*write)(void *tls, const void *buf, long len);
    long (*pending)(void *tls);
    long (*close_notify)(void *tls);
    void (*free)(void *tls);
    const char *(*errtext)(void *tls);
} NKTlsOps;

/* Wraps `tls` over `tcp` (taking both). The new transport closes them in the
 * right order: close_notify, free the TLS connection, close the socket. */
int  nk_tls_wrap(const NKTlsOps *ops, void *tls, NKTransport *tcp, NKTransport *out);
/* NK_OK when the handshake is done, NK_WOULDBLOCK, or NK_EPROTO with the
 * TLS library's code and words from nk_tls_error(). */
int  nk_tls_handshake(NKTransport *t);
long nk_tls_error(NKTransport *t, const char **text);
/* 1 when the peer closed without close_notify (the last data may be cut).
 * Such a close reads as NK_EOF like a clean one, so an app must check this
 * at EOF and never take a transfer that ended there as complete (an
 * article body, a yEnc part, a file): treat it as an I/O error. */
int  nk_tls_truncated(NKTransport *t);

/* ---- host / POSIX helpers (not on the Amiga build) --------------------------
 * Connects TCP (blocking connect, then non-blocking I/O). For tests and tools. */
int nk_posix_tcp(const char *host, uint16_t port, NKTransport *out);

#ifdef __cplusplus
}
#endif
#endif

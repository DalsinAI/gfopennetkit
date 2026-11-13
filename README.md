# gfopennetkit

GFOS shared network kit: TLS connections over GFOpenSocket, text view, journal; used by GFOpenNews, GFOpenIRC and GFOpenTorrent

Part of GFOS (AmiCradle). Design: amicradle-teams `designs/` (GFOPENNEWS-20261113.md, GFOPENIRC-20261113.md, and the Team 2 answers of 13 Nov 2026).

Rules: commits are authored and committed by SacredTrees; draw through GFOpenLayout; third-party code keeps its own licence and goes in `third_party/` with its notice (BSD-3-Clause libtorrent is allowed; no GPL, LGPL or MPL code).

## What is here (0.1, step 1 of the GFOpenNews design)

Plain C99, one static link library (`libgfopennetkit.a`). Every call is
non-blocking, and nothing reads a clock or a random number by itself: the app
passes the time and the random value, so the unit tests never depend on
timing.

| Part | Header | Notes |
| --- | --- | --- |
| Transport | `netkit.h` `NKTransport` | read, write, pending, close, fd. TCP, TLS, a SOCKS5 tunnel and the test mocks all look alike |
| Line reader | `nk_lr_*` | LF or CR LF, a cap per line (an over-long line is dropped once with `NK_ETOOLONG`, the stream carries on), raw bytes after a header for binary bodies |
| Connection | `nk_conn_*` | transport + line reader + bounded output queue, idle timeout from the caller's clock (wraps safely), `nk_conn_cancel()` for QUIT and "cancel all". `nk_conn_sendline()` refuses CR or LF inside a line, so a nick, subject or group name can't inject a command |
| TLS | `nk_tls_*`, `netkit_amiga.h` `nk_amiga_tls()` | gfopentls.library through an ops table: "would block", clean close (close_notify) and cut-off close (`nk_tls_truncated()`) handled once, in the same code the host tests run against a fake TLS library. Close order: close_notify, free the TLS connection, close the socket |
| TCP | `nk_amiga_tcp()` (bsdsocket.library), `nk_posix_tcp()` (host tests and tools) | |
| SOCKS5 | `nk_socks5_*` | RFC 1928 and RFC 1929 user/password, non-blocking, the password wiped after use |
| Reconnect | `nk_backoff_*` | exponential with a cap and equal jitter |
| Journal | `nk_journal_*` | crash-safe append-only log of text records with a CRC32 per line: a torn tail is cut off, a damaged record in the middle is skipped and counted, compaction swaps in three steps (AmigaDOS `Rename()` never replaces a file) and open repairs a crash at any point of the swap |
| CRC-32 | `nk_crc32_update`, `nk_crc32_combine` | our own table-driven code (slicing by 8), for the journal, yEnc and PAR2; combine joins CRCs of parts |

Passwords are not kept here: apps get them from gfopenkeys.library (the system
keystore) and hand them over only for the moment they are sent.

| Text view model | `textview.h` `nk_tv_*` | scrollback ring bounded by lines and bytes, seq numbers that survive eviction, a last-read marker; IRC codes (bold, italic, underline, strike, mono, reverse, `^C` colours, `^D` hex colours, reset) taken out into style runs; news quote levels and the dimmed `-- ` signature; links (http, https, ftp, news, nntp, irc, ircs, mailto, www.) with trailing punctuation and unmatched `)` left out; wrapping through the caller's measure function (pixels on screen), never inside a UTF-8 character; search both ways; copy |

Still to come in this repo: the text view's GFOpenLayout element (drawing,
scrolling, selection with the mouse), which needs a lab run.

## Building and testing

    cmake -S . -B build && cmake --build build && ctest --test-dir build

The tests run on the host. `live_fake_servers` starts the fake NNTP, IRC and
SOCKS5 servers in `tests/fakes/fakeserv.py` on free local ports and runs a
real-socket session through each, directly and through the proxy; no test
needs the internet. GFOpenNews and GFOpenIRC use the same fakes.

    ./build-amiga.sh        # STOVE=... TLS=../gfopentls; makes build-amiga/lib/libgfopennetkit.a

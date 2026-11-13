#!/usr/bin/env python3
# Fake NNTP, IRC and SOCKS5 servers for the gfopennetkit, GFOpenNews and
# GFOpenIRC unit tests, so no test needs the internet.
# MIT licence, (c) 2026 AmiCradle Team.
#
#   fakeserv.py nntp,irc,socks5          serve on free ports, print
#                                         "PORTS nntp=N irc=N socks5=N"
#   fakeserv.py --run nntp,socks5 -- CMD  start them, run CMD with
#                                         NK_NNTP_PORT etc. set, answer
#                                         CMD's exit status
import os
import socket
import socketserver
import struct
import subprocess
import sys
import threading

# ---- NNTP (RFC 3977, RFC 4643) -------------------------------------------
NNTP_USER, NNTP_PASS = "dale", "s3cret"
GROUPS = {
    "comp.sys.amiga.misc": {
        1: ("Re: A1200 trapdoor", "a@b.example", "<1@fake>", ""),
        2: ("Kickstart question", "c@d.example", "<2@fake>", "<1@fake>"),
        3: ("..dots at the start", "e@f.example", "<3@fake>", ""),
    },
    "alt.binaries.test": {},
}
# LIST ACTIVE: name high low status
NEWGROUPS = ["comp.sys.amiga.announce"]
BODY = {
    3: [".leading dot line", "..two dots", "plain"],
}


def spool_file(spool, msgid):
    """Spool article for a message-id: <spool>/<id with unsafe chars as _>.art"""
    safe = "".join(c if c.isalnum() or c in "@._-" else "_" for c in msgid.strip("<>"))
    return os.path.join(spool, safe + ".art")


class NNTPHandler(socketserver.StreamRequestHandler):
    # binaries tests put articles in a spool directory named by this variable
    spool_env = "NK_NNTP_SPOOL"

    def out(self, line):
        self.wfile.write(line.encode("latin-1") + b"\r\n")

    def handle(self):
        authed = False
        user = None
        group = None
        self.out("200 fake NNTP ready, posting allowed")
        for raw in self.rfile:
            line = raw.decode("latin-1").rstrip("\r\n")
            parts = line.split(" ")
            cmd = parts[0].upper()
            if cmd == "QUIT":
                self.out("205 bye")
                return
            if cmd == "CAPABILITIES":
                self.out("101 capability list follows")
                for c in ("VERSION 2", "READER", "AUTHINFO USER", "OVER", "POST", "."):
                    self.out(c)
            elif cmd == "MODE" and len(parts) > 1 and parts[1].upper() == "READER":
                self.out("200 reader mode")
            elif cmd == "AUTHINFO" and len(parts) > 2 and parts[1].upper() == "USER":
                user = parts[2]
                self.out("381 password required")
            elif cmd == "AUTHINFO" and len(parts) > 2 and parts[1].upper() == "PASS":
                if user == NNTP_USER and parts[2] == NNTP_PASS:
                    authed = True
                    self.out("281 authentication accepted")
                else:
                    self.out("481 authentication failed")
            elif not authed:
                self.out("480 authentication required")
            elif cmd == "LIST" and (len(parts) == 1 or parts[1].upper() == "ACTIVE"):
                self.out("215 list of newsgroups follows")
                for name, g in sorted(GROUPS.items()):
                    nums = sorted(g) or [0]
                    hi, lo = nums[-1], nums[0] if g else 1
                    self.out("%s %d %d %s" % (name, hi, lo, "y"))
                self.out(".")
            elif cmd == "NEWGROUPS":
                self.out("231 list of new newsgroups follows")
                for name in NEWGROUPS:
                    self.out("%s 0 1 y" % name)
                self.out(".")
            elif cmd == "STAT" and len(parts) > 1 and group:
                n = int(parts[1]) if parts[1].isdigit() else -1
                art = GROUPS[group].get(n)
                if art:
                    self.out("223 %d %s" % (n, art[2]))
                else:
                    self.out("423 no such article")
            elif cmd == "GROUP" and len(parts) > 1:
                g = GROUPS.get(parts[1])
                if g is None:
                    self.out("411 no such group")
                else:
                    group = parts[1]
                    nums = sorted(g) or [0]
                    self.out("211 %d %d %d %s" % (len(g), nums[0], nums[-1], group))
            elif cmd in ("OVER", "XOVER"):
                if not group:
                    self.out("412 no group selected")
                    continue
                self.out("224 overview follows")
                for n, (subj, frm, mid, ref) in sorted(GROUPS[group].items()):
                    self.out("\t".join([str(n), subj, frm, "13 Nov 2026 18:00:00 +0000",
                                        mid, ref, "100", "3"]))
                self.out(".")
            elif cmd in ("ARTICLE", "BODY") and len(parts) > 1 and parts[1].startswith("<"):
                spool = os.environ.get(self.spool_env)
                path = spool_file(spool, parts[1]) if spool else None
                if not path or not os.path.exists(path):
                    self.out("430 no such article")
                    continue
                self.out("%d 0 %s" % (220 if cmd == "ARTICLE" else 222, parts[1]))
                with open(path, "rb") as f:
                    body = f.read()
                if cmd == "ARTICLE":
                    self.out("Message-ID: " + parts[1])
                    self.out("")
                if body.endswith(b"\n"):
                    body = body[:-1]
                for b in body.split(b"\n"):
                    if b.endswith(b"\r"):
                        b = b[:-1]
                    self.wfile.write((b"." + b if b.startswith(b".") else b) + b"\r\n")
                self.out(".")
            elif cmd in ("ARTICLE", "BODY", "HEAD") and len(parts) > 1 and group:
                n = int(parts[1]) if parts[1].isdigit() else -1
                art = GROUPS[group].get(n)
                if not art:
                    self.out("423 no such article")
                    continue
                self.out("%d %d %s" % ({"ARTICLE": 220, "HEAD": 221, "BODY": 222}[cmd], n, art[2]))
                if cmd != "BODY":
                    self.out("Subject: " + art[0])
                    self.out("From: " + art[1])
                    self.out("Message-ID: " + art[2])
                    if art[3]:
                        self.out("References: " + art[3])
                if cmd == "HEAD":
                    self.out(".")
                    continue
                if cmd == "ARTICLE":
                    self.out("")
                for b in BODY.get(n, ["body of %d" % n]):
                    self.out("." + b if b.startswith(".") else b)  # dot-stuffing
                self.out(".")
            else:
                self.out("500 unknown command")


# ---- IRC (RFC 2812 subset, CAP LS 302, SASL PLAIN) -------------------------
class IRCHandler(socketserver.StreamRequestHandler):
    def out(self, line):
        self.wfile.write(line.encode("utf-8") + b"\r\n")

    def handle(self):
        nick = None
        registered = False
        in_cap = False
        for raw in self.rfile:
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            parts = line.split(" ", 1)
            cmd = parts[0].upper()
            arg = parts[1] if len(parts) > 1 else ""
            if cmd == "CAP":
                sub = arg.split(" ")[0].upper()
                if sub == "LS":
                    in_cap = True
                    self.out(":fake.irc CAP * LS :sasl server-time multi-prefix")
                elif sub == "REQ":
                    self.out(":fake.irc CAP * ACK :" + arg.split(":", 1)[-1])
                elif sub == "END":
                    in_cap = False
            elif cmd == "AUTHENTICATE":
                if arg == "PLAIN":
                    self.out("AUTHENTICATE +")
                else:
                    self.out(":fake.irc 900 %s %s!u@h dale :You are now logged in" % (nick or "*", nick or "*"))
                    self.out(":fake.irc 903 %s :SASL authentication successful" % (nick or "*"))
            elif cmd == "NICK":
                nick = arg.lstrip(":")
            elif cmd == "USER":
                pass
            elif cmd == "PING":
                self.out(":fake.irc PONG fake.irc " + arg)
            elif cmd == "JOIN":
                self.out(":%s!u@h JOIN %s" % (nick, arg))
                self.out(":fake.irc 353 %s = %s :@op +voice %s" % (nick, arg, nick))
                self.out(":fake.irc 366 %s %s :End of /NAMES list." % (nick, arg))
            elif cmd == "PRIVMSG":
                target, text = arg.split(" ", 1)
                self.out(":echo!e@h PRIVMSG %s %s" % (nick, text))
            elif cmd == "QUIT":
                self.out("ERROR :Closing link")
                return
            if nick and not registered and not in_cap:
                registered = True
                self.out(":fake.irc 001 %s :Welcome to the fake network" % nick)
                self.out(":fake.irc 005 %s CHANTYPES=# PREFIX=(ov)@+ CASEMAPPING=rfc1459 :are supported" % nick)
                self.out(":fake.irc PING :tok123")


# ---- SOCKS5 (RFC 1928/1929), forwards to localhost only --------------------
SOCKS_USER, SOCKS_PASS = b"proxyuser", b"proxypass"


class SOCKSHandler(socketserver.BaseRequestHandler):
    def recvn(self, n):
        b = b""
        while len(b) < n:
            c = self.request.recv(n - len(b))
            if not c:
                raise EOFError
            b += c
        return b

    def handle(self):
        s = self.request
        try:
            ver, nm = self.recvn(2)
            methods = self.recvn(nm)
            if 2 in methods:
                s.sendall(b"\x05\x02")
                self.recvn(1)
                u = self.recvn(self.recvn(1)[0])
                p = self.recvn(self.recvn(1)[0])
                ok = (u, p) == (SOCKS_USER, SOCKS_PASS)
                s.sendall(b"\x01\x00" if ok else b"\x01\x01")
                if not ok:
                    return
            else:
                s.sendall(b"\x05\x00")
            ver, cmd, _, atyp = self.recvn(4)
            if atyp != 3:
                s.sendall(b"\x05\x08\x00\x01\0\0\0\0\0\0")
                return
            host = self.recvn(self.recvn(1)[0]).decode()
            port = struct.unpack(">H", self.recvn(2))[0]
            if host not in ("localhost", "127.0.0.1"):
                s.sendall(b"\x05\x02\x00\x01\0\0\0\0\0\0")  # not allowed
                return
            try:
                up = socket.create_connection(("127.0.0.1", port))
            except OSError:
                s.sendall(b"\x05\x05\x00\x01\0\0\0\0\0\0")
                return
            s.sendall(b"\x05\x00\x00\x01\x7f\x00\x00\x01" + struct.pack(">H", port))
        except EOFError:
            return

        def pipe(a, b):
            try:
                while True:
                    d = a.recv(4096)
                    if not d:
                        break
                    b.sendall(d)
            except OSError:
                pass
            try:
                b.shutdown(socket.SHUT_WR)
            except OSError:
                pass
        t = threading.Thread(target=pipe, args=(up, s), daemon=True)
        t.start()
        pipe(s, up)
        t.join(5)
        up.close()


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class NNTP2Handler(NNTPHandler):
    """A second news server (backup) with its own spool."""
    spool_env = "NK_NNTP2_SPOOL"


KINDS = {"nntp": NNTPHandler, "nntp2": NNTP2Handler, "irc": IRCHandler, "socks5": SOCKSHandler}


def start(kinds):
    ports = {}
    for k in kinds:
        srv = Server(("127.0.0.1", 0), KINDS[k])
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        ports[k] = srv.server_address[1]
    return ports


def main(argv):
    if argv and argv[0] == "--run":
        kinds = argv[1].split(",")
        cmd = argv[argv.index("--") + 1:]
        ports = start(kinds)
        env = dict(os.environ)
        for k, p in ports.items():
            env["NK_%s_PORT" % k.upper()] = str(p)
        return subprocess.call(cmd, env=env, timeout=60)
    ports = start(argv[0].split(",") if argv else ["nntp"])
    print("PORTS " + " ".join("%s=%d" % kv for kv in ports.items()), flush=True)
    threading.Event().wait()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

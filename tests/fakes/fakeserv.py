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
    posted = 0
    lock = threading.Lock()

    def out(self, line):
        self.wfile.write(line.encode("latin-1") + b"\r\n")

    def post(self):
        """POST (RFC 3977 6.3.1): 340, then the dot-stuffed article up to ".".
        It needs From, Newsgroups (known groups only) and Subject headers,
        else 441. With NK_NNTP_POSTED naming a directory, the article (dots
        undone, LF line ends) is written there as <n>.art."""
        self.out("340 send article to be posted")
        lines = []
        for raw in self.rfile:
            l = raw.decode("latin-1").rstrip("\r\n")
            if l == ".":
                break
            lines.append(l[1:] if l.startswith("..") else l)
        else:
            return
        heads = {}
        for l in lines:
            if not l:
                break
            if ":" in l and not l[0].isspace():
                k, v = l.split(":", 1)
                heads[k.strip().lower()] = v.strip()
        groups = [g.strip() for g in heads.get("newsgroups", "").split(",") if g.strip()]
        why = None
        if not heads.get("from"):
            why = "no From header"
        elif not heads.get("subject"):
            why = "no Subject header"
        elif not groups:
            why = "no Newsgroups header"
        elif any(g not in GROUPS for g in groups):
            why = "no such newsgroup"
        if why:
            self.out("441 posting failed: " + why)
            return
        with NNTPHandler.lock:
            NNTPHandler.posted += 1
            n = NNTPHandler.posted
        mid = heads.get("message-id") or "<post%d@fake>" % n
        d = os.environ.get("NK_NNTP_POSTED")
        if d:
            with open(os.path.join(d, "%d.art" % n), "w", encoding="latin-1") as f:
                f.write("\n".join(lines) + "\n")
        self.out("240 article posted " + mid)

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
            elif cmd == "POST":
                self.post()
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
# SASL accepts dale / s3cret. The nick "taken" is in use (433). With
# server-time acked, lines to the client carry @time; with echo-message,
# the client's own PRIVMSG comes back. Every PRIVMSG is answered by "echo".
IRC_CAPS = ["sasl=PLAIN,EXTERNAL server-time message-tags",
            "multi-prefix echo-message away-notify"]
IRC_TIME = "2026-11-13T19:00:00.000Z"


class IRCHandler(socketserver.StreamRequestHandler):
    def out(self, line):
        if getattr(self, "acked", None) and "server-time" in self.acked and line.startswith(":"):
            line = "@time=%s %s" % (IRC_TIME, line)
        self.wfile.write(line.encode("utf-8") + b"\r\n")

    def handle(self):
        nick = None
        registered = False
        in_cap = False
        self.acked = set()
        channels = {}
        for raw in self.rfile:
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            if line.startswith("@"):
                line = line.split(" ", 1)[1] if " " in line else ""
            parts = line.split(" ", 1)
            cmd = parts[0].upper()
            arg = parts[1] if len(parts) > 1 else ""
            me = nick or "*"
            if cmd == "CAP":
                sub = arg.split(" ")[0].upper()
                if sub == "LS":
                    in_cap = True
                    if "302" in arg:
                        self.out(":fake.irc CAP * LS * :" + IRC_CAPS[0])
                        self.out(":fake.irc CAP * LS :" + IRC_CAPS[1])
                    else:
                        self.out(":fake.irc CAP * LS :" + " ".join(IRC_CAPS).replace("=PLAIN,EXTERNAL", ""))
                elif sub == "REQ":
                    want = arg.split(":", 1)[-1].split()
                    known = " ".join(IRC_CAPS).split()
                    known = [k.split("=")[0] for k in known]
                    if all(w.lstrip("-") in known for w in want):
                        self.acked.update(w for w in want if not w.startswith("-"))
                        self.out(":fake.irc CAP %s ACK :%s" % (me, " ".join(want)))
                    else:
                        self.out(":fake.irc CAP %s NAK :%s" % (me, " ".join(want)))
                elif sub == "END":
                    in_cap = False
            elif cmd == "AUTHENTICATE":
                if arg == "PLAIN":
                    self.out("AUTHENTICATE +")
                elif arg == "*":
                    self.out(":fake.irc 906 %s :SASL authentication aborted" % me)
                else:
                    import base64
                    try:
                        z, u, pw = base64.b64decode(arg).split(b"\0")
                    except Exception:
                        z = u = pw = b""
                    if u == b"dale" and pw == b"s3cret":
                        self.out(":fake.irc 900 %s %s!u@h dale :You are now logged in as dale" % (me, me))
                        self.out(":fake.irc 903 %s :SASL authentication successful" % me)
                    else:
                        self.out(":fake.irc 904 %s :SASL authentication failed" % me)
            elif cmd == "NICK":
                want = arg.lstrip(":")
                if want == "taken":
                    self.out(":fake.irc 433 %s %s :Nickname is already in use" % (me, want))
                    continue
                if registered:
                    self.out(":%s!u@h NICK :%s" % (nick, want))
                nick = want
            elif cmd == "USER" or cmd == "PASS":
                pass
            elif cmd == "PING":
                self.out(":fake.irc PONG fake.irc " + arg)
            elif cmd == "JOIN":
                for ch in arg.split(" ")[0].split(","):
                    channels[ch] = True
                    self.out(":%s!u@h JOIN %s" % (nick, ch))
                    self.out(":fake.irc 332 %s %s :Amiga talk, be nice" % (nick, ch))
                    if "multi-prefix" in self.acked:
                        self.out(":fake.irc 353 %s = %s :@+op +voice %s" % (nick, ch, nick))
                    else:
                        self.out(":fake.irc 353 %s = %s :@op +voice %s" % (nick, ch, nick))
                    self.out(":fake.irc 366 %s %s :End of /NAMES list." % (nick, ch))
                    self.out(":other!o@h JOIN %s" % ch)
                    self.out(":fake.irc MODE %s +o other" % ch)
            elif cmd == "PART":
                ch = arg.split(" ")[0]
                channels.pop(ch, None)
                self.out(":%s!u@h PART %s" % (nick, arg))
            elif cmd in ("PRIVMSG", "NOTICE"):
                target, text = arg.split(" ", 1)
                if "echo-message" in self.acked:
                    self.out(":%s!u@h %s %s %s" % (nick, cmd, target, text))
                if cmd == "PRIVMSG":
                    if text.startswith(":\x01VERSION"):
                        self.out(":echo!e@h PRIVMSG %s :\x01VERSION\x01" % nick)
                    else:
                        self.out(":echo!e@h PRIVMSG %s %s" % (target if target in channels else nick, text))
            elif cmd == "QUIT":
                self.out("ERROR :Closing link")
                return
            if nick and not registered and not in_cap:
                registered = True
                self.out(":fake.irc 001 %s :Welcome to the fake network" % nick)
                self.out(":fake.irc 005 %s CHANTYPES=# PREFIX=(ov)@+ CASEMAPPING=rfc1459 NICKLEN=16 "
                         "CHANMODES=b,k,l,imnt NETWORK=FakeNet :are supported" % nick)
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

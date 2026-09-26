#!/usr/bin/env python3
"""Upload-only FTP server for bench results (Python standard library only).

A bench PC runs mTCP's FTP client from a script after each job and MPUTs
C:\\OUT here. The sink accepts one login, stores every STOR under its root
directory (flat, file names only) and refuses everything that would read
or change the host. Passive mode only (mTCP's default).

  ftpsink.py --root DIR --port 2121 [--pasv-address 10.0.2.2] [--user U --password P]

tools/bench/run.py starts one per job, in-process (FtpSink)."""
import argparse
import os
import re
import socket
import socketserver
import threading


class _Handler(socketserver.StreamRequestHandler):
    timeout = 120

    def reply(self, text):
        self.wfile.write((text + "\r\n").encode("latin-1"))

    def handle(self):
        sink = self.server.sink
        authed, user, pasv = False, None, None
        self.reply("220 MGA-Glide bench upload sink")
        while True:
            try:
                raw = self.rfile.readline(1024)
            except (socket.timeout, OSError):
                break
            if not raw:
                break
            line = raw.decode("latin-1").rstrip("\r\n")
            cmd, _, arg = line.partition(" ")
            cmd = cmd.upper()
            if cmd == "USER":
                user = arg
                self.reply("331 password please")
            elif cmd == "PASS":
                authed = sink.user is None or (user == sink.user and arg == sink.password)
                self.reply("230 logged in" if authed else "530 login incorrect")
            elif cmd == "QUIT":
                self.reply("221 bye")
                break
            elif not authed:
                self.reply("530 log in first")
            elif cmd == "SYST":
                self.reply("215 UNIX Type: L8")
            elif cmd in ("TYPE", "MODE", "STRU", "NOOP", "OPTS"):
                self.reply("200 ok")
            elif cmd in ("PWD", "XPWD"):
                self.reply('257 "/"')
            elif cmd in ("CWD", "XCWD", "CDUP"):
                self.reply("250 ok")
            elif cmd == "FEAT":
                self.reply("211 no features")
            elif cmd == "PASV":
                if pasv:
                    pasv.close()
                pasv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                pasv.bind(("0.0.0.0", 0))
                pasv.listen(1)
                pasv.settimeout(60)
                port = pasv.getsockname()[1]
                addr = sink.pasv_address or self.connection.getsockname()[0]
                self.reply("227 Entering Passive Mode (%s,%d,%d)" % (addr.replace(".", ","), port >> 8, port & 255))
            elif cmd in ("STOR", "STOU"):
                name = os.path.basename(arg.replace("\\", "/")).strip()
                if not name or name.startswith(".") or not re.match(r"^[\w.$~-]+$", name):
                    self.reply("553 bad file name")
                    continue
                if not pasv:
                    self.reply("425 use PASV first")
                    continue
                self.reply("150 send it")
                try:
                    data, _ = pasv.accept()
                except OSError:
                    self.reply("425 no data connection")
                    pasv.close()
                    pasv = None
                    continue
                n = 0
                path = os.path.join(sink.root, name)
                with data, open(path + ".part", "wb") as f:
                    data.settimeout(120)
                    while True:
                        try:
                            chunk = data.recv(65536)
                        except OSError:
                            break
                        if not chunk:
                            break
                        f.write(chunk)
                        n += len(chunk)
                os.replace(path + ".part", path)
                pasv.close()
                pasv = None
                sink.received.append((name, n))
                self.reply("226 stored %d bytes" % n)
            elif cmd in ("LIST", "NLST"):
                if not pasv:
                    self.reply("425 use PASV first")
                    continue
                self.reply("150 listing")
                try:
                    data, _ = pasv.accept()
                    data.close()
                except OSError:
                    pass
                pasv.close()
                pasv = None
                self.reply("226 done")
            elif cmd in ("MKD", "XMKD"):
                self.reply('257 "/" ok')
            else:
                self.reply("502 not implemented")
        if pasv:
            pasv.close()


class _Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class FtpSink:
    """Run the sink on a background thread: start(), then stop()."""

    def __init__(self, root, port, pasv_address=None, user=None, password=None):
        self.root, self.port = root, port
        self.pasv_address, self.user, self.password = pasv_address, user, password
        self.received = []
        os.makedirs(root, exist_ok=True)
        self.server = _Server(("0.0.0.0", port), _Handler)
        self.server.sink = self
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    def start(self):
        self.thread.start()
        return self

    def stop(self):
        self.server.shutdown()
        self.server.server_close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", required=True)
    ap.add_argument("--port", type=int, default=2121)
    ap.add_argument("--pasv-address")
    ap.add_argument("--user")
    ap.add_argument("--password")
    a = ap.parse_args()
    sink = FtpSink(a.root, a.port, a.pasv_address, a.user, a.password)
    print("ftpsink: %s on port %d" % (a.root, a.port), flush=True)
    try:
        sink.server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

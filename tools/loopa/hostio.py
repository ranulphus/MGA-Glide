"""Host-side I/O for Loop A jobs: the network card's 86Box config, the COM2
bridge (a pty that 86Box opens, relayed to a TCP port), and --tcp-send
steps that talk to the guest through either of them. Used by run.py."""
import os
import select
import socket
import threading
import time
import tty

# 86Box network cards run.py offers (--net), with any config section they need.
# NE2000 (ISA) sits where the Crynwr packet driver looks for it (--net-dos).
NICS = {
    "ne2k": "[NE2000 Compatible #1]\nbase = 0300\nirq = 10\n",
    "ne2kpci": "",              # Realtek RTL8029AS
    "rtl8139c+": "",            # Realtek RTL8139C+
    "i82557": "",               # Intel PRO/100
    "i82558": "",               # Intel PRO/100+
}


def free_port():
    """A TCP port nothing is listening on now (SLiRP binds 0.0.0.0)."""
    s = socket.socket()
    s.bind(("0.0.0.0", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def parse_forwards(specs):
    """--net-fwd [HOST:]GUEST items -> [(host or None, guest)]."""
    out = []
    for spec in specs:
        host, _, guest = spec.rpartition(":")
        out.append((int(host) if host else None, int(guest)))
    return out


def net_config(card, forwards):
    """[Network] and SLiRP port forwarding sections; forwards [(host, guest)],
    host may be a placeholder string (run.py --emit-config)."""
    text = "\n[Network]\nnet_01_card = %s\nnet_01_net_type = slirp\n" % card
    if NICS[card]:
        text += "\n" + NICS[card]
    if forwards:
        text += "\n[SLiRP Port Forwarding #1]\n"
        for i, (host, guest) in enumerate(forwards):
            text += "%d_protocol = tcp\n%d_external = %s\n%d_internal = %d\n" % (i, i, host, i, guest)
    return text


def com2_config(path):
    """COM2 on 86Box's named-pipe device, which opens a character device
    (here the bridge's pty) directly."""
    return "\n[Named Pipe (COM) #2]\npath = %s\n" % path


class Com2Bridge(threading.Thread):
    """A pty for 86Box's COM2 and a TCP port on 127.0.0.1 relaying to it.
    Everything the guest sends is also appended to `log`."""

    def __init__(self, log):
        super().__init__(daemon=True)
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)
        tty.setraw(self.master)
        self.path = os.ttyname(self.slave)
        self.lsock = socket.socket()
        self.lsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.lsock.bind(("127.0.0.1", 0))
        self.lsock.listen(1)
        self.port = self.lsock.getsockname()[1]
        self.log = open(log, "wb")
        self.conn = None
        self.stopping = False

    def run(self):
        while not self.stopping:
            fds = [self.master, self.lsock] + ([self.conn] if self.conn else [])
            try:
                ready, _, _ = select.select(fds, [], [], 0.2)
            except (OSError, ValueError):
                break
            for f in ready:
                if f is self.lsock:
                    c, _ = self.lsock.accept()
                    if self.conn:
                        self.conn.close()
                    self.conn = c
                elif f == self.master:
                    try:
                        data = os.read(self.master, 4096)
                    except OSError:
                        data = b""
                    if data:
                        self.log.write(data)
                        self.log.flush()
                        if self.conn:
                            try:
                                self.conn.sendall(data)
                            except OSError:
                                self.conn = None
                else:
                    data = self.conn.recv(4096)
                    if data:
                        os.write(self.master, data)
                    else:
                        self.conn.close()
                        self.conn = None

    def stop(self):
        self.stopping = True
        self.join(timeout=2)
        for fd in (self.master, self.slave):
            try:
                os.close(fd)
            except OSError:
                pass
        self.lsock.close()
        self.log.close()


def parse_tcp_send(spec):
    """ANCHOR|TARGET|TEXT, TARGET being com2 or net:GUESTPORT."""
    anchor, target, text = spec.split("|", 2)
    return {"anchor": anchor, "target": target, "text": text}


class TcpSend(threading.Thread):
    """One --tcp-send step: connect to the host port, send TEXT and a CR LF,
    then collect what comes back until the peer closes or `quiet` seconds
    pass without data. The reply goes to `out`."""

    def __init__(self, port, text, out, delay=3.0, quiet=5.0):
        super().__init__(daemon=True)
        self.port, self.text, self.out = port, text, out
        self.delay, self.quiet = delay, quiet
        self.result = {"port": port, "sent": text, "got": 0, "error": None}

    def run(self):
        time.sleep(self.delay)
        got = b""
        try:
            s = socket.create_connection(("127.0.0.1", self.port), timeout=10)
            s.sendall(self.text.encode("latin-1") + b"\r\n")
            s.settimeout(self.quiet)
            while True:
                try:
                    data = s.recv(4096)
                except socket.timeout:
                    break
                if not data:
                    break
                got += data
            s.close()
        except OSError as e:
            self.result["error"] = str(e)
        open(self.out, "wb").write(got)
        self.result["got"] = len(got)

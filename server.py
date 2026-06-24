#!/usr/bin/env python3
"""Local test server for RePaint network protocol.

Usage:
    python3 server.py              # default port 33789
    python3 server.py --port 9999
"""

import argparse
import socket
import struct
import threading

# ── Protocol constants (mirrors net_protocol.h dStates) ───────────────────

NET_PORT = 33789
NET_HEADER_SIZE = 5

sdNONE, sdAction, sdSegment, sdSection, sdLAction, sdGetName, sdGetPass, sdGetMsg, \
    sdLogin, sdLoginS, sdUserAdded, sdUserDel, sdUserStat, sdRoomJoin, \
    sdRoomPart, sdRoomCreate, sdAuth, sdReg, sdLog, sdFriendSeek, \
    sdFriendAdd, sdFriendDel, sdLock, sdUnlock, sdGetImg, sdReqImg, \
    sdConfirmImg, sdPeopleResults, sdMisc, sdFAIL, sdSTOP = range(31)

atLogin, atRegister, atRename, atSTOP = range(4)


def pack_head(hid: int, size: int) -> bytes:
    return struct.pack("!BI", hid & 0xFF, size)


def unpack_head(data: bytes) -> tuple:
    return struct.unpack("!BI", data)


def pack_string(s: str) -> bytes:
    encoded = s.encode("utf-8")
    if len(encoded) > 65535:
        encoded = encoded[:65535]
    return struct.pack("!H", len(encoded)) + encoded


def unpack_string(data: bytes) -> str:
    slen = struct.unpack("!H", data[:2])[0]
    return data[2:2 + slen].decode("utf-8", errors="replace")


# ── UserState serialization ───────────────────────────────────────────────

def pack_userstate(uname: str, ustate: int) -> bytes:
    return bytes([ustate & 0xFF]) + pack_string(uname)


def unpack_userstate(data: bytes) -> tuple:
    ustate = data[0]
    uname = unpack_string(data[1:])
    return uname, ustate


# ── Auth serialization ────────────────────────────────────────────────────

def unpack_auth(data: bytes) -> tuple:
    atype = data[0]
    off = 1
    uname = unpack_string(data[off:])
    off += 2 + len(uname.encode("utf-8"))
    upass = unpack_string(data[off:])
    return atype, uname, upass


# ── Client handler ────────────────────────────────────────────────────────

class Client:
    def __init__(self, conn: socket.socket, addr):
        self.conn = conn
        self.addr = addr
        self.name = f"User_{addr[1]}"
        self.registered = False
        self.buf = b""

    def send_packet(self, hid: int, payload: bytes = b""):
        try:
            self.conn.sendall(pack_head(hid, len(payload)) + payload)
        except OSError:
            pass

    def close(self):
        try:
            self.conn.close()
        except OSError:
            pass


class Server:
    def __init__(self, host="0.0.0.0", port=NET_PORT):
        self.host = host
        self.port = port
        self.clients: list[Client] = []
        self.lock = threading.Lock()

    def start(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((self.host, self.port))
        sock.listen(16)
        print(f"[server] listening on {self.host}:{self.port}")

        while True:
            conn, addr = sock.accept()
            print(f"[server] new connection from {addr}")
            client = Client(conn, addr)
            t = threading.Thread(target=self.handle_client, args=(client,), daemon=True)
            t.start()

    def handle_client(self, client: Client):
        # send login prompt
        client.send_packet(sdLogin, pack_string("Welcome to local test server"))

        buf = b""
        while True:
            try:
                data = client.conn.recv(4096)
            except OSError:
                data = b""
            if not data:
                break
            buf += data

            while len(buf) >= NET_HEADER_SIZE:
                hid, size = unpack_head(buf[:NET_HEADER_SIZE])
                total = NET_HEADER_SIZE + size
                if len(buf) < total:
                    break
                payload = buf[NET_HEADER_SIZE:total]
                buf = buf[total:]

                self.dispatch(client, hid, payload)

        self.remove_client(client)
        print(f"[server] {client.name} disconnected")

    def dispatch(self, sender: Client, hid: int, payload: bytes):
        with self.lock:
            if hid == sdAuth:
                atype, uname, upass = unpack_auth(payload)
                sender.name = uname
                sender.registered = True

                # check for duplicate name
                dup = any(c is not sender and c.registered and c.name == uname for c in self.clients)
                if dup:
                    sender.send_packet(sdFAIL, pack_string("Name already taken"))
                    print(f"[server] rejected duplicate name: {uname}")
                    return

                self.clients.append(sender)
                sender.send_packet(sdLoginS, pack_string(uname))
                print(f"[server] registered: {uname}")

                # broadcast new user to everyone else
                for c in self.clients:
                    if c is not sender and c.registered:
                        c.send_packet(sdUserAdded, pack_string(uname))
                        # also send existing users to the new arrival
                        sender.send_packet(sdUserAdded, pack_string(c.name))
                        # send user status
                        c.send_packet(sdUserStat, pack_userstate(uname, 3))  # suOn
                        sender.send_packet(sdUserStat, pack_userstate(c.name, 3))
                return

            if not sender.registered:
                return

            # relay messages to all other clients
            if hid in (sdAction, sdSegment, sdSection, sdGetMsg):
                for c in self.clients:
                    if c is not sender and c.registered:
                        c.send_packet(hid, payload)

            elif hid == sdLAction:
                wrapped = pack_string(sender.name) + payload
                for c in self.clients:
                    if c.registered:
                        c.send_packet(hid, wrapped)

            elif hid == sdUserStat:
                for c in self.clients:
                    if c is not sender and c.registered:
                        c.send_packet(sdUserStat, payload)

            elif hid == sdRoomCreate:
                # auto-room all users into one room
                for c in self.clients:
                    if c is not sender and c.registered:
                        c.send_packet(sdRoomJoin, pack_string(sender.name))
                        sender.send_packet(sdRoomJoin, pack_string(c.name))
                sender.send_packet(sdRoomJoin, pack_string(sender.name))

            elif hid in (sdRoomJoin, sdRoomPart):
                for c in self.clients:
                    if c is not sender and c.registered:
                        c.send_packet(hid, payload)

            elif hid == sdGetMsg:
                for c in self.clients:
                    if c is not sender and c.registered:
                        c.send_packet(sdGetMsg, payload)

            elif hid == sdFriendSeek:
                for c in self.clients:
                    if c.registered:
                        # send back list of all users
                        names = ",".join(cl.name for cl in self.clients if cl.registered)
                        sender.send_packet(sdPeopleResults, pack_string(names))
                        break

            else:
                # unknown/unsupported: echo back as fail
                sender.send_packet(sdFAIL, pack_string("Unsupported message"))

    def remove_client(self, client: Client):
        with self.lock:
            if client in self.clients:
                self.clients.remove(client)
            name = client.name
            client.close()
            # notify others
            if name:
                for c in self.clients:
                    if c.registered:
                        c.send_packet(sdUserDel, pack_string(name))


def main():
    parser = argparse.ArgumentParser(description="RePaint local test server")
    parser.add_argument("--port", type=int, default=NET_PORT, help=f"port (default: {NET_PORT})")
    args = parser.parse_args()

    srv = Server(port=args.port)
    try:
        srv.start()
    except KeyboardInterrupt:
        print("\n[server] shutting down")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Run a console command on a BO1 zombies dedicated server over rcon and print its output.

    tools/server_rcon.py HOST[:PORT] PASSWORD COMMAND...      # PORT defaults to 3074
    tools/server_rcon.py 192.168.1.20 secret zm_god all 1

The server needs a non-empty rcon_password. It answers with out-of-band "print" packets holding the command's console
output (the admin mod's "admin: ..." replies come later, in the server's console and games_mp.log).
Exit status: 0 when the server answered, 1 when nothing came back.
"""
import argparse
import socket
import sys
import time

OOB = b"\xff\xff\xff\xff"


def parse_target(target):
    host, _, port = target.partition(":")
    return host, int(port) if port else 3074


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("target")
    parser.add_argument("password")
    parser.add_argument("command", nargs="+")
    parser.add_argument("--timeout", type=float, default=1.5)
    args = parser.parse_args()

    host, port = parse_target(args.target)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(args.timeout)
    sock.sendto(OOB + b"rcon " + args.password.encode() + b" " + " ".join(args.command).encode(), (host, port))
    answered = False
    end = time.time() + args.timeout
    while time.time() < end:
        try:
            data, _ = sock.recvfrom(65536)
        except socket.timeout:
            break
        if not data.startswith(OOB):
            continue
        answered = True
        text = data[len(OOB):].decode("latin-1")
        # BO1 puts a 0x01 byte between the out-of-band header and "print"
        text = text.lstrip("\x01")
        if text.startswith("print"):
            text = text[len("print"):].lstrip("\r\n")
        sys.stdout.write(text)
    return 0 if answered else 1


if __name__ == "__main__":
    sys.exit(main())

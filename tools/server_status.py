#!/usr/bin/env python3
"""Ask a BO1 zombies dedicated server for its status (getstatus) and print it.

    tools/server_status.py HOST[:PORT] [--timeout SECONDS]      # PORT defaults to 3074

The server answers an out-of-band "statusResponse" packet: the serverinfo string (\\key\\value...), then one line per
connected client: number, score, ping, client state (3 connected, 4 loading, 5 active), quoted name.
Exit status: 0 when the server answered, 1 on timeout or a malformed reply.
"""
import argparse
import socket
import sys

OOB = b"\xff\xff\xff\xff"
STATES = {3: "connected", 4: "loading", 5: "active"}


def parse_target(target):
    host, _, port = target.partition(":")
    return host, int(port) if port else 3074


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("target")
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args()

    host, port = parse_target(args.target)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(args.timeout)
    sock.sendto(OOB + b"getstatus", (host, port))
    try:
        data, _ = sock.recvfrom(65536)
    except socket.timeout:
        print(f"no answer from {host}:{port} within {args.timeout} s")
        return 1

    if not data.startswith(OOB + b"statusResponse\n"):
        print(f"unexpected reply: {data[:64]!r}")
        return 1
    lines = data[len(OOB):].decode("latin-1").rstrip("\x00").split("\n")
    fields = lines[1].split("\\")[1:]
    info = dict(zip(fields[0::2], fields[1::2]))
    for key in ("sv_hostname", "mapname", "g_gametype", "sv_maxclients", "fs_game"):
        if key in info:
            print(f"{key}: {info[key]}")
    players = [line for line in lines[2:] if line]
    print(f"players: {len(players)}")
    for line in players:
        num, score, ping, state, name = line.split(" ", 4)
        print(f"  #{num} {name.strip(chr(34))}: {score} points, ping {ping}, {STATES.get(int(state), state)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""
Find an LG webOS TV on your network and ask it which app is in the foreground.

Usage:
    py -m pip install aiowebostv
    py lg_current_app.py                  # finds the TV automatically
    py lg_current_app.py --ip 192.168.1.50
    py lg_current_app.py --key <CLIENT_KEY>   # skip the pairing prompt next time
    py lg_current_app.py --list           # also list every installed app

The first run shows a pairing prompt on the TV: accept it with the remote.
Run it while the blank screen is showing.
"""
import argparse
import asyncio
import ipaddress
import re
import socket
import sys
import time
import urllib.request

SSDP_ADDR = ("239.255.255.250", 1900)
SSDP_TARGETS = [
    "urn:lge-com:service:webos-second-screen:1",
    "urn:dial-multiscreen-org:service:dial:1",
]


# ---------------------------------------------------------------- discovery

def ssdp_discover(timeout=4.0):
    """Ask the network who is an LG TV. Returns {ip: friendly_name_or_None}."""
    found = {}
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
    sock.settimeout(0.5)
    try:
        for st in SSDP_TARGETS:
            msg = (
                "M-SEARCH * HTTP/1.1\r\n"
                f"HOST: {SSDP_ADDR[0]}:{SSDP_ADDR[1]}\r\n"
                'MAN: "ssdp:discover"\r\n'
                "MX: 2\r\n"
                f"ST: {st}\r\n\r\n"
            ).encode()
            for _ in range(2):
                sock.sendto(msg, SSDP_ADDR)

        end = time.time() + timeout
        while time.time() < end:
            try:
                data, (ip, _) = sock.recvfrom(4096)
            except socket.timeout:
                continue
            text = data.decode(errors="ignore")
            looks_lg = "lge" in text.lower() or "webos" in text.lower()
            if not looks_lg:
                continue
            location = re.search(r"(?im)^location:\s*(\S+)", text)
            if ip not in found:
                found[ip] = _friendly_name(location.group(1)) if location else None
    finally:
        sock.close()
    return found


def _friendly_name(url):
    try:
        with urllib.request.urlopen(url, timeout=2) as resp:
            xml = resp.read().decode(errors="ignore")
        m = re.search(r"<friendlyName>(.*?)</friendlyName>", xml)
        return m.group(1) if m else None
    except Exception:
        return None


def local_ip():
    """The laptop's own LAN address (no packets are actually sent)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    finally:
        s.close()


async def port_scan(timeout=0.6):
    """Fallback: look for webOS control ports (3000/3001) across the local /24."""
    me = local_ip()
    net = ipaddress.ip_network(f"{me}/24", strict=False)
    sem = asyncio.Semaphore(128)

    async def probe(ip):
        async with sem:
            for port in (3001, 3000):
                try:
                    _, w = await asyncio.wait_for(
                        asyncio.open_connection(str(ip), port), timeout
                    )
                    w.close()
                    return str(ip)
                except Exception:
                    pass
        return None

    print(f"  Scanning {net} for ports 3000/3001 (takes ~10s)...")
    results = await asyncio.gather(*(probe(ip) for ip in net.hosts() if str(ip) != me))
    return [r for r in results if r]


def find_tv():
    print("Searching for LG TVs on your network...")
    tvs = ssdp_discover()
    if not tvs:
        print("  No reply to the network search; trying a port scan instead.")
        tvs = {ip: None for ip in asyncio.run(port_scan())}
    if not tvs:
        print("\nNo TV found. Check the TV is on and on the same network as this")
        print("computer, or pass its address with --ip (TV: Settings > Network).")
        sys.exit(1)

    tvs = list(tvs.items())
    for i, (ip, name) in enumerate(tvs, 1):
        print(f"  [{i}] {ip}  {name or ''}")
    if len(tvs) == 1:
        return tvs[0][0]
    choice = input("Which TV? Enter the number: ").strip()
    return tvs[int(choice) - 1][0]


# ---------------------------------------------------------------- TV query

def _get(client, name):
    # Newer aiowebostv keeps state under client.tv_state; older versions expose it directly.
    state = getattr(client, "tv_state", None)
    if state is not None and hasattr(state, name):
        return getattr(state, name)
    return getattr(client, name, None)


async def query_tv(host, key, list_apps):
    from aiowebostv import WebOsClient

    client = WebOsClient(host, key)
    print(f"\nConnecting to {host} - accept the prompt on the TV if one appears...")
    await client.connect()
    try:
        print(f"Client key (save this for --key next time): {client.client_key}")

        app_id = _get(client, "current_app_id")
        apps = _get(client, "apps") or {}
        app = apps.get(app_id, {}) if app_id else {}

        print()
        print(f"Foreground app id : {app_id or '(none reported)'}")
        if app:
            print(f"Foreground app    : {app.get('title', '?')}")

        inputs = _get(client, "inputs") or {}
        if app_id in inputs:
            print(f"(This is an input: {inputs[app_id].get('label', app_id)})")

        if list_apps and apps:
            print("\nInstalled apps:")
            for aid, info in sorted(apps.items()):
                print(f"  {aid:45} {info.get('title', '')}")
    finally:
        await client.disconnect()


def main():
    p = argparse.ArgumentParser(description="Show the foreground app on an LG webOS TV.")
    p.add_argument("--ip", help="TV IP address (found automatically if omitted)")
    p.add_argument("--key", help="client key printed by an earlier run")
    p.add_argument("--list", action="store_true", help="also list installed apps")
    args = p.parse_args()

    try:
        import aiowebostv  # noqa: F401
    except ImportError:
        print("Missing library. Run:  py -m pip install aiowebostv")
        sys.exit(1)

    host = args.ip or find_tv()
    asyncio.run(query_tv(host, args.key, args.list))


if __name__ == "__main__":
    main()

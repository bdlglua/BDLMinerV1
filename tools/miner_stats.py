#!/usr/bin/env python3
"""Anonymous usage count: how many miners run each version, on which board.

Once a day each miner downloads ping-<board>.txt of its own version's release
(settings: "count", on by default), and GitHub counts the downloads of every
release file. So for each version and board:

  miner-days   all the pings so far (one per miner and day)
  active       pings per day since the previous run of this script: about
               the number of miners running it now (needs a day or more
               between two runs to mean much)

    python3 tools/miner_stats.py

The previous counts are kept in ~/.cache/bdlminer_stats.json.
"""
import json
import math
import os
import re
import sys
import time
import tempfile
import urllib.request

REPO = "bdlglua/BDLMinerV1"
STATE = os.path.expanduser("~/.cache/bdlminer_stats.json")


def version_order(key):
    """Keep version components separate from the textual tie-breaker."""
    version = key.split()[0]
    parts = tuple(int(x) for x in re.findall(r"\d+", version))
    return parts, key


def releases():
    out, page = [], 1
    while True:
        url = f"https://api.github.com/repos/{REPO}/releases?per_page=100&page={page}"
        req = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json"})
        with urllib.request.urlopen(req, timeout=30) as response:
            batch = json.load(response)
        out += batch
        if len(batch) < 100:
            return out
        page += 1


def main():
    counts = {}  # "version board" -> pings
    for r in releases():
        for a in r["assets"]:
            name = a["name"]
            if name.startswith("ping-") and name.endswith(".txt"):
                counts[f"{r['tag_name'].lstrip('v')} {name[5:-4]}"] = a["download_count"]

    previous = {}
    if os.path.exists(STATE):
        try:
            with open(STATE) as saved:
                previous = json.load(saved)
            if (not isinstance(previous, dict) or
                    not isinstance(previous.get("time"), (int, float)) or
                    not math.isfinite(previous["time"]) or
                    not isinstance(previous.get("counts"), dict) or
                    any(not isinstance(n, int) or n < 0 for n in previous["counts"].values())):
                raise ValueError("Invalid usage snapshot")
        except (ValueError, TypeError):
            print("Previous usage snapshot is invalid; starting a new baseline.", file=sys.stderr)
            previous = {}
    now = time.time()
    days = (now - previous["time"]) / 86400 if previous else 0

    if not counts:
        print("no release has ping files yet (they come with 1.0.8 and later)")
    else:
        print(f"{'version':10} {'board':22} {'miner-days':>10} {'active':>8}")
        total_active = 0.0
        comparable = 0
        for key in sorted(counts, key=version_order, reverse=True):
            version, board = key.split()
            active = ""
            before = previous.get("counts", {}).get(key)
            # A new or re-uploaded asset has no comparable baseline. Its full
            # historical count is not a day's activity, nor a negative rate.
            if days > 0 and before is not None and counts[key] >= before:
                comparable += 1
                rate = (counts[key] - before) / days
                total_active += rate
                active = f"{rate:.1f}"
            print(f"{version:10} {board:22} {counts[key]:>10} {active:>8}")
        if days > 0 and comparable:
            print(f"\nabout {total_active:.0f} active miners (over the last {days:.1f} days)")
        else:
            print("\nrun again in a day or more to see how many miners are active")

    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", dir=os.path.dirname(STATE), delete=False) as saved:
            temporary = saved.name
            json.dump({"time": now, "counts": counts}, saved)
        os.replace(temporary, STATE)
    finally:
        if temporary is not None and os.path.exists(temporary):
            os.remove(temporary)


if __name__ == "__main__":
    main()

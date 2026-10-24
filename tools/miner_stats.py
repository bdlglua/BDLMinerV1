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
import os
import time
import urllib.request

REPO = "bdlglua/BDLMinerV1"
STATE = os.path.expanduser("~/.cache/bdlminer_stats.json")


def releases():
    out, page = [], 1
    while True:
        url = f"https://api.github.com/repos/{REPO}/releases?per_page=100&page={page}"
        req = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json"})
        batch = json.load(urllib.request.urlopen(req, timeout=30))
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
        previous = json.load(open(STATE))
    now = time.time()
    days = (now - previous["time"]) / 86400 if previous else 0

    if not counts:
        print("no release has ping files yet (they come with 1.0.8 and later)")
    else:
        print(f"{'version':10} {'board':22} {'miner-days':>10} {'active':>8}")
        total_active = 0.0
        for key in sorted(counts, key=lambda k: [int(x) for x in k.split()[0].split(".")] + [k], reverse=True):
            version, board = key.split()
            active = ""
            if days > 0:
                rate = (counts[key] - previous["counts"].get(key, 0)) / days
                total_active += rate
                active = f"{rate:.1f}"
            print(f"{version:10} {board:22} {counts[key]:>10} {active:>8}")
        if days > 0:
            print(f"\nabout {total_active:.0f} active miners (over the last {days:.1f} days)")
        else:
            print("\nrun again in a day or more to see how many miners are active")

    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    json.dump({"time": now, "counts": counts}, open(STATE, "w"))


if __name__ == "__main__":
    main()

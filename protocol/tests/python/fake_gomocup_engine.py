"""Tiny fake Gomocup engine for adapter tests.

Reads INFO/START/BOARD/TURN/END from stdin, plays a deterministic
strategy: in response to BOARD or TURN, finds the first empty cell
in row-major order and prints its coordinates. Acks ``START`` with
``OK``.
"""

from __future__ import annotations

import sys


def _next_empty(occupied: set[tuple[int, int]], size: int) -> tuple[int, int]:
    for y in range(size):
        for x in range(size):
            if (x, y) not in occupied:
                return x, y
    raise RuntimeError("no empty cell")


def main() -> int:
    size = 15
    occupied: set[tuple[int, int]] = set()
    for raw in sys.stdin:
        line = raw.strip()
        if not line:
            continue
        upper = line.upper()
        if upper.startswith("INFO"):
            continue
        if upper.startswith("START"):
            parts = line.split()
            if len(parts) >= 2:
                size = int(parts[1])
            occupied = set()
            print("OK", flush=True)
        elif upper == "BOARD":
            occupied = set()
            for inner in sys.stdin:
                inner = inner.strip()
                if inner.upper() == "DONE":
                    break
                xs, ys, _who = inner.split(",")
                occupied.add((int(xs), int(ys)))
            x, y = _next_empty(occupied, size)
            occupied.add((x, y))
            print(f"{x},{y}", flush=True)
        elif upper.startswith("TURN"):
            xy = line.split(None, 1)[1]
            xs, ys = xy.split(",")
            occupied.add((int(xs), int(ys)))
            x, y = _next_empty(occupied, size)
            occupied.add((x, y))
            print(f"{x},{y}", flush=True)
        elif upper == "END":
            return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())

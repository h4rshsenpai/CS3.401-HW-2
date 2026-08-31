#!/usr/bin/env python3
import sys
import random

def main():
    V = int(sys.argv[1])
    E = int(sys.argv[2])
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 42
    random.seed(seed)

    max_possible = V * (V - 1) // 2
    E = min(E, max_possible)

    edges = set()
    while len(edges) < E:
        u = random.randint(0, V - 1)
        v = random.randint(0, V - 1)
        if u == v:
            continue
        if u > v:
            u, v = v, u
        edges.add((u, v))

    print(V, len(edges))
    out = []
    for (u, v) in edges:
        out.append(f"{u} {v}")
    print("\n".join(out))

if __name__ == "__main__":
    main()

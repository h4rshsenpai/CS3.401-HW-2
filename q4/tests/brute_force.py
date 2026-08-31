#!/usr/bin/env python3
"""Reference (non-distributed) triangle counter used only to verify the
MPI program's output on small/medium random graphs."""
import sys

def main():
    data = sys.stdin.read().split()
    idx = 0
    V = int(data[idx]); idx += 1
    E = int(data[idx]); idx += 1
    adj = [set() for _ in range(V)]
    edges = []
    for _ in range(E):
        u = int(data[idx]); idx += 1
        v = int(data[idx]); idx += 1
        if u == v:
            continue
        edges.append((u, v))
        adj[u].add(v)
        adj[v].add(u)

    count = 0
    for (u, v) in edges:
        # count common neighbours w > both u and v is wrong in general;
        # simplest correct way: intersection size, each triangle counted
        # 3 times (once per edge), so divide by 3 at the end.
        count += len(adj[u] & adj[v])
    print(count // 3)

if __name__ == "__main__":
    main()

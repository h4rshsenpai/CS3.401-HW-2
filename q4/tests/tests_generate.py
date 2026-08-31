import os

def write_graph(filename, V, edges):
    with open(filename, 'w') as f:
        f.write(f"{V} {len(edges)}\n")
        for u, v in edges:
            f.write(f"{u} {v}\n")
    print(f"Generated {filename}: V={V}, E={len(edges)}")

def gen_complete_graph():
    # K_1000: Every node connected to every other node.
    # Max double-counting stress test.
    # Triangles = 1000 C 3 = 166,167,000
    V = 1000
    edges = []
    for i in range(V):
        for j in range(i + 1, V):
            edges.append((i, j))
    write_graph("test_complete.txt", V, edges)
    print("  -> Expected Triangles: 166167000\n")

def gen_star_graph():
    # 1 Hub node connected to 99,999 isolated nodes.
    # Tests load imbalance and zero-triangle logic.
    # Triangles = 0
    V = 100000
    edges = [(0, i) for i in range(1, V)]
    write_graph("test_star.txt", V, edges)
    print("  -> Expected Triangles: 0\n")

def gen_wheel_graph():
    # 1 Hub node connected to a perimeter of nodes forming a ring.
    # Triangles = V - 1
    V = 100000
    edges = []
    # Hub connections
    for i in range(1, V):
        edges.append((0, i))
    # Perimeter cycle
    for i in range(1, V - 1):
        edges.append((i, i + 1))
    edges.append((V - 1, 1))
    write_graph("test_wheel.txt", V, edges)
    print(f"  -> Expected Triangles: {V - 1}\n")

def gen_disjoint_triangles():
    # 33,333 separate triangles floating in space.
    # Tests distributed summation without overlaps.
    # Triangles = 33,333
    triangles = 33333
    V = triangles * 3
    edges = []
    for i in range(triangles):
        base = i * 3
        edges.append((base, base + 1))
        edges.append((base + 1, base + 2))
        edges.append((base + 2, base))
    write_graph("test_disjoint.txt", V, edges)
    print(f"  -> Expected Triangles: {triangles}\n")

if __name__ == "__main__":
    gen_complete_graph()
    gen_star_graph()
    gen_wheel_graph()
    gen_disjoint_triangles()
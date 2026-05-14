#!/usr/bin/env python3
import random
import sys
from typing import List, Tuple, Set

def generate_random_weight(min_w: int = 1, max_w: int = 1000) -> int:
    return random.randint(min_w, max_w)

def write_graph_to_file(edges: List[Tuple[int, int, int]], filename: str, create_undirected: bool = True):
    written_edges = set()
    
    with open(filename, 'w') as f:
        for u, v, w in edges:
            a, b = (u, v) if u < v else (v, u)
            
            if (a, b) not in written_edges:
                f.write(f"{a} {b} {w}\n")
                written_edges.add((a, b))
    
    print(f"✓ Wrote {len(written_edges)} edges to {filename}")

def read_graph_from_file(filename: str) -> List[Tuple[int, int, int]]:
    edges = []
    with open(filename, 'r') as f:
        for line in f:
            if line.strip():
                u, v, w = map(int, line.split())
                edges.append((u, v, w))
    return edges

def graph_stats(edges: List[Tuple[int, int, int]]) -> dict:
    vertices = set()
    for u, v, _ in edges:
        vertices.add(u)
        vertices.add(v)
    
    return {
        'vertices': len(vertices),
        'edges': len(edges),
        'min_vertex': min(vertices),
        'max_vertex': max(vertices)
    }

def print_statistics(filename: str):
    edges = read_graph_from_file(filename)
    stats = graph_stats(edges)
    
    print(f"\n=== Graph Statistics: {filename} ===")
    print(f"Vertices: {stats['vertices']}")
    print(f"Edges: {stats['edges']}")
    print(f"Vertex ID range: [{stats['min_vertex']}, {stats['max_vertex']}]")
    density = 2 * stats['edges'] / (stats['vertices'] * (stats['vertices'] - 1))
    print(f"Density: {density:.4f}")

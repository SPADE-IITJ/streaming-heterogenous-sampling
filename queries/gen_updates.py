#!/usr/bin/env python3
import random
import argparse
from typing import List, Tuple
import os

def generate_update_stream(n_vertices: int, n_updates: int, 
                          graph_file: str = None, seed: int = None) -> List[Tuple[int, int, int]]:
    if seed is not None:
        random.seed(seed)
    
    updates = []
    
    existing_edges = set()
    if graph_file and os.path.exists(graph_file):
        with open(graph_file, 'r') as f:
            for line in f:
                if line.strip():
                    u, v, w = map(int, line.split())
                    existing_edges.add((min(u, v), max(u, v)))
    
    existing_edge_ratio = 0.4  
    
    for _ in range(n_updates):
        if existing_edges and random.random() < existing_edge_ratio:
            
            u, v = random.choice(list(existing_edges))
        else:
            
            u = random.randint(0, n_vertices - 1)
            v = random.randint(0, n_vertices - 1)
            
            while v == u:
                v = random.randint(0, n_vertices - 1)
            
            if u > v:
                u, v = v, u
        
        
        w = random.randint(1, 1000)
        updates.append((u, v, w))
    
    return updates

def write_updates_to_file(updates: List[Tuple[int, int, int]], filename: str):
    with open(filename, 'w') as f:
        for u, v, w in updates:
            f.write(f"{u} {v} {w}\n")
    print(f"✓ Wrote {len(updates)} updates to {filename}")

def main():
    parser = argparse.ArgumentParser(
        description="Generate streaming update sequences for MST algorithms"
    )
    parser.add_argument('--vertices', '-V', type=int, default=1000,
                        help='Number of vertices (default: 1000)')
    parser.add_argument('--updates', '-U', type=int, default=5000,
                        help='Number of updates to generate (default: 5000)')
    parser.add_argument('--input-graph', '-g', type=str, default=None,
                        help='Optional base graph file (for modification mix)')
    parser.add_argument('--output', '-o', type=str, default='updates.txt',
                        help='Output file (default: updates.txt)')
    parser.add_argument('--seed', '-s', type=int, default=None,
                        help='Random seed for reproducibility')
    
    args = parser.parse_args()
    
    print(f"Generating {args.updates} update edges for {args.vertices} vertices...")
    updates = generate_update_stream(args.vertices, args.updates, args.input_graph, args.seed)
    write_updates_to_file(updates, args.output)

if __name__ == '__main__':
    main()

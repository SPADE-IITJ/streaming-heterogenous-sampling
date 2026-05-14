#!/usr/bin/env python3
import random
import sys
import argparse
import math
from typing import List, Tuple, Set
from gen_utils import generate_random_weight, write_graph_to_file, print_statistics

def generate_watts_strogatz(n: int, k: int, p: float, seed: int = None) -> List[Tuple[int, int, int]]:
    if seed is not None:
        random.seed(seed)
    
    if k % 2 != 0:
        print("Warning: k should be even. Rounding down to nearest even number.")
        k = (k // 2) * 2
    
    edges_set: Set[Tuple[int, int]] = set()
    
    for i in range(n):
        for j in range(1, k // 2 + 1):
            neighbor = (i + j) % n
            edge = tuple(sorted([i, neighbor]))
            edges_set.add(edge)
    
    edges_to_rewire = []
    for edge in edges_set:
        if random.random() < p:
            edges_to_rewire.append(edge)
    
    for u, v in edges_to_rewire:
        while True:
            new_v = random.randint(0, n - 1)
            new_edge = tuple(sorted([u, new_v]))
            
            if new_v != u and new_v != v and new_edge not in edges_set:
                
                edges_set.discard((u, v))
                edges_set.add(new_edge)
                break
    
    edges = [(u, v, generate_random_weight()) for u, v in edges_set]
    
    return edges

def main():
    parser = argparse.ArgumentParser(
        description="Generate Watts-Strogatz small-world graph"
    )
    parser.add_argument('--vertices', '-V', type=int, default=1000,
                        help='Number of vertices (default: 1000)')
    parser.add_argument('--neighbors', '-k', type=int, default=6,
                        help='Nearest neighbors per vertex (default: 6, should be even)')
    parser.add_argument('--rewire-prob', '-p', type=float, default=0.3,
                        help='Rewiring probability 0-1 (default: 0.3)')
    parser.add_argument('--output', '-o', type=str, default='synthetic/ws_graph.txt',
                        help='Output file (default: synthetic/ws_graph.txt)')
    parser.add_argument('--seed', '-s', type=int, default=None,
                        help='Random seed for reproducibility')
    parser.add_argument('--stats', action='store_true', help='Print graph statistics')
    
    args = parser.parse_args()
    
    
    if not (0 <= args.rewire_prob <= 1):
        print("Error: rewire probability must be between 0 and 1")
        sys.exit(1)
    
    print(f"Generating Watts-Strogatz graph with {args.vertices} vertices, k={args.neighbors}, p={args.rewire_prob}...")
    edges = generate_watts_strogatz(args.vertices, args.neighbors, args.rewire_prob, args.seed)
    
    write_graph_to_file(edges, args.output)
    
    if args.stats:
        print_statistics(args.output)

if __name__ == '__main__':
    main()

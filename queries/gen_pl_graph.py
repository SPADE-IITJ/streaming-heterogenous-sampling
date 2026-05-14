#!/usr/bin/env python3
import random
import sys
import argparse
from typing import List, Tuple
from gen_utils import generate_random_weight, write_graph_to_file, print_statistics

def generate_barabasi_albert(n: int, m: int, seed: int = None) -> List[Tuple[int, int, int]]:
    if seed is not None:
        random.seed(seed)
    
    edges = []
      
    for i in range(m + 1):
        for j in range(i + 1, m + 1):
            w = generate_random_weight()
            edges.append((i, j, w))
        
    degrees = [0] * n
    for u, v, _ in edges:
        degrees[u] += 1
        degrees[v] += 1
     
    for new_vertex in range(m + 1, n):
        total_degree = sum(degrees[:new_vertex])
        
        targets = []
        for _ in range(m):
            r = random.randint(0, total_degree - 1)
            cumsum = 0
            for vertex in range(new_vertex):
                cumsum += degrees[vertex]
                if r < cumsum:
                    if vertex not in targets:
                        targets.append(vertex)
                    break
        
        for target in targets:
            w = generate_random_weight()
            edges.append((target, new_vertex, w))
            degrees[target] += 1
            degrees[new_vertex] += 1
    
    return edges

def main():
    parser = argparse.ArgumentParser(
        description="Generate Power-Law (scale-free) graph using Barabási-Albert model"
    )
    parser.add_argument('--vertices', '-V', type=int, default=1000,
                        help='Number of vertices (default: 1000)')
    parser.add_argument('--edges-per-vertex', '-m', type=int, default=5,
                        help='Edges to attach per new vertex (default: 5)')
    parser.add_argument('--output', '-o', type=str, default='synthetic/pl_graph.txt',
                        help='Output file (default: synthetic/pl_graph.txt)')
    parser.add_argument('--seed', '-s', type=int, default=None,
                        help='Random seed for reproducibility')
    parser.add_argument('--stats', action='store_true', help='Print graph statistics')
    
    args = parser.parse_args()
    
    print(f"Generating Power-Law graph with {args.vertices} vertices, m={args.edges_per_vertex}...")
    edges = generate_barabasi_albert(args.vertices, args.edges_per_vertex, args.seed)
    
    write_graph_to_file(edges, args.output)
    
    if args.stats:
        print_statistics(args.output)

if __name__ == '__main__':
    main()

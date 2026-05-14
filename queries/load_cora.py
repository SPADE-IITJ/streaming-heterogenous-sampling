#!/usr/bin/env python3
import os
import sys
import argparse
import random
from collections import defaultdict

def load_cora_from_ml_libraries():
    try:
        from torch_geometric.datasets import Planetoid
        print("Loading Cora from PyTorch Geometric...")
        cora = Planetoid(root="/tmp", name='Cora')
        data = cora[0]
        edges = data.edge_index.t().numpy()
        return [(int(u), int(v)) for u, v in edges]
    except Exception as e:
        print(f"  PyTorch Geometric failed: {e}")
    
    
    try:
        import dgl
        print("Loading Cora from DGL...")
        cora = dgl.data.CoraGraphDataset()
        g = cora[0]
        edges = g.edges()
        return [(int(u), int(v)) for u, v in zip(edges[0], edges[1])]
    except Exception as e:
        print(f"  DGL failed: {e}")
    
    try:
        import networkx as nx
        print("Loading Cora from NetworkX...")
        cora = nx.read_gml("/path/to/cora.gml")  
        edges = list(cora.edges())
        return edges
    except Exception as e:
        print(f"  NetworkX failed: {e}")
    
    try:
        print("Looking for local cora.gml or cora.edges file...")
        for fname in ["cora.gml", "cora.edges", "synthetic/cora.gml"]:
            if os.path.exists(fname):
                print(f"Found local file: {fname}")
                if fname.endswith(".gml"):
                    import networkx as nx
                    g = nx.read_gml(fname)
                    return list(g.edges())
                else:
                    
                    edges = []
                    with open(fname) as f:
                        for line in f:
                            parts = line.strip().split()
                            if len(parts) >= 2:
                                edges.append((int(parts[0]), int(parts[1])))
                    return edges
    except Exception as e:
        print(f"  Local file search failed: {e}")
    
    return None

def download_cora_manual(cache_dir="synthetic"):
    print("\n" + "=" * 60)
    print("MANUAL DOWNLOAD REQUIRED")
    print("=" * 60)
    print("""
The Cora dataset can be obtained from multiple sources:

Option 1 - ML Libraries (Recommended):
  pip install torch-geometric
  
  

Option 2 - Linqs Dataset:
  1. Visit: https://linqs.org/datasets/
  2. Download cora_v2.1.zip
  3. Extract and place in: {}
  4. Re-run with --input cora.gml

Option 3 - GitHub mirrored dataset:
  wget https://raw.githubusercontent.com/prletarian/cora-dataset/master/cora.gml
  mv cora.gml {}

After obtaining the file, re-run this script with:
  python3 load_cora.py --stats
""".format(cache_dir, cache_dir))
    print("=" * 60)
    sys.exit(1)

def load_cora_from_file(filepath):
    edges = []
    
    if filepath.endswith(".gml"):
        try:
            import networkx as nx
            print(f"Loading Cora from GML: {filepath}")
            g = nx.read_gml(filepath)
            edges = list(g.edges())
        except Exception as e:
            print(f"Failed to parse GML: {e}")
            sys.exit(1)
    else:
        print(f"Loading Cora from edge list: {filepath}")
        try:
            with open(filepath) as f:
                for line in f:
                    parts = line.strip().split()
                    if len(parts) >= 2:
                        u, v = int(parts[0]), int(parts[1])
                        edges.append((u, v))
        except Exception as e:
            print(f"Failed to parse file: {e}")
            sys.exit(1)
    
    return edges

def normalize_edges(edges):
    edge_set = set()
    node_map = {}
    next_id = 0
    
    for u, v in edges:
        
        if u > v:
            u, v = v, u
        
        
        if u not in node_map:
            node_map[u] = next_id
            next_id += 1
        if v not in node_map:
            node_map[v] = next_id
            next_id += 1
        
        edge_set.add((node_map[u], node_map[v]))
    
    return list(edge_set), len(node_map)

def assign_weights(edges, min_w=1, max_w=1000, seed=None):
    if seed is not None:
        random.seed(seed)
    
    weighted_edges = []
    for u, v in edges:
        w = random.randint(min_w, max_w)
        weighted_edges.append((u, v, w))
    
    return weighted_edges

def write_graph(edges, output_file):
    with open(output_file, 'w') as f:
        for u, v, w in edges:
            f.write(f"{u} {v} {w}\n")

def print_stats(vertices, edges):
    print(f"\n=== Graph Statistics ===")
    print(f"Vertices: {vertices}")
    print(f"Edges:    {edges}")
    print(f"Density:  {2*edges / (vertices*(vertices-1)):.4f}")
    print(f"Avg Degree: {2*edges / vertices:.2f}")

def main():
    parser = argparse.ArgumentParser(
        description="Load Cora citation network dataset"
    )
    parser.add_argument(
        "--input", "-i",
        help="Input GML or edge list file (auto-downloads if not specified)"
    )
    parser.add_argument(
        "--output", "-o",
        default="synthetic/cora.txt",
        help="Output file (default: synthetic/cora.txt)"
    )
    parser.add_argument(
        "--cache-dir", "-c",
        default="synthetic",
        help="Cache directory (default: synthetic)"
    )
    parser.add_argument(
        "--min-weight",
        type=int,
        default=1,
        help="Minimum edge weight (default: 1)"
    )
    parser.add_argument(
        "--max-weight",
        type=int,
        default=1000,
        help="Maximum edge weight (default: 1000)"
    )
    parser.add_argument(
        "--seed",
        type=int,
        help="Random seed for weight assignment"
    )
    parser.add_argument(
        "--stats",
        action="store_true",
        help="Print graph statistics"
    )
    
    args = parser.parse_args()
    
    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    os.makedirs(args.cache_dir, exist_ok=True)
    
    print("Cora Citation Network Loader")
    print("=" * 40)
    
    if args.input:
        
        edges = load_cora_from_file(args.input)
    else:
        
        edges = load_cora_from_ml_libraries()
        if edges is None:
            download_cora_manual(args.cache_dir)
    
    print(f"Loaded initial edges: {len(edges)}")
    
    print("Normalizing and deduplicating...")
    edges, vertices = normalize_edges(edges)
    print(f"Normalized to {vertices} vertices, {len(edges)} edges")
    
    print(f"Assigning weights ({args.min_weight}-{args.max_weight})...")
    weighted_edges = assign_weights(
        edges,
        min_w=args.min_weight,
        max_w=args.max_weight,
        seed=args.seed
    )
    
    print(f"Writing to {args.output}...")
    write_graph(weighted_edges, args.output)
    print(f"Written {len(weighted_edges)} edges")
    
    if args.stats:
        print_stats(vertices, len(weighted_edges))
    
    print(f"\nDone! Graph saved to {args.output}")

if __name__ == "__main__":
    main()

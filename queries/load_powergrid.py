#!/usr/bin/env python3
import os
import sys
import gzip
import urllib.request
import argparse
from collections import defaultdict
import random

def download_powergrid(cache_dir="synthetic"):
    os.makedirs(cache_dir, exist_ok=True)
    
    gml_path = os.path.join(cache_dir, "power.gml")
    gz_path = os.path.join(cache_dir, "power.gml.gz") 
    
    if os.path.exists(gml_path):
        print(f"Found cached power grid at {gml_path}")
        return gml_path
    
    if not os.path.exists(gz_path):
        print("Downloading US Power Grid dataset from SNAP...")
        url = "https://snap.stanford.edu/data/power.gml.gz"
        try:
            urllib.request.urlretrieve(url, gz_path)
            print(f"Downloaded to {gz_path}")
        except Exception as e:
            print(f"Failed to download: {e}")
            print("  Alternative: Download manually from https://snap.stanford.edu/data/")
            sys.exit(1)
    
    try:
        with gzip.open(gz_path, 'rb') as f_in:
            with open(gml_path, 'wb') as f_out:
                f_out.writelines(f_in)
        print(f"Decompressed to {gml_path}")
        return gml_path
    except Exception as e:
        print(f"Failed to decompress: {e}")
        sys.exit(1)

def parse_gml(gml_path):
    edges = []
    node_ids = {}
    next_id = 0
    
    with open(gml_path, 'r') as f:
        in_edge = False
        edge_src = None
        edge_tgt = None
        
        for line in f:
            line = line.strip()

            if line.startswith('id '):
                node_label = int(line.split()[1])
                if node_label not in node_ids:
                    node_ids[node_label] = next_id
                    next_id += 1    
        
            if line.startswith('source '):
                edge_src = int(line.split()[1])
            elif line.startswith('target '):
                edge_tgt = int(line.split()[1])
                if edge_src is not None and edge_tgt is not None:
                    
                    u = node_ids.get(edge_src, next_id)
                    if u == next_id:
                        node_ids[edge_src] = next_id
                        next_id += 1

                    v = node_ids.get(edge_tgt, next_id)
                    if v == next_id:
                        node_ids[edge_tgt] = next_id
                        next_id += 1
                    
                    edges.append((u, v))
                    edge_src = None
                    edge_tgt = None
    
    return edges, len(node_ids)

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
        description="Load US Power Grid from SNAP dataset"
    )
    parser.add_argument(
        "--output", "-o",
        default="synthetic/powergrid.txt",
        help="Output file (default: synthetic/powergrid.txt)"
    )
    parser.add_argument(
        "--cache-dir", "-c",
        default="synthetic",
        help="Cache directory for downloads (default: synthetic)"
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
    
    print("US Power Grid Loader")
    print("=" * 40)
    
    gml_path = download_powergrid(args.cache_dir)
    print("\nParsing GML format...")
    edges, vertices = parse_gml(gml_path)
    print(f"Parsed {vertices} vertices, {len(edges)} edges")    
    
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

#ifndef UTILS_HPP
#define UTILS_HPP

#include <cstddef>
#include <cstdint>

#ifdef __has_include
  #if __has_include(<cuda.h>)
    #include <cuda.h>
    #include <cuda_runtime.h>
    #define CUDA_AVAILABLE 1
  #endif
#endif

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <tuple>
#include <utility>
#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <climits>
#include <set>
#include <numeric>
#include <functional>
#include <chrono>
#include <unordered_map>

using std::cout;
using std::endl;
using std::vector;
using std::string;
using std::tuple;
using std::set;
using std::function;
using std::unordered_map;

using std::sort;

using stream = std::ifstream;

#ifdef CUDA_AVAILABLE
#define CHECK_CUDA(call) { \
    cudaError_t err = call; \
    if(err != cudaSuccess){ \
        fprintf(stderr, "CUDA ERROR @ %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(err)); \
        exit(EXIT_FAILURE); \
    } \
}
#else
#define CHECK_CUDA(call) { }  
#endif

#ifdef CUDA_AVAILABLE
#define HOST_DEVICE __host__ __device__
#define GLOBAL __global__
#else
#define HOST_DEVICE
#define GLOBAL
#endif

#define UNPACK(u, v, w, edge){  \
    std::tie(u,v, w) = edge;    \
}

#define UNPACK_MST_RESULT(changed, removed, result) \
    std::tie(changed, removed) = result;

#define FILENAME "graph.txt"
#define UPDATES_FILENAME "updates.txt"

typedef int32_t vtx_t;
typedef int32_t weight_t;
typedef uint32_t bitmap_t;

#define NO_EDGE_SENTINEL -1

struct CanonicalEdge {
    vtx_t u, v;
    
    CanonicalEdge() : u(-1), v(-1) {}
    CanonicalEdge(vtx_t u_, vtx_t v_) {
        if (u_ == v_) { u = -1; v = -1; return; } 
        if (u_ < v_) { u = u_; v = v_; }
        else { u = v_; v = u_; }
    }
    
    bool is_valid() const { return u >= 0 && v >= 0; }
    
    bool operator==(const CanonicalEdge& other) const {
        return u == other.u && v == other.v;
    }
};

struct CanonicalEdgeHash {
    size_t operator()(const CanonicalEdge& e) const {
        // hash = (u + v) * (u + v + 1) / 2 + v
        size_t a = (size_t)e.u;
        size_t b = (size_t)e.v;
        return (a + b) * (a + b + 1) / 2 + b;
    }
};

struct Edge {
    vtx_t u, v;
    weight_t w;
};

class MSF_MembershipMap {
private:
    unordered_map<CanonicalEdge, weight_t, CanonicalEdgeHash> mst_edges;
    
    unordered_map<vtx_t, set<vtx_t>> adjacency;

public:
    MSF_MembershipMap() : mst_edges(), adjacency() {}
    
    bool insert(vtx_t u, vtx_t v, weight_t w) {
        CanonicalEdge ce(u, v);
        if (!ce.is_valid()) return false; 
        
        auto [it, inserted] = mst_edges.insert({ce, w});
        
        if (inserted) {
            adjacency[ce.u].insert(ce.v);
            adjacency[ce.v].insert(ce.u);
        }
        
        return inserted;
    }
    
    bool remove(vtx_t u, vtx_t v) {
        CanonicalEdge ce(u, v);
        if (!ce.is_valid()) return false;
        
        auto it = mst_edges.find(ce);
        if (it == mst_edges.end()) return false; 
        
        mst_edges.erase(it);
        
        adjacency[ce.u].erase(ce.v);
        adjacency[ce.v].erase(ce.u);
        
        if (adjacency[ce.u].empty()) adjacency.erase(ce.u);
        if (adjacency[ce.v].empty()) adjacency.erase(ce.v);
        
        return true;
    }
    
    bool contains(vtx_t u, vtx_t v) const {
        CanonicalEdge ce(u, v);
        if (!ce.is_valid()) return false;
        return mst_edges.find(ce) != mst_edges.end();
    }
    
    weight_t get_weight(vtx_t u, vtx_t v) const {
        CanonicalEdge ce(u, v);
        if (!ce.is_valid()) return NO_EDGE_SENTINEL;
        
        auto it = mst_edges.find(ce);
        return (it != mst_edges.end()) ? it->second : NO_EDGE_SENTINEL;
    }
    
    set<vtx_t> get_incident_edges(vtx_t v) const {
        auto it = adjacency.find(v);
        return (it != adjacency.end()) ? it->second : set<vtx_t>();
    }
    
    vector<Edge> get_all_edges() const {
        vector<Edge> edges;
        for (const auto& [ce, w] : mst_edges) {
            edges.push_back({ce.u, ce.v, w});
        }
        return edges;
    }

    size_t size() const {
        return mst_edges.size();
    }
    
    bool empty() const {
        return mst_edges.empty();
    }

    void clear() {
        mst_edges.clear();
        adjacency.clear();
    }

    void print_stats() const {
        cout << "MSF Membership Map Statistics:" << endl;
        cout << "  Total edges in MSF: " << mst_edges.size() << endl;
        cout << "  Vertices with incident edges: " << adjacency.size() << endl;
        
        double avg_degree = adjacency.empty() ? 0.0 : 2.0 * mst_edges.size() / adjacency.size();
        cout << "  Average degree in MSF: " << avg_degree << endl;
    }
};

struct CSR {
    vtx_t num_vertices;
    long long num_total_possible_edges;
    weight_t* d_weights; 
    CSR* d_self_ptr; 
};

struct DSU {
    vtx_t* d_parent;
};

struct GPU_Boruvka_Result {
    CSR adj_matrix;
    vector<Edge> mst_edges;
};

#define NOW std::chrono::high_resolution_clock::now()
#define NANOSECONDS std::chrono::duration_cast<std::chrono::nanoseconds>


HOST_DEVICE inline void device_aware_swap(vtx_t& a, vtx_t& b) {
    vtx_t temp = a;
    a = b;
    b = temp;
}

HOST_DEVICE inline long long get_edge_index(vtx_t u, vtx_t v, vtx_t V) {
    if (u == v) return -1; 
    if (u > v) device_aware_swap(u, v);
    return (long long)u * (2LL * V - (long long)u - 1) / 2 + v - u - 1;
}

inline vector<Edge> read_graph_from_file(const string& file_path, int& num_vertices, set<vtx_t>& vertices) {
    vector<Edge> edges;
    stream infile(file_path);
    if (!infile.is_open()) {
        fprintf(stderr, "Error opening file: %s\n", file_path.c_str());
        exit(EXIT_FAILURE);
    }
    vtx_t u, v, w;
    vtx_t max_vtx_id = -1;
    while (infile >> u >> v >> w) {
        vertices.insert(u);
        vertices.insert(v);
        edges.push_back({u, v, w});
        if (u > max_vtx_id) max_vtx_id = u;
        if (v > max_vtx_id) max_vtx_id = v;
    }
    infile.close();
    num_vertices = max_vtx_id + 1;
    cout << "Read " << edges.size() << " edges from " << file_path << ". Max vertex ID found: " << max_vtx_id << endl;
    return edges;
}

#ifdef CUDA_AVAILABLE

#ifdef __CUDACC__
GLOBAL void initialize_matrix_kernel(weight_t* d_weights, long long size) {
    long long idx = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < size) {
        d_weights[idx] = NO_EDGE_SENTINEL;
    }
}

GLOBAL void populate_matrix_kernel(Edge* d_edges, int E, vtx_t V, weight_t* d_weights) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= E) return;

    Edge edge = d_edges[idx];
    long long edge_idx = get_edge_index(edge.u, edge.v, V);
    if (edge_idx != -1) {
        d_weights[edge_idx] = edge.w;
    }
}
#else
GLOBAL void initialize_matrix_kernel(weight_t* d_weights, long long size);
GLOBAL void populate_matrix_kernel(Edge* d_edges, int E, vtx_t V, weight_t* d_weights);
#endif

inline CSR create_adj_matrix_on_gpu(const vector<Edge>& edges, vtx_t V, vtx_t E) {
    CSR adj_matrix;
    adj_matrix.num_vertices = V;
    adj_matrix.num_total_possible_edges = (long long)V * (V - 1) / 2;

    cout << "Allocating Triangular Adjacency Matrix on GPU ("
         << adj_matrix.num_total_possible_edges * sizeof(weight_t) / (1024.0 * 1024.0)
         << " MB)..." << endl;

    CHECK_CUDA(cudaMalloc(&adj_matrix.d_weights, adj_matrix.num_total_possible_edges * sizeof(weight_t)));

    int block_size = 256;
    long long num_blocks_init = (adj_matrix.num_total_possible_edges + block_size - 1) / block_size;
    initialize_matrix_kernel<<<num_blocks_init, block_size>>>(adj_matrix.d_weights, adj_matrix.num_total_possible_edges);
    CHECK_CUDA(cudaGetLastError());

    Edge* d_edges;
    CHECK_CUDA(cudaMalloc(&d_edges, E * sizeof(Edge)));
    CHECK_CUDA(cudaMemcpy(d_edges, edges.data(), E * sizeof(Edge), cudaMemcpyHostToDevice));

    int num_blocks_populate = (E + block_size - 1) / block_size;
    populate_matrix_kernel<<<num_blocks_populate, block_size>>>(d_edges, E, V, adj_matrix.d_weights);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    cudaFree(d_edges);

    CHECK_CUDA(cudaMalloc(&adj_matrix.d_self_ptr, sizeof(CSR)));
    CHECK_CUDA(cudaMemcpy(adj_matrix.d_self_ptr, &adj_matrix, sizeof(CSR), cudaMemcpyHostToDevice));

    cout << "Triangular Adjacency Matrix created on GPU." << endl;
    return adj_matrix;
}

#else

inline CSR create_adj_matrix_on_gpu(const vector<Edge>& edges, vtx_t V, vtx_t E) {
    cout << "GPU matrix creation not available in CPU-only build" << endl;
    CSR adj_matrix;
    adj_matrix.d_weights = nullptr;
    adj_matrix.d_self_ptr = nullptr;
    return adj_matrix;
}

#endif

#endif // UTILS_HPP

#ifndef FILTERS_HPP
#define FILTERS_HPP

#include "utils.hpp"
#include <thread>

#ifdef CUDA_AVAILABLE

#ifdef __CUDACC__
GLOBAL void set_is_edge_kernel(Edge* edges, int num_graph_edges, vtx_t V, bitmap_t* filter_bitmap) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= num_graph_edges) return;

    long long edge_idx = get_edge_index(edges[idx].u, edges[idx].v, V);
    long long word_idx = edge_idx / 16;
    int bit_pos = (edge_idx % 16) * 2;
    atomicOr(&filter_bitmap[word_idx], 1U << bit_pos); // IS_EDGE = 1
}

GLOBAL void set_in_mst_kernel(Edge* mst_edges, int num_mst_edges, vtx_t V, bitmap_t* filter_bitmap) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= num_mst_edges) return;

    long long edge_idx = get_edge_index(mst_edges[idx].u, mst_edges[idx].v, V);
    long long word_idx = edge_idx / 16;
    int bit_pos = (edge_idx % 16) * 2;

    bitmap_t mask = ~(3U << bit_pos); // Mask to clear the 2 bits
    atomicAnd(&filter_bitmap[word_idx], mask); // Clear old state
    atomicOr(&filter_bitmap[word_idx], 2U << bit_pos); // IN_MST = 2
}
#else
GLOBAL void set_is_edge_kernel(Edge* edges, int num_graph_edges, vtx_t V, bitmap_t* filter_bitmap);
GLOBAL void set_in_mst_kernel(Edge* mst_edges, int num_mst_edges, vtx_t V, bitmap_t* filter_bitmap);
#endif

class multi_filter {
private:
    bitmap_t* d_bitmap;
    long long num_words;

public:
    static constexpr bitmap_t NOT_EDGE = 0;
    static constexpr bitmap_t IS_EDGE = 1;
    static constexpr bitmap_t IN_MST  = 2;

    multi_filter(long long max_edges) {
        num_words = (max_edges + 15) / 16;
        CHECK_CUDA(cudaMalloc(&d_bitmap, num_words * sizeof(bitmap_t)));
        CHECK_CUDA(cudaMemset(d_bitmap, 0, num_words * sizeof(bitmap_t)));
    }

    ~multi_filter() {
        if (d_bitmap) {
            cudaFree(d_bitmap);
            d_bitmap = nullptr;
        }
    }

    bitmap_t* get_device_ptr() { return d_bitmap; }
    long long get_num_words() const { return num_words; }
};

inline multi_filter* populate_multifilter_parallel(const vector<Edge>& all_edges, const vector<Edge>& mst_edges, vtx_t V) {
    long long max_possible_edges = (long long)V * (V - 1) / 2;
    multi_filter* filter = new multi_filter(max_possible_edges);

    Edge* d_all_edges;
    Edge* d_mst_edges;
    CHECK_CUDA(cudaMalloc(&d_all_edges, all_edges.size() * sizeof(Edge)));
    CHECK_CUDA(cudaMalloc(&d_mst_edges, mst_edges.size() * sizeof(Edge)));
    CHECK_CUDA(cudaMemcpy(d_all_edges, all_edges.data(), all_edges.size() * sizeof(Edge), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_mst_edges, mst_edges.data(), mst_edges.size() * sizeof(Edge), cudaMemcpyHostToDevice));

    int block_size = 256;

    int num_blocks_all = (all_edges.size() + block_size - 1) / block_size;
    set_is_edge_kernel<<<num_blocks_all, block_size>>>(d_all_edges, all_edges.size(), V, filter->get_device_ptr());
    CHECK_CUDA(cudaGetLastError());

    if (!mst_edges.empty()) {
        int num_blocks_mst = (mst_edges.size() + block_size - 1) / block_size;
        set_in_mst_kernel<<<num_blocks_mst, block_size>>>(d_mst_edges, mst_edges.size(), V, filter->get_device_ptr());
        CHECK_CUDA(cudaGetLastError());
    }

    CHECK_CUDA(cudaDeviceSynchronize());

    cudaFree(d_all_edges);
    cudaFree(d_mst_edges);

    cout << "MultiFilter populated on GPU." << endl;
    return filter;
}

#else

class multi_filter {
public:
    static constexpr bitmap_t NOT_EDGE = 0;
    static constexpr bitmap_t IS_EDGE = 1;
    static constexpr bitmap_t IN_MST  = 2;

    multi_filter(long long max_edges) { }
    ~multi_filter() { }
    bitmap_t* get_device_ptr() { return nullptr; }
    long long get_num_words() const { return 0; }
};

inline multi_filter* populate_multifilter_parallel(const vector<Edge>& all_edges, const vector<Edge>& mst_edges, vtx_t V) {
    return new multi_filter((long long)V * (V - 1) / 2);
}

#endif

inline void populate_nodefilter(set<vtx_t>& vertices, vector<bitmap_t>& bitmap) {
    for(vtx_t v : vertices) {
        int word_idx = v / 32;
        int bit_pos = v % 32;
        bitmap[word_idx] |= (1u << bit_pos);
    }
}

inline int node_filter(vector<bitmap_t>& NodeFilter, vtx_t u, vtx_t v) {
    auto check_u = [&]() {
        int word_idx = u / 32;
        int bit_pos = u % 32;
        return (NodeFilter.at(word_idx) & (1u << bit_pos)) != 0;
    };

    auto check_v = [&]() {
        int word_idx = v / 32;
        int bit_pos = v % 32;
        return (NodeFilter.at(word_idx) & (1u << bit_pos)) != 0;
    };

    bool result_u, result_v;
    std::thread t1([&]() { result_u = check_u(); });
    std::thread t2([&]() { result_v = check_v(); });

    t1.join();
    t2.join();

    if (result_u && result_v) {
        return 0; // both old
    } else if (result_u || result_v) {
        return 1; // one new
    } else {
        return 2; // both new
    }
}

#endif // FILTERS_HPP

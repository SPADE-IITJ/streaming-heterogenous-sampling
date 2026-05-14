#ifndef GPU_KERNELS_HPP
#define GPU_KERNELS_HPP

#include "utils.hpp"

#ifdef CUDA_AVAILABLE

// dsu
__device__ vtx_t find_set_atomic(DSU* dsu, vtx_t node);
__device__ void unite_sets_atomic(DSU* dsu, vtx_t a, vtx_t b);

//kernels

__global__ void find_cheapest_edges_kernel(CSR* d_csr, DSU* d_dsu, 
                                           vtx_t* d_cheapest_neighbors, 
                                           weight_t* d_cheapest_weights);
__global__ void add_cheapest_edges_kernel(DSU* d_dsu, vtx_t V, 
                                          vtx_t* d_cheapest_neighbors);

#endif  // CUDA_AVAILABLE

vector<Edge> run_parallel_boruvka(CSR& adj_matrix);

#endif // GPU_KERNELS_HPP

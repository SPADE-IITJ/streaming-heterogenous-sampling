#include "../include/utils.hpp"
#include "../include/filters.hpp"
#include <algorithm>
#include <set>

__device__ vtx_t find_set_atomic(DSU* dsu, vtx_t node) {
    vtx_t curr = node;
    while (dsu->d_parent[curr] != curr) {
        curr = dsu->d_parent[curr];
    }
    return curr;
}

__device__ void unite_sets_atomic(DSU* dsu, vtx_t a, vtx_t b) {
    vtx_t root_a = find_set_atomic(dsu, a);
    vtx_t root_b = find_set_atomic(dsu, b);
    while (root_a != root_b) {
        if (root_a > root_b) device_aware_swap(root_a, root_b);
        vtx_t old_parent = atomicCAS(&dsu->d_parent[root_b], root_b, root_a);
        if (old_parent == root_b) return;
        root_a = find_set_atomic(dsu, root_a);
        root_b = find_set_atomic(dsu, old_parent);
    }
}

__global__ void find_cheapest_edges_kernel(CSR* d_csr, DSU* d_dsu, 
                                           vtx_t* d_cheapest_neighbors, 
                                           weight_t* d_cheapest_weights) {
    vtx_t u = blockIdx.x * blockDim.x + threadIdx.x;
    if (u >= d_csr->num_vertices) return;

    weight_t min_weight = INT_MAX;
    vtx_t min_edge_neighbor = -1;
    vtx_t my_component = find_set_atomic(d_dsu, u);
    vtx_t V = d_csr->num_vertices;

    // O(V) neighbor iteration for the triangular adjacency matrix
    for (vtx_t v = 0; v < V; ++v) {
        if (u == v) continue;

        long long edge_idx = get_edge_index(u, v, V);
        weight_t weight = d_csr->d_weights[edge_idx];

        // Check if edge exists and connects to different component
        if (weight != NO_EDGE_SENTINEL) {
            if (my_component != find_set_atomic(d_dsu, v)) {
                if (weight < min_weight || (weight == min_weight && v < min_edge_neighbor)) {
                    min_weight = weight;
                    min_edge_neighbor = v;
                }
            }
        }
    }

    d_cheapest_neighbors[u] = min_edge_neighbor;
    if (min_edge_neighbor != -1) {
        d_cheapest_weights[u] = min_weight;
    }
}

__global__ void add_cheapest_edges_kernel(DSU* d_dsu, vtx_t V, 
                                          vtx_t* d_cheapest_neighbors) {
    vtx_t node = blockIdx.x * blockDim.x + threadIdx.x;
    if (node >= V) return;

    vtx_t neighbor = d_cheapest_neighbors[node];
    if (neighbor != -1 && node < neighbor) {
        unite_sets_atomic(d_dsu, node, neighbor);
    }
}

vector<Edge> run_parallel_boruvka(CSR& adj_matrix) {
    vtx_t V = adj_matrix.num_vertices;
    int block_size = 256;
    int num_blocks = (V + block_size - 1) / block_size;

    DSU dsu;
    CHECK_CUDA(cudaMalloc(&dsu.d_parent, V * sizeof(vtx_t)));
    DSU* d_dsu_ptr;
    CHECK_CUDA(cudaMalloc(&d_dsu_ptr, sizeof(DSU)));
    CHECK_CUDA(cudaMemcpy(d_dsu_ptr, &dsu, sizeof(DSU), cudaMemcpyHostToDevice));

    vtx_t* d_cheapest_neighbors;
    weight_t* d_cheapest_weights;
    CHECK_CUDA(cudaMalloc(&d_cheapest_neighbors, V * sizeof(vtx_t)));
    CHECK_CUDA(cudaMalloc(&d_cheapest_weights, V * sizeof(weight_t)));

    vector<vtx_t> h_parent(V);
    std::iota(h_parent.begin(), h_parent.end(), 0);
    CHECK_CUDA(cudaMemcpy(dsu.d_parent, h_parent.data(), V * sizeof(vtx_t), cudaMemcpyHostToDevice));

    vector<Edge> h_mst_edges_candidates;
    vector<vtx_t> h_cheapest_neighbors(V);
    vector<weight_t> h_cheapest_weights(V);

    int num_components = V;
    while (num_components > 1) {
        CHECK_CUDA(cudaMemset(d_cheapest_neighbors, -1, V * sizeof(vtx_t)));

        find_cheapest_edges_kernel<<<num_blocks, block_size>>>(adj_matrix.d_self_ptr, d_dsu_ptr, 
                                                               d_cheapest_neighbors, d_cheapest_weights);
        CHECK_CUDA(cudaGetLastError());

        add_cheapest_edges_kernel<<<num_blocks, block_size>>>(d_dsu_ptr, V, d_cheapest_neighbors);
        CHECK_CUDA(cudaGetLastError());
        CHECK_CUDA(cudaDeviceSynchronize());

        CHECK_CUDA(cudaMemcpy(h_cheapest_neighbors.data(), d_cheapest_neighbors, 
                              V * sizeof(vtx_t), cudaMemcpyDeviceToHost));
        CHECK_CUDA(cudaMemcpy(h_cheapest_weights.data(), d_cheapest_weights, 
                              V * sizeof(weight_t), cudaMemcpyDeviceToHost));

        for (vtx_t i = 0; i < V; ++i) {
            if (h_cheapest_neighbors[i] != -1) {
                h_mst_edges_candidates.push_back({i, h_cheapest_neighbors[i], h_cheapest_weights[i]});
            }
        }

        CHECK_CUDA(cudaMemcpy(h_parent.data(), dsu.d_parent, V * sizeof(vtx_t), cudaMemcpyDeviceToHost));
        std::set<vtx_t> roots;
        for (vtx_t i = 0; i < V; ++i) {
            vtx_t curr = i;
            while(h_parent[curr] != curr) curr = h_parent[curr];
            roots.insert(curr);
        }

        if (roots.size() >= num_components) break;
        num_components = roots.size();
        cout << "  Boruvka iteration: " << num_components << " components remaining." << endl;
    }

    sort(h_mst_edges_candidates.begin(), h_mst_edges_candidates.end(), 
         [](const Edge& a, const Edge& b){ return a.w < b.w; });

    vector<Edge> final_mst;
    vector<vtx_t> host_dsu_parent(V);
    std::iota(host_dsu_parent.begin(), host_dsu_parent.end(), 0);

    auto find_set_host = [&](vtx_t i) -> vtx_t {
        if (host_dsu_parent[i] == i) return i;
        return host_dsu_parent[i] = find_set_host(host_dsu_parent[i]);
    };
    
    auto unite_sets_host = [&](vtx_t i, vtx_t j) {
        vtx_t root_i = find_set_host(i);
        vtx_t root_j = find_set_host(j);
        if (root_i != root_j) host_dsu_parent[root_i] = root_j;
    };

    for (const auto& edge : h_mst_edges_candidates) {
        if (find_set_host(edge.u) != find_set_host(edge.v)) {
            unite_sets_host(edge.u, edge.v);
            final_mst.push_back(edge);
        }
    }

    cudaFree(dsu.d_parent);
    cudaFree(d_dsu_ptr);
    cudaFree(d_cheapest_neighbors);
    cudaFree(d_cheapest_weights);

    cout << "Boruvka's algorithm finished. Found " << final_mst.size() << " MST edges." << endl;
    return final_mst;
}

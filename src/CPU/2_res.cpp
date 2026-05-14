#include "../include/utils.hpp"
#include "../include/lct.hpp"
#include "../include/filters.hpp"
#include "../include/reservoir.hpp"
#include "../include/gpu_kernels.hpp"

#include <chrono>
#include <set>
#include <queue>

class RES_Algorithm {
private:
    vtx_t V;
    MSF_MembershipMap membership_map;  
    link_cut_tree lct;
    vector<bitmap_t> h_MultiFilter;
    vector<weight_t> h_weights;
    
    UpdationReservoir R_U;  
    int B_max;              
    int tau_ms;             
    std::chrono::high_resolution_clock::time_point timer_start;

public:
    RES_Algorithm(vtx_t num_vertices,
                  const vector<bitmap_t>& filter,
                  const vector<weight_t>& weights,
                  int batch_size = 1000,
                  int time_threshold = 100)
        : V(num_vertices), membership_map(),
          lct(num_vertices + (num_vertices / 4), &membership_map),
          h_MultiFilter(filter), h_weights(weights),
          B_max(batch_size), tau_ms(time_threshold) {
        timer_start = NOW;
    }

    MSF_MembershipMap& get_membership_map() {
        return membership_map;
    }

    void insert_into_reservoir(vtx_t u, vtx_t v, weight_t w) {
        if (u > v) std::swap(u, v);
        R_U[{u, v}] = w;  // Overwrites if edge already in reservoir
    }

    bool should_trigger_batch() {
        bool size_trigger = (int)R_U.size() >= B_max;
        
        auto now = NOW;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - timer_start);
        bool time_trigger = elapsed.count() >= tau_ms;
        
        return size_trigger || time_trigger;
    }

    void process_batch() {
        cout << "Processing batch of " << R_U.size() << " updates..." << endl;
        
        vector<Edge> batch_edges;
        for (const auto& [edge_key, weight] : R_U) {
            batch_edges.push_back({std::get<0>(edge_key), std::get<1>(edge_key), weight});
        }

        for (const auto& edge : batch_edges) {
            vtx_t u = edge.u, v = edge.v;
            weight_t w = edge.w;
            
            if (u > v) std::swap(u, v);
            
            long long edge_idx = get_edge_index(u, v, V);
            if (edge_idx == -1) continue;
            
            weight_t old_w = h_weights[edge_idx];
            h_weights[edge_idx] = w;

            long long word_idx = edge_idx / 16;
            int bit_pos = (edge_idx % 16) * 2;
            
            if (membership_map.contains(u, v)) {
                weight_t mst_w = membership_map.get_weight(u, v);
            } else {
                bool changed;
                Edge removed_edge;
                UNPACK_MST_RESULT(changed, removed_edge, maintain_mst_with_lct(lct, u, v, w));

                if (changed) {
                    bitmap_t mask = ~(3U << bit_pos);
                    h_MultiFilter[word_idx] = (h_MultiFilter[word_idx] & mask) | (multi_filter::IN_MST << bit_pos);

                    if (removed_edge.u != -1) {
                        long long rem_edge_idx = get_edge_index(removed_edge.u, removed_edge.v, V);
                        if(rem_edge_idx != -1) {
                            long long rem_word_idx = rem_edge_idx / 16;
                            int rem_bit_pos = (rem_edge_idx % 16) * 2;
                            mask = ~(3U << rem_bit_pos);
                            h_MultiFilter[rem_word_idx] = (h_MultiFilter[rem_word_idx] & mask) | (multi_filter::IS_EDGE << rem_bit_pos);
                        }
                    }
                }
            }
        }

        R_U.clear();
        timer_start = NOW;
    }

    void process_update(vtx_t u, vtx_t v, weight_t w) {
        insert_into_reservoir(u, v, w);
        
        if (should_trigger_batch()) {
            process_batch();
        }
    }

    void flush() {
        if (!R_U.empty()) {
            cout << "Flushing final batch of " << R_U.size() << " updates" << endl;
            process_batch();
        }
    }

    int get_reservoir_size() const {
        return R_U.size();
    }
};

int smash_res(int argc, char* argv[]) {
    cout << "\n=== SMAsh-RES: Reservoir-based Dynamic MST ===" << endl;

    int V = 0;
    set<vtx_t> vertices;
    vector<Edge> all_edges = read_graph_from_file(FILENAME, V, vertices);
    int E = all_edges.size();
    
    if (V == 0 || E == 0) {
        cout << "Graph is empty or has no vertices. Exiting." << endl;
        return 0;
    }

    auto start = NOW;
    
    CSR csr = create_adj_matrix_on_gpu(all_edges, V, E);
    auto end = NOW;
    auto duration_matrix = NANOSECONDS(end - start).count() / 1e6;

    start = NOW;
    
    vector<Edge> mst_edges = run_parallel_boruvka(csr);
    end = NOW;
    auto duration_boruvka = NANOSECONDS(end - start).count() / 1e6;

    start = NOW;
    
    link_cut_tree lct = build_lct_from_mst(mst_edges, V);
    end = NOW;
    auto duration_lct = NANOSECONDS(end - start).count() / 1e6;

    start = NOW;
    
    multi_filter* filter = populate_multifilter_parallel(all_edges, mst_edges, V);
    end = NOW;
    auto duration_filter = NANOSECONDS(end - start).count() / 1e6;

    long long num_total_possible_edges = (long long)V * (V - 1) / 2;
    vector<weight_t> h_weights(num_total_possible_edges);
    CHECK_CUDA(cudaMemcpy(h_weights.data(), csr.d_weights, 
                          num_total_possible_edges * sizeof(weight_t), 
                          cudaMemcpyDeviceToHost));

    long long num_words = filter->get_num_words();
    vector<bitmap_t> h_MultiFilter(num_words);
    CHECK_CUDA(cudaMemcpy(h_MultiFilter.data(), filter->get_device_ptr(), 
                          num_words * sizeof(bitmap_t), 
                          cudaMemcpyDeviceToHost));

    cout << "\n--- Processing Updates (RES with Batching) ---" << endl;
    
    int B_max = 100;      
    int tau_ms = 50;      
    RES_Algorithm res(V, h_MultiFilter, h_weights, B_max, tau_ms);

    stream updates(UPDATES_FILENAME);
    vtx_t u, v;
    weight_t w;
    int update_count = 0;

    start = NOW;
    
    while(updates >> u >> v >> w) {
        if (u > v) std::swap(u, v);
        res.process_update(u, v, w);
        update_count++;
    }
    
    res.flush();
    
    end = NOW;
    auto duration_updates = NANOSECONDS(end - start).count() / 1e6;

    delete filter;
#ifdef CUDA_AVAILABLE
    cudaFree(csr.d_weights);
    cudaFree(csr.d_self_ptr);
#endif

    // Print timing results
    cout << "\nSMASH-RES" << endl;
    cout << "PROCESSED " << update_count << " UPDATES IN " << duration_updates << " ms" << endl;
    cout << "AVG/UPDATE:  " << (duration_updates / update_count) * 1000 << " μs" << endl;
    cout << "BATCH SIZE:      " << B_max << " | TIME THRESHOLD: " << tau_ms << " ms" << endl;
    cout << "GRAPH: G(V=" << V << ", E=" << E << ")" << endl;

    return 0;
}

#ifdef RES_MAIN
int main(int argc, char* argv[]) {
    return smash_res(argc, argv);
}
#endif

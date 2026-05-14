#include "../include/utils.hpp"
#include "../include/lct.hpp"
#include "../include/filters.hpp"
#include "../include/reservoir.hpp"
#include "../include/gpu_kernels.hpp"

#include <chrono>
#include <set>

enum UpdateOp {
    AUG,  // add edge 
    PRU,  // remove edge 
    MUT   // mutate edge
};

struct UpdateEdge {
    vtx_t u, v;
    weight_t w_old, w_new;
    UpdateOp op;
};

class PDS_Algorithm {
private:
    vtx_t V;
    MSF_MembershipMap membership_map;  
    link_cut_tree lct;
    vector<bitmap_t> h_MultiFilter;
    vector<weight_t> h_weights;

public:
    PDS_Algorithm(vtx_t num_vertices, 
                  const vector<bitmap_t>& filter,
                  const vector<weight_t>& weights)
        : V(num_vertices), membership_map(), 
          lct(num_vertices + (num_vertices / 4), &membership_map),
          h_MultiFilter(filter), h_weights(weights) {}
    
    MSF_MembershipMap& get_membership_map() {
        return membership_map;
    }

    void handle_aug(vtx_t u, vtx_t v, weight_t w) {
        if (u > v) std::swap(u, v);
        
        if (membership_map.contains(u, v)) {
            weight_t current_w = membership_map.get_weight(u, v);
            if (w < current_w) {
                lct.cut(u);
                lct.link(u, v, w);
                
                update_edge_filter(u, v, multi_filter::IN_MST);
            }
        } else if (lct.connected(u, v)) {
            Edge e_max = lct.get_path_max_edge(u, v);
            
            if (w < e_max.w) {
                lct.cut(e_max.u);
                lct.link(u, v, w);
                
                update_edge_filter(u, v, multi_filter::IN_MST);
                update_edge_filter(e_max.u, e_max.v, multi_filter::IS_EDGE);
            }
        } else {
            lct.link(u, v, w);
            update_edge_filter(u, v, multi_filter::IN_MST);
        }
    }

    void handle_pru(vtx_t u, vtx_t v) {
        if (u > v) std::swap(u, v);
        
        long long edge_idx = get_edge_index(u, v, V);
        if (edge_idx == -1) return;
        
        bool in_mst = membership_map.contains(u, v);
        
        long long word_idx = edge_idx / 16;
        int bit_pos = (edge_idx % 16) * 2;
        bitmap_t mask = ~(3U << bit_pos);
        h_MultiFilter[word_idx] = (h_MultiFilter[word_idx] & mask);
        
        if (in_mst) {
            lct.cut(u);
            
            // TODO: FindReplacementEdge(L, G, E, T, δ)
        }
    }

    void handle_mut(vtx_t u, vtx_t v, weight_t w_old, weight_t w_new) {
        if (u > v) std::swap(u, v);
        
        long long edge_idx = get_edge_index(u, v, V);
        if (edge_idx == -1) return;
        
        long long word_idx = edge_idx / 16;
        int bit_pos = (edge_idx % 16) * 2;
        h_weights[edge_idx] = w_new;
        
        auto state = (h_MultiFilter[word_idx] >> bit_pos) & 3;
        
        if (state == multi_filter::IN_MST && w_new > w_old) {
            handle_pru(u, v);
            handle_aug(u, v, w_new);
        }
        else {
            handle_aug(u, v, w_new);
        }
    }

    void process_update(const UpdateEdge& update) {
        switch(update.op) {
            case AUG:
                handle_aug(update.u, update.v, update.w_new);
                break;
            case PRU:
                handle_pru(update.u, update.v);
                break;
            case MUT:
                handle_mut(update.u, update.v, update.w_old, update.w_new);
                break;
        }
    }

    void process_batch(const vector<UpdateEdge>& updates) {
        for (const auto& update : updates) {
            process_update(update);
        }
    }

private:
    void update_edge_filter(vtx_t u, vtx_t v, bitmap_t state) {
        if (u > v) std::swap(u, v);
        long long edge_idx = get_edge_index(u, v, V);
        if (edge_idx == -1) return;
        
        long long word_idx = edge_idx / 16;
        int bit_pos = (edge_idx % 16) * 2;
        bitmap_t mask = ~(3U << bit_pos);
        h_MultiFilter[word_idx] = (h_MultiFilter[word_idx] & mask) | (state << bit_pos);
    }
};

int smash_pds(int argc, char* argv[]) {
    int V = 0;
    set<vtx_t> vertices;
    vector<Edge> all_edges = read_graph_from_file(FILENAME, V, vertices);
    int E = all_edges.size();
    
    if (V == 0 || E == 0) {
        cout << "EMPTY GRAPH / EXITING" << endl;
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

    
    PDS_Algorithm pds(V, h_MultiFilter, h_weights);

    stream updates(UPDATES_FILENAME);
    vtx_t u, v;
    weight_t w;
    int update_count = 0;

    start = NOW;
    
    while(updates >> u >> v >> w) {
        if (u > v) std::swap(u, v);
        
        UpdateEdge update = {u, v, 0, w, AUG};
        pds.process_update(update);
        update_count++;
    }
    
    end = NOW;
    auto duration_updates = NANOSECONDS(end - start).count() / 1e6;

    delete filter;
#ifdef CUDA_AVAILABLE
    cudaFree(csr.d_weights);
    cudaFree(csr.d_self_ptr);
#endif

    return 0;
}

#ifdef PDS_MAIN
int main(int argc, char* argv[]) {
    return smash_pds(argc, argv);
}
#endif

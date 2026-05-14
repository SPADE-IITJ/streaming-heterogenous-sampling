#include "../include/utils.hpp"
#include "../include/lct.hpp"
#include "../include/filters.hpp"
#include "../include/reservoir.hpp"
#include "../include/gpu_kernels.hpp"

#include <chrono>
#include <set>
#include <map>
#include <queue>

struct ReservoirGraph {
    std::set<vtx_t> V;                                    
    std::set<Edge, std::function<bool(const Edge&, const Edge&)>> E;  
    link_cut_tree* lct;                                  

    ReservoirGraph()
        : E([](const Edge& a, const Edge& b) {
            if (a.u != b.u) return a.u < b.u;
            return a.v < b.v;
          }) {
        lct = nullptr;
    }

    void add_vertex(vtx_t v) {
        V.insert(v);
    }

    void add_edge(const Edge& e) {
        E.insert(e);
    }
};


class FRESH_Algorithm {
private:
    vtx_t V_main;
    MSF_MembershipMap membership_map_main;      
    MSF_MembershipMap membership_map_reservoir; 
    link_cut_tree lct_main;
    vector<bitmap_t> h_MultiFilter;
    vector<weight_t> h_weights;
    std::set<vtx_t> V_main_set;
    
    ReservoirGraph reservoir;

public:
    FRESH_Algorithm(vtx_t num_vertices,
                    const vector<bitmap_t>& filter,
                    const vector<weight_t>& weights,
                    const std::set<vtx_t>& main_vertices)
        : V_main(num_vertices), membership_map_main(), membership_map_reservoir(),
          lct_main(num_vertices + (num_vertices / 4), &membership_map_main),
          h_MultiFilter(filter), h_weights(weights), V_main_set(main_vertices) {
        reservoir.lct = new link_cut_tree(num_vertices * 2, &membership_map_reservoir);
    }

    ~FRESH_Algorithm() {
        if (reservoir.lct) delete reservoir.lct;
    }
    
    MSF_MembershipMap& get_main_membership_map() {
        return membership_map_main;
    }
    
    MSF_MembershipMap& get_reservoir_membership_map() {
        return membership_map_reservoir;
    }

    bool in_main(vtx_t v) {
        return V_main_set.count(v) > 0;
    }

    bool in_reservoir(vtx_t v) {
        return reservoir.V.count(v) > 0;
    }

    void case1__both_in_main(vtx_t u, vtx_t v, weight_t w) {        
        if (u > v) std::swap(u, v);
        
        if (membership_map_main.contains(u, v)) {
            weight_t current_w = membership_map_main.get_weight(u, v);
            if (w < current_w) {
                lct_main.cut(u);
                lct_main.link(u, v, w);
                update_main_filter(u, v, multi_filter::IN_MST);
            }
        } else if (lct_main.connected(u, v)) {
            Edge e_max = lct_main.get_path_max_edge(u, v);
            if (w < e_max.w) {
                lct_main.cut(e_max.u);
                lct_main.link(u, v, w);
                update_main_filter(u, v, multi_filter::IN_MST);
                update_main_filter(e_max.u, e_max.v, multi_filter::IS_EDGE);
            }
        } else {
            lct_main.link(u, v, w);
            update_main_filter(u, v, multi_filter::IN_MST);
        }
    }

    void case2__bridge_edge(vtx_t u, vtx_t v, weight_t w) {        
        if (u > v) std::swap(u, v);
        
        vtx_t v_main = in_main(u) ? u : v;
        vtx_t v_new = in_main(u) ? v : u;
        
        std::set<vtx_t> component;
        find_component_in_reservoir(v_new, component);
        
        for (vtx_t cv : component) {
            V_main_set.insert(cv);
        }
        
        if (membership_map_main.contains(u, v)) {
            weight_t current_w = membership_map_main.get_weight(u, v);
            if (w < current_w) {
                lct_main.cut(u);
                lct_main.link(u, v, w);
            }
        } else {
            lct_main.link(u, v, w);
            update_main_filter(u, v, multi_filter::IN_MST);
        }
    }

    void case3__both_in_reservoir(vtx_t u, vtx_t v, weight_t w) {        
        if (u > v) std::swap(u, v);
        
        reservoir.add_vertex(u);
        reservoir.add_vertex(v);
        reservoir.add_edge({u, v, w});
        
        if (membership_map_reservoir.contains(u, v)) {
            weight_t current_w = membership_map_reservoir.get_weight(u, v);
            if (w < current_w) {
                reservoir.lct->cut(u);
                reservoir.lct->link(u, v, w);
            }
        } else if (reservoir.lct->connected(u, v)) {
            Edge e_max = reservoir.lct->get_path_max_edge(u, v);
            if (w < e_max.w) {
                reservoir.lct->cut(e_max.u);
                reservoir.lct->link(u, v, w);
            }
        } else {
            reservoir.lct->link(u, v, w);
        }
    }

    void process_update(vtx_t u, vtx_t v, weight_t w) {
        if (u > v) std::swap(u, v);
        
        bool u_in_main = in_main(u);
        bool v_in_main = in_main(v);
        bool u_in_res = in_reservoir(u);
        bool v_in_res = in_reservoir(v);
        
        if (u_in_main && v_in_main) {
            case1__both_in_main(u, v, w);
        }
        else if ((u_in_main || v_in_main) && (u_in_res || v_in_res)) {
            case2__bridge_edge(u, v, w);
        }
        else {
            if (!u_in_res) reservoir.add_vertex(u);
            if (!v_in_res) reservoir.add_vertex(v);
            case3__both_in_reservoir(u, v, w);
        }
    }

private:
    void find_component_in_reservoir(vtx_t start, std::set<vtx_t>& component) {
        std::queue<vtx_t> q;
        std::set<vtx_t> visited;
        q.push(start);
        visited.insert(start);
        
        while (!q.empty()) {
            vtx_t u = q.front();
            q.pop();
            component.insert(u);
            
            for (const auto& e : reservoir.E) {
                vtx_t neighbor = -1;
                if (e.u == u) neighbor = e.v;
                else if (e.v == u) neighbor = e.u;
                
                if (neighbor != -1 && !visited.count(neighbor)) {
                    visited.insert(neighbor);
                    q.push(neighbor);
                }
            }
        }
    }

    void update_main_filter(vtx_t u, vtx_t v, bitmap_t state) {
        if (u > v) std::swap(u, v);
        long long edge_idx = get_edge_index(u, v, V_main);
        if (edge_idx == -1) return;
        
        long long word_idx = edge_idx / 16;
        int bit_pos = (edge_idx % 16) * 2;
        bitmap_t mask = ~(3U << bit_pos);
        h_MultiFilter[word_idx] = (h_MultiFilter[word_idx] & mask) | (state << bit_pos);
    }
};

int main_fresh(int argc, char* argv[]) {
    cout << "\nSMASH-FRESH" << endl;

    int V = 0;
    set<vtx_t> vertices;
    vector<Edge> all_edges = read_graph_from_file(FILENAME, V, vertices);
    int E = all_edges.size();
    
    if (V == 0 || E == 0) {
        cout << "Graph is empty or has no vertices. Exiting." << endl;
        return 0;
    }

    auto start = NOW;
    
    GPU_Boruvka_Result gpu_result = run_parallel_boruvka_gpu_wrapper(all_edges, V);
    CSR csr = gpu_result.adj_matrix;
    vector<Edge> mst_edges = gpu_result.mst_edges;
    auto end = NOW;
    auto duration_matrix = NANOSECONDS(end - start).count() / 1e6;

    start = NOW;
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

    
    FRESH_Algorithm fresh(V, h_MultiFilter, h_weights, vertices);

    stream updates(UPDATES_FILENAME);
    vtx_t u, v;
    weight_t w;
    int update_count = 0;

    start = NOW;
    
    while(updates >> u >> v >> w) {
        if (u > v) std::swap(u, v);
        fresh.process_update(u, v, w);
        update_count++;
    }
    
    end = NOW;
    auto duration_updates = NANOSECONDS(end - start).count() / 1e6;

    delete filter;
#ifdef CUDA_AVAILABLE
    cudaFree(csr.d_weights);
    cudaFree(csr.d_self_ptr);
#endif

    cout << "\nSMASH-FRESH" << endl;
    cout << "PROCESSED " << update_count << " UPDATES " << duration_updates << " ms" << endl;
    cout << "AVG/UPDATE:  " << (duration_updates / update_count) * 1000 << " μs" << endl;
    cout << "GRAPH: G(V=" << V << ", E=" << E << ")" << endl;

    return 0;
}

// Add this to enable standalone compilation
#ifdef FRESH_MAIN
int main(int argc, char* argv[]) {
    return main_fresh(argc, argv);
}
#endif

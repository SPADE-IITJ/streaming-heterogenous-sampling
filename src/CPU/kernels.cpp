#include "../../include/utils.hpp"


#ifndef CUDA_AVAILABLE

extern vector<Edge> read_graph_from_file(const string& filename, int& V, std::set<vtx_t>& vertices);

vector<Edge> run_parallel_boruvka(CSR& adj_matrix) {
    cout << "\n--- Using CPU-based Kruskal's Algorithm for MST Construction ---" << endl;
    
    int V = 0;
    std::set<vtx_t> vertices;
    vector<Edge> all_edges = read_graph_from_file(FILENAME, V, vertices);
    
    if (all_edges.empty()) {
        cout << "Warning: No edges loaded for MST construction" << endl;
        return vector<Edge>();
    }
    
    vector<Edge> sorted_edges = all_edges;
    std::sort(sorted_edges.begin(), sorted_edges.end(), 
              [](const Edge& a, const Edge& b) { return a.w < b.w; });
    
    vector<vtx_t> parent(V);
    std::iota(parent.begin(), parent.end(), 0);
    
    function<vtx_t(vtx_t)> find = [&](vtx_t x) {
        return parent[x] == x ? x : parent[x] = find(parent[x]);
    };
    
    vector<Edge> result;
    for (const auto& edge : sorted_edges) {
        vtx_t root_u = find(edge.u);
        vtx_t root_v = find(edge.v);
        
        if (root_u != root_v) {
            parent[root_v] = root_u;
            result.push_back(edge);
            if ((int)result.size() == V - 1) break;  // MST complete
        }
    }
    
    cout << "MST constructed: " << result.size() << " edges" << endl;
    return result;
}

#endif  // !CUDA_AVAILABLE


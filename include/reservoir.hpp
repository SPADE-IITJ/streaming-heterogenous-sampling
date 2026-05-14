#ifndef RESERVOIR_HPP
#define RESERVOIR_HPP

#include "utils.hpp"
#include "lct.hpp"
#include "filters.hpp"

struct TupleHash {
    std::size_t operator()(const std::tuple<vtx_t, vtx_t>& t) const {
        return std::hash<vtx_t>{}(std::get<0>(t)) ^ (std::hash<vtx_t>{}(std::get<1>(t)) << 1);
    }
};

using UpdationReservoir = unordered_map<tuple<vtx_t, vtx_t>, weight_t, TupleHash>;

void process_edge(vtx_t u, vtx_t v, weight_t w, vtx_t V,
                  link_cut_tree& lct,
                  vector<bitmap_t>& h_MultiFilter,
                  vector<weight_t>& h_weights)
{
    long long edge_index = get_edge_index(u, v, V);
    if (edge_index == -1) return; 

    weight_t old_w = h_weights[edge_index];
    if (old_w == w) return;
    h_weights[edge_index] = w;

    long long word_index = edge_index / 16;
    int bit_pos = (edge_index % 16) * 2;
    auto state = (h_MultiFilter[word_index] >> bit_pos) & 3;

    if (state == multi_filter::IN_MST && w > old_w) {
        Edge edge_to_cut = lct.get_path_max_edge(u, v);
        lct.cut(edge_to_cut.u);

        bitmap_t mask = ~(3U << bit_pos);
        h_MultiFilter[word_index] = (h_MultiFilter[word_index] & mask) | (multi_filter::IS_EDGE << bit_pos);
        return;
    }

    bool changed;
    Edge removed_edge;
    UNPACK_MST_RESULT(changed, removed_edge, maintain_mst_with_lct(lct, u, v, w));

    if (changed) {
        bitmap_t mask = ~(3U << bit_pos);
        h_MultiFilter[word_index] = (h_MultiFilter[word_index] & mask) | (multi_filter::IN_MST << bit_pos);

        if (removed_edge.u != -1) {
            long long rem_edge_index = get_edge_index(removed_edge.u, removed_edge.v, V);
            if(rem_edge_index != -1) {
                long long rem_word_index = rem_edge_index / 16;
                int rem_bit_pos = (rem_edge_index % 16) * 2;
                mask = ~(3U << rem_bit_pos);
                h_MultiFilter[rem_word_index] = (h_MultiFilter[rem_word_index] & mask) | (multi_filter::IS_EDGE << rem_bit_pos);
            }
        }
    }
}

#endif // RESERVOIR_HPP

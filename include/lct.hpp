#ifndef LCT_HPP
#define LCT_HPP

#include "utils.hpp"

class link_cut_tree {
private:
    struct Node {
        vtx_t label = 0;
        weight_t weight = 0;
        weight_t max_weight = 0;
        Node* max_weight_node = this;

        Node *parent = nullptr;
        Node *left = nullptr;
        Node *right = nullptr;
        
        bool reverse = false; 
    };

    vector<Node> nodes;
    MSF_MembershipMap* membership_map;  

    bool is_root(Node* x) {
        return !x->parent || (x->parent->left != x && x->parent->right != x);
    }

    void update(Node* x) {
        if (!x) return;
        x->max_weight = x->weight;
        x->max_weight_node = x;
        if (x->left) {
            if (x->left->max_weight > x->max_weight) {
                x->max_weight = x->left->max_weight;
                x->max_weight_node = x->left->max_weight_node;
            }
        }
        if (x->right) {
            if (x->right->max_weight > x->max_weight) {
                x->max_weight = x->right->max_weight;
                x->max_weight_node = x->right->max_weight_node;
            }
        }
    }

    void push(Node* x) {
        if (!x || !x->reverse) return;
        std::swap(x->left, x->right);
        if (x->left) x->left->reverse ^= 1;
        if (x->right) x->right->reverse ^= 1;
        x->reverse = false;
    }

    void rotate(Node* x) {
        Node *p = x->parent, *g = p->parent;
        if (!is_root(p)) (g->left == p ? g->left : g->right) = x;
        x->parent = g;
        
        push(p); push(x); 
        if (p->left == x) { 
            p->left = x->right;
            if (x->right) x->right->parent = p;
            x->right = p;
        } else { 
            p->right = x->left;
            if (x->left) x->left->parent = p;
            x->left = p;
        }
        p->parent = x;
        update(p); update(x);
    }

    void splay(Node* x) {
        push(x);
        while (!is_root(x)) {
            Node *p = x->parent, *g = p->parent;
            if (!is_root(p)) push(g);
            push(p); push(x);
            if (!is_root(p)) {
                if ((p->left == x) == (g->left == p)) rotate(p);  
                else rotate(x); 
            }
            rotate(x); 
        }
        update(x);
    }

    Node* access(Node* x) {
        Node* last = nullptr;
        for (Node* y = x; y; y = y->parent) {
            splay(y);
            y->left = last;
            update(y);
            last = y;
        }
        splay(x);
        return last;
    }

    void make_root(int u_idx) {
        access(&nodes[u_idx]);
        nodes[u_idx].reverse ^= 1;
        push(&nodes[u_idx]);
    }

public:
    explicit link_cut_tree(int n) : nodes(n), membership_map(nullptr) {
        for (int i = 0; i < n; ++i) nodes[i].label = i;
    }
    
    explicit link_cut_tree(int n, MSF_MembershipMap* mmap) 
        : nodes(n), membership_map(mmap) {
        for (int i = 0; i < n; ++i) nodes[i].label = i;
    }
    
    void set_membership_map(MSF_MembershipMap* mmap) {
        membership_map = mmap;
    }

    void link(int u_idx, int v_idx, weight_t w) {
        make_root(u_idx);
        access(&nodes[u_idx]);
        
        nodes[u_idx].weight = w; 
        update(&nodes[u_idx]);
        
        nodes[u_idx].parent = &nodes[v_idx];
        
        if (membership_map) {
            membership_map->insert(u_idx, v_idx, w);
        }
    }

    void cut(int u_idx) {
        access(&nodes[u_idx]);
        if (nodes[u_idx].right) {
            vtx_t parent_label = -1;
            if (nodes[u_idx].parent) {
                parent_label = nodes[u_idx].parent->label;
            }
            
            nodes[u_idx].right->parent = nullptr;
            nodes[u_idx].right = nullptr;
            update(&nodes[u_idx]);
            
            if (membership_map && parent_label >= 0) {
                membership_map->remove(u_idx, parent_label);
            }
        }
    }

    int get_root(int u_idx) {
        access(&nodes[u_idx]);
        Node* root = &nodes[u_idx];
        push(root);
        while (root->right) {
            root = root->right;
            push(root);
        }
        splay(root);
        return root->label;
    }

    bool connected(int u_idx, int v_idx) {
        return get_root(u_idx) == get_root(v_idx);
    }

    Edge get_path_max_edge(int u_idx, int v_idx) {
        make_root(u_idx);
        access(&nodes[v_idx]);
        
        Node* max_node = nodes[v_idx].max_weight_node;
        
        splay(max_node);
        Node* parent = max_node->right;
        if (!parent) return {-1, -1, -1}; 
        push(parent);
        while(parent->left) {
            parent = parent->left;
            push(parent);
        }
        
        return {max_node->label, parent->label, max_node->weight};
    }
};

link_cut_tree build_lct_from_mst(const vector<Edge>& mst_edges, vtx_t V) {
    link_cut_tree lct(V);
    for (const auto& edge : mst_edges) {
        lct.link(edge.u, edge.v, edge.w);
    }
    cout << "Link-Cut Tree built on host from MST edges." << endl;
    return lct;
}

link_cut_tree build_lct_from_mst_with_membership(
    const vector<Edge>& mst_edges, 
    vtx_t V,
    MSF_MembershipMap& membership_map) 
{
    link_cut_tree lct(V, &membership_map);
    for (const auto& edge : mst_edges) {
        lct.link(edge.u, edge.v, edge.w);
    }
    cout << "Link-Cut Tree built with membership map for O(1) queries." << endl;
    return lct;
}

tuple<bool, Edge> maintain_mst_with_lct(link_cut_tree& lct, vtx_t u, vtx_t v, weight_t w) {
    if (lct.get_root(u) != lct.get_root(v)) {
        lct.link(u, v, w);
        return {true, {-1, -1, -1}};
    }
    
    Edge max_edge = lct.get_path_max_edge(u, v);

    if (w < max_edge.w) {
        lct.cut(max_edge.u); 
        lct.link(u, v, w);
        return {true, max_edge};
    }

    return {false, {-1, -1, -1}};
}

#endif // LCT_HPP

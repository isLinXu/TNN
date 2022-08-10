// Tencent is pleased to support the open source community by making TNN available.
//
// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "tnn/optimizer/graph_matcher/graph_matcher.h"

#include <vector>
#include <stack>
#include <map>
#include <set>
#include <sstream>

#include "tnn/core/macro.h"
#include "tnn/optimizer/graph_matcher/ir.h"
#include "tnn/optimizer/graph_matcher/logger.h"

namespace TNN_NS {

const char * pattern_node_prefix = "@";

#define TEST(expr)              \
        if (!(expr)) {          \
            DEBUG("\t\tcheck "#expr" failed for node [%s] with pattern [%s]", \
                    node->name.c_str(), probe->name.c_str());   \
            return false;       \
        }

void AnchorGraph::backTrace(int recursion) {
    for(auto it = paired_nodes.begin(); it!= paired_nodes.end(); ) {
        if (it->second.recursion >= recursion) {
            it = paired_nodes.erase(it);
        } else {
            ++it;
        }
    }
}


bool AnchorGraph::matchUp(Node *node, Node* probe, int recursion, bool silence) {
    if (paired_nodes.find(node) != paired_nodes.end()) {
        if (paired_nodes.at(node).anchor != probe) {
            if (!silence) DEBUG("node[%s] is already paired with another probe[%s].", node->name.c_str(), 
                        paired_nodes.at(node).anchor->name.c_str());
            return false;
        }
        return true;
    }
    for(auto it : paired_nodes) {
        if (it.second.anchor == probe) {
            if (!silence) DEBUG("probe[%s] is already paired with another node[%s].", probe->name.c_str(), it.second.node->name.c_str());
            return false;
        }
    }

    // LAYER_NOT_SUPPORT MEANS PlaceHolder here.
    // We need Another type wildcard type here.
    if (probe->info->type == LAYER_NOT_SUPPORT) {
        if (!silence) DEBUG("%*srec[%d] node[%s] matched with pattern placeholder [%s]", (recursion%10)*4, "", recursion, node->name.c_str(), probe->name.c_str());
        paired_nodes[node] = NodePair(node, probe, recursion);
        return true;
    }
    TEST(node->info->type == probe->info->type);
    TEST(node->info->inputs.size() == probe->info->inputs.size());
    TEST(node->input_edges.size() == probe->input_edges.size());
    TEST(node->info->outputs.size() == probe->info->outputs.size());

    for(size_t i=0;i<probe->input_edges.size();i++) {
        TEST(matchUp(node->input_edges[i]->src, probe->input_edges[i]->src, recursion+1, silence));
    }

    if (!silence) DEBUG("%*srec[%d] node[%s] matched with pattern[%s]", (recursion%10)*4, "", recursion, node->name.c_str(), probe->name.c_str());
    paired_nodes[node] = NodePair(node, probe, recursion);

    return true;   
}

std::vector<Node *> AnchorGraph::allStructualMatchedNodes(Node * pattern_sibling_node) {
    struct Path {
        Node * n;
        std::stack<LayerType> types;

        Path(Node *ptr, std::stack<LayerType> _types=std::stack<LayerType>()) : n(ptr), types(_types) {};
    };
    std::queue<Path> start_points;

    std::stack<Path> que;
    que.push(Path(pattern_sibling_node));
    // DFS to find the common node
    while(!que.empty()) {
        Path candidate = que.top();
        que.pop();

        bool found = false;
        for(auto it = paired_nodes.begin(); it != paired_nodes.end();it++) {
            if (it->second.anchor == candidate.n) {
                DEBUG("add Start point:%s", it->first->name.c_str());
                Node * sibling_node = it->first;
                start_points.push(Path(sibling_node, candidate.types));
                found = true;
                break;
            }
        }
        if (found) continue;

        Path next(candidate.n, candidate.types);
        next.types.push(candidate.n->info->type);
        for(auto &e : candidate.n->input_edges) {
            next.n = e->src;
            que.push(next);
        }
    }

    std::vector<Node *> res;
    // BFS to find all matched Nodes
    while(!start_points.empty()) {
        Path path = start_points.front(); start_points.pop();

        std::stringstream ss;
        ss << "test start Point:" << path.n->name << " type path:";
        auto tmp = path.types;
        while(!tmp.empty()) { ss << "[" << tmp.top() << "] "; tmp.pop();}
        DEBUG("%s", ss.str().c_str());

        if (path.types.empty()) {
            res.push_back(path.n);
            continue;
        }

        for(auto &e: path.n->output_edges) {
            // skip already matched nodes.
            if (paired_nodes.find(e->dst) != paired_nodes.end()) {
                continue;
            }
            if (e->dst->info->type == path.types.top() || path.types.top() == LAYER_NOT_SUPPORT ) {
                std::stack<LayerType> next_types = path.types;
                next_types.pop();
                start_points.push(Path(e->dst, next_types));
            }
        }

    }

    return res;
}

void AnchorGraph::formalize(Graph *g) {
    // copy subgraph related nodes, edges, placeholders.
    // Initialize the blob_2_node map,
    // add entry point to the blob_2_node map for the named nodes in the pattern graph.

    // removed pairs that anchor is a placeholder
    for(auto it = paired_nodes.begin(); it!= paired_nodes.end(); ) {
        if (it->second.anchor->info->type == LAYER_NOT_SUPPORT) {
            it = paired_nodes.erase(it);
        } else {
            ++it;
        }
    }

    for(auto &n : g->nodes) {
        if (paired_nodes.find(n.get()) != paired_nodes.end()) {
            nodes.push_back(n);
            blob_2_node[n->name] = n;
        }
    }
    for(auto &e : g->edges) {
        if (paired_nodes.find(e->src) != paired_nodes.end() || paired_nodes.find(e->dst) != paired_nodes.end()) {
            edges.push_back(e);
        }
    }
    // placeholder types is not included in the paired nodes now.
    for(auto &n : g->placeholders) {
        if (paired_nodes.find(n.get()) != paired_nodes.end()) {
            throw std::runtime_error("PlaceHolder node should not be included in the AnchorGraph.");
            nodes.push_back(n);
        }
    }

    auto getShared = [&](Node *p) {
        for(auto &n:nodes) {if (n.get() == p) return n;}
        throw std::runtime_error("Not found the paired nodes in the subgraph.");
    };

    for(auto it = paired_nodes.begin(); it!= paired_nodes.end(); it++) {
        // the prefix should keep as same as that in parser.cpp
        if (it->second.anchor->name.substr(0, 5) != std::string("Node_")) {
            std::string ref_name = std::string(pattern_node_prefix) + it->second.anchor->name;
            blob_2_node[ref_name] = getShared(it->first);
        }
    }
}


void match(const std::shared_ptr<Graph> graph, const std::shared_ptr<Graph> pattern,  std::vector<std::shared_ptr<AnchorGraph>>  &results) {
    results.resize(0);

    std::vector<Node *> pattern_outs = pattern->outputs();
    for(auto &n : graph->nodes) {
        std::shared_ptr<AnchorGraph> res = std::make_shared<AnchorGraph>();
        DEBUG("---------------------- test output[0] of pattern on node [%s]", n->name.c_str());
        if (res->matchUp(n.get(), pattern_outs[0], 0)) {
            struct DFSState {
                int output_id;
                Node * node;
                DFSState(int id, Node *n) : node(n), output_id(id) {}
            };

            auto getRecursion = [](int out_id) -> int {return out_id * 100;};

            std::stack<DFSState> que;
            que.push(DFSState(1, n.get()));
            while(!que.empty()) {
                DFSState cur = que.top(); que.pop();
                DEBUG("---------------------- test output[%d] of pattern on node [%s]", cur.output_id, cur.node->name.c_str());
                res->backTrace(getRecursion(cur.output_id - 1));
                if (!res->matchUp(cur.node, pattern_outs[cur.output_id -1], getRecursion(cur.output_id -1), true)) {
                    std::stringstream ss; 
                    ss << "graph matcher found unmatched Node State on" << cur.node->name << " on output_id:" << cur.output_id;
                    throw std::runtime_error(ss.str());
                }

                if (cur.output_id == pattern_outs.size())  {
                    auto snapshot = std::make_shared<AnchorGraph>(*res);
                    snapshot->formalize(graph.get());
                    results.push_back(snapshot);
                    INFO("matched at node [%s] and pattern [%s]", cur.node->name.c_str(), pattern_outs[cur.output_id-1]->name.c_str());
                    continue;
                }

                // Loop over all possible paired output node for the pattern output
                auto possible_outs = res->allStructualMatchedNodes(pattern_outs[cur.output_id]);

                std::stringstream ss; 
                ss << "output[" << cur.output_id << "] candidates:";
                for(auto &n : possible_outs) { ss << "[" << n->name << "],"; }
                DEBUG("%s", ss.str().c_str());

                for(auto &candidate : possible_outs) {
                    res->backTrace(getRecursion(cur.output_id));
                    if (res->matchUp(candidate, pattern_outs[cur.output_id], getRecursion(cur.output_id))) {
                        que.push(DFSState(cur.output_id +1, candidate));
                    }
                }
            }
        }
    }
}

std::vector<std::vector<std::shared_ptr<AnchorGraph>>> clustering(const std::vector<std::shared_ptr<AnchorGraph>> &matches) {

    struct Cluster {
        int id;
        std::set<Node *> nodes;
        Cluster(int _id): id(_id) {}
    };

    std::map<Node *, std::shared_ptr<Cluster> > groups;

    auto allNodeNotSeen = [&](const std::shared_ptr<AnchorGraph> & g) {
        for(auto &n : g->nodes) {
            if (groups.find(n.get()) != groups.end()) {
                return false;
            }
        }
        return true;
    };

    auto relatedClusters = [&](const std::shared_ptr<AnchorGraph> & g) -> std::vector<std::shared_ptr<Cluster>> {
        // use std::map to prevent duplicated clusters showing up 
        std::map<int, std::shared_ptr<Cluster>> _c;
        for(auto &n : g->nodes) {
            if (groups.find(n.get()) == groups.end()) {
                continue;
            }
            auto cluster = groups.at(n.get());
            _c[cluster->id] = cluster;
        }
        std::vector<std::shared_ptr<Cluster>> result;
        for(auto it : _c) {
            result.push_back(it.second);
        }
        return result;
    };

    int cnt = 0;
    for(auto &g : matches) {
        if (allNodeNotSeen(g)) {
            // create a cluster 
            auto c = std::make_shared<Cluster>(cnt++);
            for(auto &n : g->nodes) {
                c->nodes.insert(n.get());
                groups[n.get()] = c;
            }
        } else {
            auto clusters_to_merge = relatedClusters(g);
            for(size_t i = 1;i<clusters_to_merge.size();i++) {
                for(auto &n : clusters_to_merge[i]->nodes) {
                    clusters_to_merge[0]->nodes.insert(n);
                    groups[n] = clusters_to_merge[0];
                }
            }
        }
    }

    std::map<int, std::vector<std::shared_ptr<AnchorGraph>>> id_to_cluster;
    for(auto &g : matches) {
        int cluster_id = groups[g->nodes[0].get()]->id;
        id_to_cluster[cluster_id].push_back(g);
    }

    std::vector<std::vector<std::shared_ptr<AnchorGraph>>> res;
    for(auto it : id_to_cluster) {
        res.push_back(it.second);
    }

    return res;
}

} // namespace tnn
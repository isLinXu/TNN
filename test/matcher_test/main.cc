#include <stdio.h>
#include <sstream>
#include <vector>
#include <fstream>

#include "tnn/optimizer/graph_matcher/ir.h"
#include "tnn/optimizer/graph_matcher/graph_parser.h"
#include "tnn/optimizer/graph_matcher/text_graph_parser.h"
#include "tnn/optimizer/graph_matcher/graph_matcher.h"
#include "tnn/optimizer/graph_matcher/logger.h"

int main(int argc, char ** argv) {
    tnn::Logger::instance().set_verbose_level("D");

    tnn::GraphParser graph_parser;

    std::string graph_str = R"(
        graph(%a):
            %a = Add(%5)
            %b,%c = Mul(%a)
            return (%b)
    )";
    // graph_parser.parseFromString(graph_str);
    // return 0;

    std::vector<std::string> text_graph = {
        "LayerNorm # some comments",
        "        MatMul<",
        "        Add",
        "                                      Mul<",
        "                            Mul<+>",
        "                Mul<        Add",
        "                Mul+>",
        "                Tanh@act",
        "        Mul     Add",
        "        Mul+>",
        "        MatMul",
        "        Add+{act}",
        "Add+>",
        "Add",
        "Mul     ",
        "Mul",
    };


    tnn::TextGraphParser parser;

    std::shared_ptr<tnn::Graph> graph, pattern;

    auto status = parser.parseFromString(text_graph);
    if (status == tnn::TNN_OK){
        graph = parser.getGraph();
        std::ofstream f("test.tnnproto");
        graph->dump(f);
    } else {
        printf("parse got error, code:%d msg:%s\n", int(status), status.description().c_str());
        return 0;
    }


    {
        std::vector<std::string> text_graph_pattern = {
            "Add",
            "Mul@xxx",
            // "Add"
            // "MatMul Tanh",
            // "Add+>",
        };

        if (parser.parseFromString(text_graph_pattern)) {
            pattern = parser.getGraph();
            std::ofstream f("pattern.tnnproto");
            pattern->dump(f);
        }

        auto gen = [](std::shared_ptr<tnn::AnchorGraph> in) -> std::shared_ptr<tnn::HeirGraph> {
            if (in->inputs().size() != 1 || in->outputs().size() != 1 ){
                return nullptr;
            }

            auto n_of_interest = in->peekNodeByTensorName(std::string("@xxx"));
            if (!n_of_interest) {
                printf("roi node not found\n");
                return nullptr;
            }

            auto g = std::make_shared<tnn::HeirGraph>();
            int num_inputs = 1;
            for(int i=0;i<num_inputs;i++) {
                auto ph = g->getNodeByTensorName(std::string("PlaceHolder_") + std::to_string(i));
                ph->info->type = tnn::LAYER_DUMMY_TYPE;
            }
            auto new_node = std::make_shared<tnn::Node>("new_heir_node");
            new_node->info->type = tnn::LAYER_TANH;
            new_node->info->outputs = {"add_mul_blob"};
            g->nodes.push_back(new_node);
            g->markAllInOneNode(*in);

            return g;
        };

        graph->rewrite(pattern, gen);
    }


#if 1
    {
        std::vector<std::string> text_graph_pattern = {
            "Add",
            "Mul    Mul<",
        };

        auto gen = [](std::shared_ptr<tnn::AnchorGraph> in) -> std::shared_ptr<tnn::HeirGraph> {
            if (in->inputs().size() != 1 || in->outputs().size() != 1 ){
                return nullptr;
            }

            auto g = std::make_shared<tnn::HeirGraph>();
            int num_inputs = 1;
            for(int i=0;i<num_inputs;i++) {
                auto ph = g->getNodeByTensorName(std::string("PlaceHolder_") + std::to_string(i));
                ph->info->type = tnn::LAYER_DUMMY_TYPE;
            }
            auto new_node = std::make_shared<tnn::Node>("_norm");
            new_node->info->type = tnn::LAYER_LAYER_NORM;
            g->nodes.push_back(new_node);
            g->markAllInOneNode(*in);

            return g;
        };

        if (parser.parseFromString(text_graph_pattern)) {
            pattern = parser.getGraph();
            std::ofstream f("pattern2.tnnproto");
            pattern->dump(f);
            graph->rewrite(pattern, gen);
        }
    }


    {
        std::vector<std::string> text_graph_pattern = {
            "Placeholder Placeholder",
            "AnyType",
            "AnyType+>",
        };

        auto gen = [](std::shared_ptr<tnn::AnchorGraph> in) -> std::shared_ptr<tnn::HeirGraph> {
            if (in->inputs().size() != 2 || in->outputs().size() != 1 ){
                printf("Expect HeirGraph to Have 2 inputs and 1 outputs, but got %lu inputs and %lu outptus.\n",
                        in->inputs().size(), in->outputs().size());
                return nullptr;
            }

            auto g = std::make_shared<tnn::HeirGraph>();
            int num_inputs = 2;
            for(int i=0;i<num_inputs;i++) {
                auto ph = g->getNodeByTensorName(std::string("PlaceHolder_") + std::to_string(i));
                ph->info->type = tnn::LAYER_DUMMY_TYPE;
            }
            auto new_node = std::make_shared<tnn::Node>("_mulmul");
            new_node->info->type = tnn::LAYER_CONVOLUTION;
            g->nodes.push_back(new_node);
            g->markAllInOneNode(*in);

            return g;
        };

        if (parser.parseFromString(text_graph_pattern)) {
            pattern = parser.getGraph();
            std::ofstream f("pattern3.tnnproto");
            pattern->dump(f);
            graph->rewrite(pattern, gen);
        }
    }

    {
        std::vector<std::string> text_graph_ffn = {
            "LayerNorm",
            "        MatMul<",
            "        AnyType",
            "                                      Mul<",
            "                            Mul<+>",
            "                Mul<        Add",
            "                Mul+>",
            "                Tanh@act",
            "        Mul     AnyType",
            "        Mul+>",
            "        MatMul",
            "        AnyType+{act}",
            "Add+>",
            "Add",
            "Mul     ",
            "Mul",
        };
        auto gen = [](std::shared_ptr<tnn::AnchorGraph> in) -> std::shared_ptr<tnn::HeirGraph> {
            if (in->inputs().size() != 1 || in->outputs().size() != 1 ){
                printf("Expect HeirGraph to Have 1 inputs and 1 outputs, but got %lu inputs and %lu outptus.\n",
                        in->inputs().size(), in->outputs().size());
                return nullptr;
            }

            auto g = std::make_shared<tnn::HeirGraph>();
            int num_inputs = 1;
            for(int i=0;i<num_inputs;i++) {
                auto ph = g->getNodeByTensorName(std::string("PlaceHolder_") + std::to_string(i));
                ph->info->type = tnn::LAYER_DUMMY_TYPE;
            }
            auto new_node = std::make_shared<tnn::Node>("_ffn");
            new_node->info->type = tnn::LAYER_CONVOLUTION;
            g->nodes.push_back(new_node);
            g->markAllInOneNode(*in);

            return g;
        };

        if (parser.parseFromString(text_graph_ffn) == tnn::TNN_OK) {
            pattern = parser.getGraph();
            if (graph) graph->rewrite(pattern, gen);
        }
    }
#endif

    if (graph) {
        std::ofstream f("rewrited.tnnproto");
        graph->dump(f);
    }

    std::string s = tnn::Logger::instance().str();
    // printf("---------final ------------------len%lu:\n%s\n", s.length(), s.c_str());

    return 0;
}

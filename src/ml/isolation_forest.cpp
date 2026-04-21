#include "ml/isolation_forest.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

IsolationForest::IsolationForest(int n_trees, double contamination, unsigned int random_seed)
    : n_trees_(n_trees), contamination_(contamination), rng_(random_seed) {}

// c(n): average path length of unsuccessful BST search
double IsolationForest::avgPathLength(int n) const {
    if (n <= 1) return 0.0;
    double h = std::log((double)(n - 1)) + 0.5772156649; // Euler-Mascheroni
    return 2.0 * h - 2.0 * (n - 1) / (double)n;
}

void IsolationForest::IsolationTree::build(
    const std::vector<std::array<double,2>>& X,
    int max_depth, std::mt19937& rng)
{
    nodes.clear();
    nodes.reserve(512);

    // iterative build using stack: {node_idx, sample_indices, depth}
    struct Frame {
        int node_idx;
        std::vector<int> indices;
        int depth;
    };

    nodes.push_back(Node{});
    root = 0;

    std::vector<Frame> stack;
    {
        std::vector<int> all(X.size());
        std::iota(all.begin(), all.end(), 0);
        stack.push_back({0, std::move(all), 0});
    }

    while (!stack.empty()) {
        auto [nidx, indices, depth] = std::move(stack.back());
        stack.pop_back();

        if ((int)indices.size() <= 1 || depth >= max_depth) {
            nodes[nidx].is_leaf = true;
            nodes[nidx].size = (int)indices.size();
            continue;
        }

        // pick random feature
        std::uniform_int_distribution<int> feat_dist(0, 1);
        int feat = feat_dist(rng);

        double mn = X[indices[0]][feat], mx = mn;
        for (int i : indices) {
            mn = std::min(mn, X[i][feat]);
            mx = std::max(mx, X[i][feat]);
        }

        if (mn >= mx) {
            nodes[nidx].is_leaf = true;
            nodes[nidx].size = (int)indices.size();
            continue;
        }

        std::uniform_real_distribution<double> split_dist(mn, mx);
        double split = split_dist(rng);

        nodes[nidx].split_feature = feat;
        nodes[nidx].split_value = split;

        std::vector<int> left_idx, right_idx;
        for (int i : indices) {
            if (X[i][feat] < split) left_idx.push_back(i);
            else right_idx.push_back(i);
        }

        int left_node = (int)nodes.size();
        nodes.push_back(Node{});
        int right_node = (int)nodes.size();
        nodes.push_back(Node{});

        nodes[nidx].left = left_node;
        nodes[nidx].right = right_node;

        stack.push_back({right_node, std::move(right_idx), depth + 1});
        stack.push_back({left_node,  std::move(left_idx),  depth + 1});
    }
}

double IsolationForest::IsolationTree::pathLength(const std::array<double,2>& x) const {
    int cur = root;
    double length = 0.0;
    while (true) {
        const Node& node = nodes[cur];
        if (node.is_leaf) {
            // c(size) adjustment
            if (node.size > 1) {
                double h = std::log((double)(node.size - 1)) + 0.5772156649;
                length += 2.0 * h - 2.0 * (node.size - 1) / (double)node.size;
            }
            break;
        }
        ++length;
        if (x[node.split_feature] < node.split_value) cur = node.left;
        else cur = node.right;
    }
    return length;
}

double IsolationForest::anomalyScore(const std::array<double,2>& x) const {
    double avg = 0.0;
    for (const auto& t : trees_) avg += t.pathLength(x);
    avg /= trees_.size();
    double cn = avgPathLength(256);
    if (cn == 0.0) return 0.0;
    // Return negative score so "more negative = more anomalous"
    return -std::pow(2.0, -avg / cn);
}

void IsolationForest::fit(const std::vector<std::array<double,2>>& X) {
    if (X.empty()) return;
    n_samples_ = (int)X.size();  // PROD-07: track for model metadata

    int subsample = std::min(256, (int)X.size());
    int max_depth = (int)std::ceil(std::log2((double)subsample));

    trees_.clear();
    trees_.resize(n_trees_);

    for (auto& tree : trees_) {
        // subsample
        std::vector<std::array<double,2>> sample;
        if ((int)X.size() <= subsample) {
            sample = X;
        } else {
            std::vector<int> idx(X.size());
            std::iota(idx.begin(), idx.end(), 0);
            std::shuffle(idx.begin(), idx.end(), rng_);
            idx.resize(subsample);
            for (int i : idx) sample.push_back(X[i]);
        }
        tree.build(sample, max_depth, rng_);
    }

    // calibrate threshold
    auto scores = scoresSamples(X);
    std::sort(scores.begin(), scores.end()); // ascending (most negative first)
    int threshold_idx = (int)((1.0 - contamination_) * scores.size());
    threshold_idx = std::max(0, std::min((int)scores.size() - 1, threshold_idx));
    threshold_ = scores[threshold_idx];
    fitted_ = true;
}

std::vector<double> IsolationForest::scoresSamples(
    const std::vector<std::array<double,2>>& X) const
{
    std::vector<double> out;
    out.reserve(X.size());
    for (const auto& x : X) out.push_back(anomalyScore(x));
    return out;
}

std::vector<int> IsolationForest::predict(
    const std::vector<std::array<double,2>>& X) const
{
    auto scores = scoresSamples(X);
    std::vector<int> preds;
    preds.reserve(X.size());
    for (double s : scores) preds.push_back(s < threshold_ ? -1 : 1);
    return preds;
}

bool IsolationForest::isFitted() const { return fitted_; }

void IsolationForest::save(const std::string& path) const {
    // PROD-07: include version metadata so load() can validate the file format.
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    gmtime_r(&t, &tm_buf);
    char ts_buf[32];
    strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);

    json j;
    j["version"]      = 1;
    j["trained_at"]   = ts_buf;
    j["n_samples"]    = n_samples_;
    j["n_trees"]      = n_trees_;
    j["contamination"] = contamination_;
    j["threshold"]    = threshold_;
    j["fitted"]       = fitted_;
    j["trees"] = json::array();
    for (const auto& tree : trees_) {
        json tj = json::array();
        for (const auto& n : tree.nodes) {
            tj.push_back({{"is_leaf", n.is_leaf}, {"split_feature", n.split_feature},
                          {"split_value", n.split_value}, {"left", n.left},
                          {"right", n.right}, {"size", n.size}});
        }
        j["trees"].push_back(tj);
    }
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot write model file: " + path);
    f << j.dump();
    f.flush();
}

void IsolationForest::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open model file: " + path);
    json j = json::parse(f);
    // PROD-07: version field present in v1+ files; older files without it still load.
    n_trees_       = j["n_trees"];
    contamination_ = j["contamination"];
    threshold_     = j["threshold"];
    fitted_        = j["fitted"];
    n_samples_     = j.value("n_samples", 0);
    trees_.clear();
    for (const auto& tj : j["trees"]) {
        IsolationTree tree;
        for (const auto& nj : tj) {
            IsolationTree::Node n;
            n.is_leaf       = nj["is_leaf"];
            n.split_feature = nj["split_feature"];
            n.split_value   = nj["split_value"];
            n.left          = nj["left"];
            n.right         = nj["right"];
            n.size          = nj["size"];
            tree.nodes.push_back(n);
        }
        trees_.push_back(std::move(tree));
    }
}

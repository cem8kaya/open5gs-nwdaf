#include "ml/isolation_forest.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ── Constructor ───────────────────────────────────────────────────────────────

template<std::size_t N>
IsolationForestN<N>::IsolationForestN(int n_trees, double contamination,
                                       unsigned int random_seed)
    : n_trees_(n_trees), contamination_(contamination), rng_(random_seed) {}

// ── Average path length c(n) ──────────────────────────────────────────────────

template<std::size_t N>
double IsolationForestN<N>::avgPathLength(int n) const {
    if (n <= 1) return 0.0;
    double h = std::log((double)(n - 1)) + 0.5772156649; // Euler-Mascheroni
    return 2.0 * h - 2.0 * (n - 1) / (double)n;
}

// ── IsolationTree::build ──────────────────────────────────────────────────────

template<std::size_t N>
void IsolationForestN<N>::IsolationTree::build(
    const std::vector<std::array<double,N>>& X,
    int max_depth, std::mt19937& rng)
{
    nodes.clear();
    nodes.reserve(512);

    // Iterative build using stack: {node_idx, sample_indices, depth}
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

        // ARCH-02: pick random feature in [0, N-1] (generalised from hardcoded 0..1)
        std::uniform_int_distribution<int> feat_dist(0, (int)N - 1);
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
            else                    right_idx.push_back(i);
        }

        int left_node  = (int)nodes.size(); nodes.push_back(Node{});
        int right_node = (int)nodes.size(); nodes.push_back(Node{});

        nodes[nidx].left  = left_node;
        nodes[nidx].right = right_node;

        stack.push_back({right_node, std::move(right_idx), depth + 1});
        stack.push_back({left_node,  std::move(left_idx),  depth + 1});
    }
}

// ── IsolationTree::pathLength ─────────────────────────────────────────────────

template<std::size_t N>
double IsolationForestN<N>::IsolationTree::pathLength(
    const std::array<double,N>& x) const
{
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
        else                                           cur = node.right;
    }
    return length;
}

// ── anomalyScore ──────────────────────────────────────────────────────────────

template<std::size_t N>
double IsolationForestN<N>::anomalyScore(const std::array<double,N>& x) const {
    double avg = 0.0;
    for (const auto& t : trees_) avg += t.pathLength(x);
    avg /= trees_.size();
    double cn = avgPathLength(256);
    if (cn == 0.0) return 0.0;
    // Return negative score so "more negative = more anomalous"
    return -std::pow(2.0, -avg / cn);
}

// ── fit ───────────────────────────────────────────────────────────────────────

template<std::size_t N>
void IsolationForestN<N>::fit(const std::vector<std::array<double,N>>& X) {
    if (X.empty()) return;
    n_samples_ = (int)X.size(); // PROD-07: track for model metadata

    int subsample = std::min(256, (int)X.size());
    int max_depth = (int)std::ceil(std::log2((double)subsample));

    trees_.clear();
    trees_.resize(n_trees_);

    for (auto& tree : trees_) {
        std::vector<std::array<double,N>> sample;
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

    // Calibrate threshold
    auto scores = scoresSamples(X);
    std::sort(scores.begin(), scores.end()); // ascending (most negative first)
    int threshold_idx = (int)((1.0 - contamination_) * scores.size());
    threshold_idx = std::max(0, std::min((int)scores.size() - 1, threshold_idx));
    threshold_ = scores[threshold_idx];
    fitted_ = true;
}

// ── scoresSamples / predict ───────────────────────────────────────────────────

template<std::size_t N>
std::vector<double> IsolationForestN<N>::scoresSamples(
    const std::vector<std::array<double,N>>& X) const
{
    std::vector<double> out;
    out.reserve(X.size());
    for (const auto& x : X) out.push_back(anomalyScore(x));
    return out;
}

template<std::size_t N>
std::vector<int> IsolationForestN<N>::predict(
    const std::vector<std::array<double,N>>& X) const
{
    auto scores = scoresSamples(X);
    std::vector<int> preds;
    preds.reserve(X.size());
    for (double s : scores) preds.push_back(s < threshold_ ? -1 : 1);
    return preds;
}

template<std::size_t N>
bool IsolationForestN<N>::isFitted() const { return fitted_; }

// ── save ──────────────────────────────────────────────────────────────────────

template<std::size_t N>
void IsolationForestN<N>::save(const std::string& path) const {
    // PROD-07: write-then-rename caller handles atomicity; we just write the file.
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    gmtime_r(&t, &tm_buf);
    char ts_buf[32];
    strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);

    json j;
    j["version"]       = 1;
    j["n_features"]    = (int)N;   // ARCH-02: dimension recorded for mismatch detection
    j["trained_at"]    = ts_buf;
    j["n_samples"]     = n_samples_;
    j["n_trees"]       = n_trees_;
    j["contamination"] = contamination_;
    j["threshold"]     = threshold_;
    j["fitted"]        = fitted_;
    j["trees"] = json::array();
    for (const auto& tree : trees_) {
        json tj = json::array();
        for (const auto& nd : tree.nodes) {
            tj.push_back({{"is_leaf",       nd.is_leaf},
                          {"split_feature", nd.split_feature},
                          {"split_value",   nd.split_value},
                          {"left",          nd.left},
                          {"right",         nd.right},
                          {"size",          nd.size}});
        }
        j["trees"].push_back(tj);
    }
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot write model file: " + path);
    f << j.dump();
    f.flush();
}

// ── load ──────────────────────────────────────────────────────────────────────

template<std::size_t N>
void IsolationForestN<N>::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open model file: " + path);
    json j = json::parse(f);

    // ARCH-02: validate feature dimension — mismatch triggers automatic retraining
    // in loadModels() because the exception is caught there.
    int file_n_features = j.value("n_features", 2); // default 2 for pre-ARCH-02 files
    if (file_n_features != (int)N) {
        throw std::runtime_error(
            "Model dimension mismatch: file has " + std::to_string(file_n_features) +
            " features, model expects " + std::to_string(N) + " — retraining required");
    }

    // PROD-07: restore all fields
    n_trees_       = j["n_trees"];
    contamination_ = j["contamination"];
    threshold_     = j["threshold"];
    fitted_        = j["fitted"];
    n_samples_     = j.value("n_samples", 0);
    trees_.clear();
    for (const auto& tj : j["trees"]) {
        IsolationTree tree;
        for (const auto& nj : tj) {
            tree.nodes.emplace_back();
            auto& nd        = tree.nodes.back();
            nd.is_leaf       = nj["is_leaf"];
            nd.split_feature = nj["split_feature"];
            nd.split_value   = nj["split_value"];
            nd.left          = nj["left"];
            nd.right         = nj["right"];
            nd.size          = nj["size"];
        }
        trees_.push_back(std::move(tree));
    }
}

// ── Explicit instantiation definitions ───────────────────────────────────────
// These lines cause the compiler to emit the full class definition for N=2 and
// N=5 in this translation unit only, satisfying the extern template declarations
// in the header without duplicating code across TUs.
template class IsolationForestN<2>;
template class IsolationForestN<5>;

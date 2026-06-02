#pragma once
#include <vector>
#include <array>
#include <random>
#include <string>
#include <cstddef>

// ARCH-02: Parameterised on feature dimension N so anomaly detection can use
// an arbitrary number of features (e.g. 2 for legacy 2-D or 5 for the full
// {dl_kbps, ul_kbps, nf_load_avg, active_sessions, auth_failure_rate} vector).
template<std::size_t N>
class IsolationForestN {
public:
    explicit IsolationForestN(int n_trees = 100, double contamination = 0.10,
                               unsigned int random_seed = 42);

    void fit(const std::vector<std::array<double,N>>& X);
    std::vector<int>    predict(const std::vector<std::array<double,N>>& X) const;
    std::vector<double> scoresSamples(const std::vector<std::array<double,N>>& X) const;
    bool isFitted() const;

    void save(const std::string& path) const;
    void load(const std::string& path);

private:
    struct IsolationTree {
        struct Node {
            bool   is_leaf = false;
            int    split_feature = 0;
            double split_value = 0.0;
            int    left = -1, right = -1;
            int    size = 0;
        };
        std::vector<Node> nodes;
        int root = 0;

        void build(const std::vector<std::array<double,N>>& X,
                   int max_depth, std::mt19937& rng);
        double pathLength(const std::array<double,N>& x) const;
    };

    int                        n_trees_;
    double                     contamination_;
    std::mt19937               rng_;
    std::vector<IsolationTree> trees_;
    double                     threshold_ = 0.0;
    bool                       fitted_ = false;
    int                        n_samples_ = 0;   // PROD-07: stored in model metadata

    double avgPathLength(int n) const;
    double anomalyScore(const std::array<double,N>& x) const;
};

// Explicit instantiation declarations — definitions live in isolation_forest.cpp.
// Using extern template prevents redundant instantiation in every translation unit.
extern template class IsolationForestN<2>;
extern template class IsolationForestN<5>;

// Backward-compatibility alias: all existing code that uses IsolationForest (2-D)
// continues to compile without any modification.
using IsolationForest = IsolationForestN<2>;

#pragma once
#include <vector>
#include <array>
#include <random>
#include <string>

class IsolationForest {
public:
    explicit IsolationForest(int n_trees = 100, double contamination = 0.10,
                             unsigned int random_seed = 42);

    void fit(const std::vector<std::array<double,2>>& X);
    std::vector<int>    predict(const std::vector<std::array<double,2>>& X) const;
    std::vector<double> scoresSamples(const std::vector<std::array<double,2>>& X) const;
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

        void build(const std::vector<std::array<double,2>>& X,
                   int max_depth, std::mt19937& rng);
        double pathLength(const std::array<double,2>& x) const;
    };

    int                        n_trees_;
    double                     contamination_;
    std::mt19937               rng_;
    std::vector<IsolationTree> trees_;
    double                     threshold_ = 0.0;
    bool                       fitted_ = false;

    double avgPathLength(int n) const;
    double anomalyScore(const std::array<double,2>& x) const;
};

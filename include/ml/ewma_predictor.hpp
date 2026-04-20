#pragma once
#include <deque>

class EwmaPredictor {
public:
    explicit EwmaPredictor(double alpha = 0.3);

    double update(double observation);
    double predict() const;
    double residualStddev(int n = 20) const;
    bool hasHistory() const;
    void reset();

private:
    double              alpha_;
    double              smoothed_ = 0.0;
    bool                initialized_ = false;
    std::deque<double>  history_;
    std::deque<double>  residuals_;
};

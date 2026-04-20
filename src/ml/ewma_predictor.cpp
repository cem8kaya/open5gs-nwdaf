#include "ml/ewma_predictor.hpp"
#include <cmath>
#include <numeric>
#include <stdexcept>

EwmaPredictor::EwmaPredictor(double alpha) : alpha_(alpha) {}

double EwmaPredictor::update(double observation) {
    if (!initialized_) {
        smoothed_ = observation;
        initialized_ = true;
    } else {
        double residual = observation - smoothed_;
        residuals_.push_back(residual);
        if (residuals_.size() > 100) residuals_.pop_front();
        smoothed_ = alpha_ * observation + (1.0 - alpha_) * smoothed_;
    }
    history_.push_back(observation);
    if (history_.size() > 100) history_.pop_front();
    return smoothed_;
}

double EwmaPredictor::predict() const {
    return smoothed_;
}

double EwmaPredictor::residualStddev(int n) const {
    if (residuals_.empty()) return 0.0;
    int count = std::min(n, (int)residuals_.size());
    auto start = residuals_.end() - count;
    double mean = 0.0;
    for (auto it = start; it != residuals_.end(); ++it) mean += *it;
    mean /= count;
    double variance = 0.0;
    for (auto it = start; it != residuals_.end(); ++it) {
        double diff = *it - mean;
        variance += diff * diff;
    }
    return std::sqrt(variance / count);
}

bool EwmaPredictor::hasHistory() const {
    return initialized_ && history_.size() > 1;
}

void EwmaPredictor::reset() {
    smoothed_ = 0.0;
    initialized_ = false;
    history_.clear();
    residuals_.clear();
}

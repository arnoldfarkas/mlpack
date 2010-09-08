#ifndef MLPACK_KDE_KDE_DUALTREE_H
#define MLPACK_KDE_KDE_DUALTREE_H

#include <armadillo>
#include "boost/math/distributions/normal.hpp"
#include "../monte_carlo/mean_variance_pair.h"
#include "core/metric_kernels/kernel.h"
#include "core/tree/statistic.h"
#include "core/table/table.h"

namespace ml {

class KdePostponed {

  public:

    double densities_l_;

    double densities_u_;

    double pruned_;

    KdePostponed() {
    }

    ~KdePostponed() {
    }

    void Init() {
      SetZero();
    }

    void Init(int rnode_count) {
      densities_l_ = densities_u_ = 0;
      pruned_ = (double) rnode_count;
    }

    template<typename KdeDelta, typename ResultType>
    void ApplyDelta(const KdeDelta &delta_in,
                    ResultType *query_results) {
      densities_l_ = ((double) densities_l_ +
                      (double) delta_in.densities_l_);
      densities_u_ = ((double) densities_u_ +
                      (double) delta_in.densities_u_);
      pruned_ = ((double) pruned_ +
                 (double) delta_in.pruned_);
    }

    void ApplyPostponed(const KdePostponed &other_postponed) {

      densities_l_ = ((double) densities_l_ +
                      (double) other_postponed.densities_l_);
      densities_u_ = ((double) densities_u_ +
                      (double) other_postponed.densities_u_);
      pruned_ = ((double) pruned_ +
                 (double) other_postponed.pruned_);
    }

    template<typename GlobalType, typename MetricType, typename PointType>
    void ApplyContribution(const GlobalType &global,
                           const MetricType &metric,
                           const PointType &query_point,
                           const PointType &reference_point) {
      double distsq = metric.DistanceSq(query_point, reference_point);
      double density_incoming = global.kernel().EvalUnnormOnSq(distsq);
      densities_l_ = ((double) densities_l_ + density_incoming);
      densities_u_ = ((double) densities_u_ + density_incoming);
    }

    void SetZero() {
      densities_l_ = 0;
      densities_u_ = 0;
      pruned_ = 0;
    }
};

class KdeGlobal {

  public:
    typedef core::metric_kernels::GaussianKernel KernelType;

    typedef core::table::Table TableType;

  private:

    double relative_error_;

    double probability_;

    KernelType kernel_;

    double mult_const_;

    core::table::Table *query_table_;

    core::table::Table *reference_table_;

    boost::math::normal normal_dist_;

    std::vector< ml::MeanVariancePair > mean_variance_pair_;

  public:

    std::vector< ml::MeanVariancePair > *mean_variance_pair() {
      return &mean_variance_pair_;
    }

    double compute_quantile(double tail_mass) const {
      double mass = 1 - 0.5 * tail_mass;
      if (mass > 0.999) {
        return 3;
      }
      else {
        return boost::math::quantile(normal_dist_, mass);
      }
    }

    core::table::Table *query_table() {
      return query_table_;
    }

    const core::table::Table *query_table() const {
      return query_table_;
    }

    core::table::Table *reference_table() {
      return reference_table_;
    }

    const core::table::Table *reference_table() const {
      return reference_table_;
    }

    double relative_error() const {
      return relative_error_;
    }

    double probability() const {
      return probability_;
    }

    void set_bandwidth(double bandwidth_in) {
      kernel_.Init(bandwidth_in);
    }

    const KernelType &kernel() const {
      return kernel_;
    }

    void Init(
      core::table::Table *reference_table_in,
      core::table::Table *query_table_in,
      double bandwidth_in, const bool is_monochromatic,
      double relative_error_in, double probability_in) {

      int effective_num_points =
        (is_monochromatic) ?
        (reference_table_in->n_entries() - 1) :
        reference_table_in->n_entries();
      kernel_.Init(bandwidth_in);
      mult_const_ = 1.0 /
                    (kernel_.CalcNormConstant(
                       reference_table_in->n_attributes()) *
                     ((double) effective_num_points));
      relative_error_ = relative_error_in;
      probability_ = probability_in;
      query_table_ = query_table_in;
      reference_table_ = reference_table_in;

      // Initialize the temporary vector for storing the Monte Carlo
      // results.
      mean_variance_pair_.resize(query_table_->n_entries());
    }

    double get_mult_const() const {
      return mult_const_;
    }
};

template<typename ContainerType>
class KdeResult {
  public:
    ContainerType densities_l_;
    ContainerType densities_;
    ContainerType densities_u_;
    ContainerType pruned_;

    KdeResult() {
    }

    ~KdeResult() {
    }

    template<typename MetricType, typename GlobalType>
    void PostProcess(const MetricType &metric,
                     int q_index,
                     const GlobalType &global,
                     const bool is_monochromatic) {
      if (is_monochromatic) {
        densities_l_[q_index] = (double)(densities_l_[q_index] -
                                         1.0);
        densities_u_[q_index] = (double)(densities_u_[q_index] -
                                         1.0);
      }

      densities_[q_index] = 0.5 * (densities_l_[q_index] +
                                   densities_u_[q_index]);
      densities_l_[q_index] *= global.get_mult_const();
      densities_[q_index] *= global.get_mult_const();
      densities_u_[q_index] *= global.get_mult_const();
    }

    void PrintDebug(const std::string &file_name) {
      FILE *file_output = fopen(file_name.c_str(), "w+");
      for (unsigned int i = 0; i < densities_.size(); i++) {
        fprintf(file_output, "%g %g %g %g\n", densities_l_[i],
                densities_[i], densities_u_[i], pruned_[i]);
      }
      fclose(file_output);
    }

    template<int mode, typename TableType>
    void GetDensities(TableType* table) {
      if (mode == 0) {
        table->Init(std::vector<int>(1, 1), std::vector<int>(),
                    densities_.size());
      }
      for (int i = 0; i < densities_.size(); i++) {
        typename TableType::Point_t point;
        table->get(i, &point);
        point.set(0, densities_[i]);
      }
    }

    void Init(int num_points) {
      densities_l_.resize(num_points);
      densities_.resize(num_points);
      densities_u_.resize(num_points);
      pruned_.resize(num_points);

      SetZero();
    }

    void SetZero() {
      for (int i = 0; i < static_cast<int>(densities_l_.size()); i++) {
        densities_l_[i] = 0;
        densities_[i] = 0;
        densities_u_[i] = 0;
        pruned_[i] = 0;
      }
    }

    template<typename GlobalType, typename TreeType, typename DeltaType>
    void ApplyProbabilisticDelta(const GlobalType &global,
                                 TreeType *qnode,
                                 double failure_probability,
                                 const DeltaType &delta_in) {

      // Get the iterator for the query node.
      typename GlobalType::TableType::TreeIterator qnode_it =
        global.query_table()->get_node_iterator(qnode);
      arma::vec qpoint;
      int qpoint_index;

      // Look up the number of standard deviations.
      double num_standard_deviations = global.compute_quantile(
                                         failure_probability);

      do {
        // Get each query point.
        qnode_it.Next(&qpoint, &qpoint_index);
        core::math::Range contribution;
        (*delta_in.mean_variance_pair_)[qpoint_index].scaled_interval(
          delta_in.pruned_, num_standard_deviations, &contribution);
        densities_l_[qpoint_index] += contribution.lo;
        densities_u_[qpoint_index] += contribution.hi;
        pruned_[qpoint_index] += delta_in.pruned_;
      }
      while (qnode_it.HasNext());
    }

    void ApplyPostponed(
      int q_index,
      const KdePostponed &postponed_in) {

      densities_l_[q_index] = ((double) densities_l_[q_index] +
                               (double) postponed_in.densities_l_);
      densities_u_[q_index] = ((double) densities_u_[q_index] +
                               (double) postponed_in.densities_u_);
      pruned_[q_index] = ((double) pruned_[q_index] +
                          (double) postponed_in.pruned_);
    }
};

class KdeDelta {

  public:

    double densities_l_;

    double densities_u_;

    double pruned_;

    std::vector< ml::MeanVariancePair > *mean_variance_pair_;

    KdeDelta() {
      SetZero();
    }

    ~KdeDelta() {
    }

    void SetZero() {
      densities_l_ = densities_u_ = pruned_ = 0;
      mean_variance_pair_ = NULL;
    }

    template<typename MetricType, typename GlobalType, typename TreeType>
    void DeterministicCompute(
      const MetricType &metric,
      const GlobalType &global, TreeType *qnode, TreeType *rnode,
      const core::math::Range &squared_distance_range) {

      int rnode_count = global.reference_table()->get_node_count(rnode);
      densities_l_ = rnode_count *
                     global.kernel().EvalUnnormOnSq(squared_distance_range.hi);
      densities_u_ = rnode_count *
                     global.kernel().EvalUnnormOnSq(squared_distance_range.lo);
      pruned_ = (double) rnode_count;
    }
};

class KdeSummary {

  public:

    double densities_l_;

    double densities_u_;

    double pruned_l_;

    KdeSummary() {
      SetZero();
    }

    ~KdeSummary() {
    }

    KdeSummary(const KdeSummary &summary_in) {
      densities_l_ = summary_in.densities_l_;
      densities_u_ = summary_in.densities_u_;
      pruned_l_ = summary_in.pruned_l_;
    }

    template < typename MetricType, typename GlobalType, typename DeltaType,
    typename TreeType, typename ResultType >
    bool CanProbabilisticSummarize(
      const MetricType &metric, GlobalType &global,
      DeltaType &delta, TreeType *qnode, TreeType *rnode,
      double failure_probability, ResultType *query_results) const {

      const int speedup_factor = 10;
      int num_samples = global.reference_table()->get_node_count(rnode) /
                        speedup_factor;

      if (num_samples > global.reference_table()->get_node_count(rnode)) {
        return false;
      }

      // Get the iterator for the query node.
      typename GlobalType::TableType::TreeIterator qnode_it =
        global.query_table()->get_node_iterator(qnode);
      arma::vec qpoint;
      int qpoint_index;

      // Get the iterator for the reference node.
      typename GlobalType::TableType::TreeIterator rnode_it =
        global.reference_table()->get_node_iterator(rnode);
      arma::vec rpoint;
      int rpoint_index;

      // Interval for the pivot query point.
      double num_standard_deviations = global.compute_quantile(
                                         failure_probability);
      delta.mean_variance_pair_ = ((GlobalType &) global).mean_variance_pair();

      // The flag saying whether the pruning is a success.
      bool prunable = true;

      // The min kernel value determined by the bounding box.
      double min_kernel_value = delta.densities_l_ /
                                ((double) global.reference_table()->
                                 get_node_count(rnode));

      int prev_qpoint_index = -1;
      double bandwidth = sqrt(global.kernel().bandwidth_sq());
      double movement_threshold = 0.05 * bandwidth;
      int movement_count = 0;
      do {

        // Get each query point.
        qnode_it.Next(&qpoint, &qpoint_index);
        bool skip = false;
        if (prev_qpoint_index >= 0) {
          arma::vec prev_qpoint;
          global.query_table()->get(prev_qpoint_index, &prev_qpoint);
          double dist = sqrt(metric.DistanceSq(qpoint, prev_qpoint));
          if (dist <= movement_threshold && movement_count < 5) {
            (*delta.mean_variance_pair_)[qpoint_index].Copy(
              (*delta.mean_variance_pair_)[prev_qpoint_index]);
            skip = true;
            movement_count++;
          }
          else {
            movement_count = 0;
          }
        }

        // Clear the sample mean variance pair for the current query.
        if (skip == false) {
          (*delta.mean_variance_pair_)[qpoint_index].SetZero();

          for (int i = 0; i < num_samples; i++) {

            // Pick a random reference point and compute the kernel
            // difference.
            rnode_it.RandomPick(&rpoint, &rpoint_index);
            double squared_dist = metric.DistanceSq(qpoint, rpoint);
            double kernel_value =
              global.kernel().EvalUnnormOnSq(squared_dist);
            double new_sample = kernel_value - min_kernel_value;

            // Accumulate the sample.
            (*delta.mean_variance_pair_)[qpoint_index].push_back(new_sample);
          }
        }

        // Add the correction.
        core::math::Range correction;
        (*delta.mean_variance_pair_)[qpoint_index].scaled_interval(
          delta.pruned_, num_standard_deviations, &correction);
        correction.lo = std::max(correction.lo, 0.0);
        correction += delta.densities_l_;

        // Take the middle estimate, though technically it is not correct.
        double modified_densities_l =
          query_results->densities_l_[qpoint_index] + correction.lo;
        double left_hand_side = correction.width() * 0.5;
        double right_hand_side =
          global.reference_table()->get_node_count(rnode) *
          global.relative_error() * modified_densities_l /
          static_cast<double>(global.reference_table()->n_entries());

        prunable = (left_hand_side <= right_hand_side);

        prev_qpoint_index = qpoint_index;
      }
      while (qnode_it.HasNext() && prunable);
      return prunable;
    }

    template < typename GlobalType, typename DeltaType, typename TreeType,
    typename ResultType >
    bool CanSummarize(
      const GlobalType &global, const DeltaType &delta,
      TreeType *qnode, TreeType *rnode, ResultType *query_results) const {

      double left_hand_side =
        0.5 * (delta.densities_u_ - delta.densities_l_);
      double right_hand_side =
        global.reference_table()->get_node_count(rnode) *
        global.relative_error() * densities_l_ /
        static_cast<double>(global.reference_table()->n_entries());

      return left_hand_side <= right_hand_side;
    }

    void SetZero() {
      densities_l_ = densities_u_ = pruned_l_ = 0;
    }

    void Init() {
      SetZero();
    }

    void StartReaccumulate() {
      densities_l_ = std::numeric_limits<double>::max();
      densities_u_ = 0;
      pruned_l_ = densities_l_;
    }

    template<typename ResultType>
    void Accumulate(const ResultType &results, int q_index) {
      densities_l_ = std::min(densities_l_, results.densities_l_[q_index]);
      densities_u_ = std::max(densities_u_, results.densities_u_[q_index]);
      pruned_l_ = std::min(pruned_l_, results.pruned_[q_index]);
    }

    void Accumulate(const KdeSummary &summary_in) {
      densities_l_ = std::min(densities_l_, summary_in.densities_l_);
      densities_u_ = std::max(densities_u_, summary_in.densities_u_);
      pruned_l_ = std::min(pruned_l_, summary_in.pruned_l_);
    }

    void Accumulate(const KdeSummary &summary_in,
                    const KdePostponed &postponed_in) {

      densities_l_ = std::min(densities_l_, summary_in.densities_l_ +
                              postponed_in.densities_l_);
      densities_u_ = std::max(densities_u_, summary_in.densities_u_ +
                              postponed_in.densities_u_);
      pruned_l_ = std::min(pruned_l_, summary_in.pruned_l_ +
                           postponed_in.pruned_);
    }

    void ApplyDelta(const KdeDelta &delta_in) {
      densities_l_ = ((double) densities_l_ +
                      (double) delta_in.densities_l_);
      densities_u_ = ((double) densities_u_ +
                      (double) delta_in.densities_u_);
    }

    void ApplyPostponed(const KdePostponed &postponed_in) {

      densities_l_ = ((double) densities_l_ +
                      (double) postponed_in.densities_l_);
      densities_u_ = ((double) densities_u_ +
                      (double) postponed_in.densities_u_);
      pruned_l_ = ((double) pruned_l_ +
                   (double) postponed_in.pruned_);
    }
};

class KdeStatistic: public core::tree::AbstractStatistic {

  private:
    KdeStatistic(const KdeStatistic &stat_in) {
    }

  public:

    ml::KdePostponed postponed_;

    ml::KdeSummary summary_;

    KdeStatistic() {
    }

    ~KdeStatistic() {
    }

    void SetZero() {
      postponed_.SetZero();
      summary_.SetZero();
    }

    /**
     * Initializes by taking statistics on raw data.
     */
    template<typename TreeIteratorType>
    void Init(TreeIteratorType &iterator) {
      SetZero();
    }

    /**
     * Initializes by combining statistics of two partitions.
     *
     * This lets you build fast bottom-up statistics when building trees.
     */
    template<typename TreeIteratorType>
    void Init(
      TreeIteratorType &iterator,
      const KdeStatistic *left_stat,
      const KdeStatistic *right_stat) {

      SetZero();
    }
};
};

#endif

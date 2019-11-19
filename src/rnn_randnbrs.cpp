//  rnndescent -- An R package for nearest neighbor descent
//
//  Copyright (C) 2019 James Melville
//
//  This file is part of rnndescent
//
//  rnndescent is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  rnndescent is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with rnndescent.  If not, see <http://www.gnu.org/licenses/>.

#include "rnn_randnbrs.h"
#include "rnn.h"
#include "rnn_parallel.h"
#include "rnn_randnbrsparallel.h"
#include "rnn_rng.h"
#include "tdoann/progress.h"
#include <Rcpp.h>

using namespace tdoann;

/* Macros */

#define RandomNbrs(Distance)                                                   \
  using KnnFactory = KnnBuildFactory<Distance>;                                \
  KnnFactory knn_factory(data);                                                \
  if (parallelize) {                                                           \
    using RandomNbrsImpl = ParallelRandomNbrsImpl<ParallelRandomKnnBuild>;     \
    RandomNbrsImpl impl(block_size, grain_size);                               \
    RandomNbrsKnn(KnnFactory, RandomNbrsImpl, Distance)                        \
  } else {                                                                     \
    using RandomNbrsImpl = SerialRandomNbrsImpl<SerialRandomKnnBuild>;         \
    RandomNbrsImpl impl(block_size);                                           \
    RandomNbrsKnn(KnnFactory, RandomNbrsImpl, Distance)                        \
  }

#define RandomNbrsQuery(Distance)                                              \
  using KnnFactory = KnnQueryFactory<Distance>;                                \
  KnnFactory knn_factory(reference, query);                                    \
  if (parallelize) {                                                           \
    using RandomNbrsImpl = ParallelRandomNbrsImpl<ParallelRandomKnnQuery>;     \
    RandomNbrsImpl impl(block_size, grain_size);                               \
    RandomNbrsKnn(KnnFactory, RandomNbrsImpl, Distance)                        \
  } else {                                                                     \
    using RandomNbrsImpl = SerialRandomNbrsImpl<SerialRandomKnnQuery>;         \
    RandomNbrsImpl impl(block_size);                                           \
    RandomNbrsKnn(KnnFactory, RandomNbrsImpl, Distance)                        \
  }

#define RandomNbrsKnn(KnnFactory, RandomNbrsImpl, Distance)                    \
  return random_knn_impl<KnnFactory, RandomNbrsImpl, Distance>(                \
      k, order_by_distance, knn_factory, impl, verbose);

/* Structs */

template <typename Distance> struct KnnQueryFactory {
  using DataVec = std::vector<typename Distance::in_type>;

  DataVec reference_vec;
  DataVec query_vec;
  int nrow;
  int ndim;

  KnnQueryFactory(Rcpp::NumericMatrix reference, Rcpp::NumericMatrix query)
      : reference_vec(Rcpp::as<DataVec>(Rcpp::transpose(reference))),
        query_vec(Rcpp::as<DataVec>(Rcpp::transpose(query))),
        nrow(query.nrow()), ndim(query.ncol()) {}

  Distance create_distance() const {
    return Distance(reference_vec, query_vec, ndim);
  }

  Rcpp::NumericMatrix create_distance_matrix(int k) const {
    return Rcpp::NumericMatrix(k, nrow);
  }

  Rcpp::IntegerMatrix create_index_matrix(int k) const {
    return Rcpp::IntegerMatrix(k, nrow);
  }
};

template <typename Distance> struct KnnBuildFactory {
  using DataVec = std::vector<typename Distance::in_type>;

  DataVec data_vec;
  int nrow;
  int ndim;

  KnnBuildFactory(Rcpp::NumericMatrix data)
      : data_vec(Rcpp::as<DataVec>(Rcpp::transpose(data))), nrow(data.nrow()),
        ndim(data.ncol()) {}

  Distance create_distance() const { return Distance(data_vec, ndim); }

  Rcpp::NumericMatrix create_distance_matrix(int k) const {
    return Rcpp::NumericMatrix(k, nrow);
  }

  Rcpp::IntegerMatrix create_index_matrix(int k) const {
    return Rcpp::IntegerMatrix(k, nrow);
  }
};

/* Functions */

template <typename KnnFactory, typename RandomNbrsImpl, typename Distance>
Rcpp::List random_knn_impl(int k, bool order_by_distance,
                           KnnFactory &knn_factory, RandomNbrsImpl &impl,
                           bool verbose = false) {
  set_seed();

  auto distance = knn_factory.create_distance();
  auto indices = knn_factory.create_index_matrix(k);
  auto dist = knn_factory.create_distance_matrix(k);

  impl.build_knn(distance, indices, dist, verbose);

  indices = Rcpp::transpose(indices);
  dist = Rcpp::transpose(dist);

  if (order_by_distance) {
    impl.sort_knn(indices, dist);
  }

  return Rcpp::List::create(Rcpp::Named("idx") = indices,
                            Rcpp::Named("dist") = dist);
}

/* Exports */

// [[Rcpp::export]]
Rcpp::List
random_knn_cpp(Rcpp::NumericMatrix data, int k,
               const std::string &metric = "euclidean",
               bool order_by_distance = true, bool parallelize = false,
               std::size_t block_size = 4096, std::size_t grain_size = 1,
               bool verbose = false){DISPATCH_ON_DISTANCES(RandomNbrs)}

// [[Rcpp::export]]
Rcpp::List
    random_knn_query_cpp(Rcpp::NumericMatrix reference,
                         Rcpp::NumericMatrix query, int k,
                         const std::string &metric = "euclidean",
                         bool order_by_distance = true,
                         bool parallelize = false,
                         std::size_t block_size = 4096,
                         std::size_t grain_size = 1, bool verbose = false) {
  DISPATCH_ON_DISTANCES(RandomNbrsQuery)
}

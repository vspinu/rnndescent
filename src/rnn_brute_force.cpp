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

#include <Rcpp.h>
#include "heap.h"
#include "distance.h"
#include "nn_brute_force.h"
#include "rnn.h"
#include "rnn_brute_force_parallel.h"

#define BruteForce(DistType)                                      \
return rnn_brute_force_impl<DistType, RProgress>(                 \
    data, k, parallelize, grain_size, verbose);

template <typename Distance,
          typename Progress>
Rcpp::List rnn_brute_force_impl(
    Rcpp::NumericMatrix data,
    int k,
    bool parallelize = false,
    std::size_t grain_size = 1,
    bool verbose = false)
{
  const std::size_t n_points = data.nrow();
  const std::size_t n_nbrs = k;

  const std::size_t ndim = data.ncol();
  data = Rcpp::transpose(data);
  auto data_vec = Rcpp::as<std::vector<typename Distance::in_type>>(data);

  Progress progress;
  Distance distance(data_vec, ndim);
  ArrayHeap<Distance> heap(distance, n_points, n_nbrs);

  if (parallelize) {
    nnbf_parallel(heap, progress, grain_size, verbose);
  }
  else {
    nnbf(heap, progress, verbose);
  }

  return heap_to_r(heap.neighbor_heap);
}

// [[Rcpp::export]]
Rcpp::List rnn_brute_force(
    Rcpp::NumericMatrix data,
    int k,
    const std::string& metric = "euclidean",
    bool parallelize = false,
    std::size_t grain_size = 1,
    bool verbose = false)
{
  if (metric == "euclidean") {
    using DistType = Euclidean<float, float>;
    BruteForce(DistType)
  }
  else if (metric == "l2") {
    using DistType = L2<float, float>;
    BruteForce(DistType)
  }
  else if (metric == "cosine") {
    using DistType = Cosine<float, float>;
    BruteForce(DistType)
  }
  else if (metric == "manhattan") {
    using DistType = Manhattan<float, float>;
    BruteForce(DistType)
  }
  else if (metric == "hamming") {
    using DistType = Hamming<uint8_t, std::size_t>;
    BruteForce(DistType)
  }
  else {
    Rcpp::stop("Bad metric");
  }
}
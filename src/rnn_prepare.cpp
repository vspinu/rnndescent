//  rnndescent -- An R package for nearest neighbor descent
//
//  Copyright (C) 2021 James Melville
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

#include "tdoann/prepare.h"

#include <Rcpp.h>

#include "rnn_distance.h"
#include "rnn_macros.h"
#include "rnn_rng.h"

using namespace Rcpp;

#define DIVERSIFY_IMPL()                                                       \
  return diversify_impl<Distance>(data, idx, dist, prune_probability);

#define DIVERSIFY_SP_IMPL()                                                    \
  return diversify_sp_impl<Distance>(data, graph_list, prune_probability);

#define DIVERSIFY_ALWAYS_SP_IMPL()                                             \
  return diversify_sp_impl<Distance>(data, graph_list);

template <typename Distance>
List diversify_impl(NumericMatrix data, IntegerMatrix idx, NumericMatrix dist,
                    double prune_probability) {
  auto distance = r_to_dist<Distance>(data);
  auto graph = r_to_graph<Distance>(idx, dist);

  RRand rand;
  auto diversified =
      tdoann::remove_long_edges(graph, distance, rand, prune_probability);

  return graph_to_r(diversified, true);
}

template <typename Distance>
List diversify_sp_impl(NumericMatrix data, List graph_list,
                       double prune_probability) {
  auto distance = r_to_dist<Distance>(data);
  auto graph = r_to_sparse_graph<Distance>(graph_list);

  RRand rand;
  auto diversified =
      tdoann::remove_long_edges_sp(graph, distance, rand, prune_probability);

  return sparse_graph_to_r(diversified);
}

template <typename Distance>
List diversify_sp_impl(NumericMatrix data, List graph_list) {
  auto distance = r_to_dist<Distance>(data);
  auto graph = r_to_sparse_graph<Distance>(graph_list);

  auto diversified = tdoann::remove_long_edges_sp(graph, distance);

  return sparse_graph_to_r(diversified);
}

// [[Rcpp::export]]
List diversify_cpp(NumericMatrix data, IntegerMatrix idx, NumericMatrix dist,
                   const std::string &metric = "euclidean",
                   double prune_probability = 1.0){
    DISPATCH_ON_DISTANCES(DIVERSIFY_IMPL)}

// [[Rcpp::export]]
List diversify_sp_cpp(NumericMatrix data, List graph_list,
                      const std::string &metric = "euclidean",
                      double prune_probability = 1.0){
    DISPATCH_ON_DISTANCES(DIVERSIFY_SP_IMPL)}

// [[Rcpp::export]]
List diversify_always_sp_cpp(NumericMatrix data, List graph_list,
                             const std::string &metric = "euclidean") {
  DISPATCH_ON_DISTANCES(DIVERSIFY_ALWAYS_SP_IMPL)
}

struct Dummy {
  using Output = double;
  using Index = std::size_t;
};

// [[Rcpp::export]]
List merge_graph_lists_cpp(Rcpp::List gl1, Rcpp::List gl2) {
  auto g1 = r_to_sparse_graph<Dummy>(gl1);
  auto g2 = r_to_sparse_graph<Dummy>(gl2);

  auto g_merge = tdoann::merge_graphs(g1, g2);

  return sparse_graph_to_r(g_merge);
}

// [[Rcpp::export]]
List degree_prune_cpp(Rcpp::List graph_list, std::size_t max_degree) {
  auto graph = r_to_sparse_graph<Dummy>(graph_list);
  auto pruned = tdoann::degree_prune(graph, max_degree);
  return sparse_graph_to_r(pruned);
}
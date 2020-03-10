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

#include "RcppPerpendicular.h"
#include "rnn_heapsort.h"
#include "rnn_heaptor.h"
#include "rnn_parallel.h"
#include "rnn_rtoheap.h"
#include "rnn_util.h"

using namespace Rcpp;

struct SerialHeapImpl {
  std::size_t block_size;

  SerialHeapImpl(std::size_t block_size) : block_size(block_size) {}

  template <typename HeapAdd>
  void init(SimpleNeighborHeap &heap, IntegerMatrix nn_idx,
            NumericMatrix nn_dist) {
    r_to_heap_serial<HeapAdd>(heap, nn_idx, nn_dist, block_size);
  }
  void sort_heap(SimpleNeighborHeap &heap) { heap.deheap_sort(); }
};

struct ParallelHeapImpl {
  std::size_t block_size;
  std::size_t grain_size;

  ParallelHeapImpl(std::size_t block_size, std::size_t grain_size)
      : block_size(block_size), grain_size(grain_size) {}

  template <typename HeapAdd>
  void init(SimpleNeighborHeap &heap, IntegerMatrix nn_idx,
            NumericMatrix nn_dist) {
    r_to_heap_parallel<HeapAdd>(heap, nn_idx, nn_dist, block_size, grain_size);
  }
  void sort_heap(SimpleNeighborHeap &heap) {
    sort_heap_parallel(heap, block_size, grain_size);
  }
};

template <typename MergeImpl, typename HeapAdd>
List merge_nn_impl(IntegerMatrix nn_idx1, NumericMatrix nn_dist1,
                   IntegerMatrix nn_idx2, NumericMatrix nn_dist2,
                   MergeImpl &merge_impl, bool verbose = false) {
  SimpleNeighborHeap nn_merged(nn_idx1.nrow(), nn_idx1.ncol());

  if (verbose) {
    ts("Merging graphs");
  }
  merge_impl.template init<HeapAdd>(nn_merged, nn_idx1, nn_dist1);
  merge_impl.template init<HeapAdd>(nn_merged, nn_idx2, nn_dist2);

  merge_impl.sort_heap(nn_merged);
  return heap_to_r(nn_merged);
}

template <typename MergeImpl, typename HeapAdd>
List merge_nn_all_impl(List nn_graphs, MergeImpl &merge_impl,
                       bool verbose = false) {
  auto n_graphs = static_cast<std::size_t>(nn_graphs.size());

  List nn_graph = nn_graphs[0];
  NumericMatrix nn_dist = nn_graph["dist"];
  IntegerMatrix nn_idx = nn_graph["idx"];

  RPProgress progress(n_graphs, verbose);
  SimpleNeighborHeap nn_merged(nn_idx.nrow(), nn_idx.ncol());
  merge_impl.template init<HeapAdd>(nn_merged, nn_idx, nn_dist);
  progress.iter_finished();

  // iterate over other graphs
  for (std::size_t i = 1; i < n_graphs; i++) {
    List nn_graphi = nn_graphs[i];
    NumericMatrix nn_disti = nn_graphi["dist"];
    IntegerMatrix nn_idxi = nn_graphi["idx"];
    merge_impl.template init<HeapAdd>(nn_merged, nn_idxi, nn_disti);
    TDOANN_ITERFINISHED()
  }

  merge_impl.sort_heap(nn_merged);
  return heap_to_r(nn_merged);
}

#define CONFIGURE_MERGE(NEXT_MACRO)                                            \
  if (parallelize) {                                                           \
    using MergeImpl = ParallelHeapImpl;                                        \
    MergeImpl merge_impl(block_size, grain_size);                              \
    if (is_query) {                                                            \
      using HeapAdd = HeapAddQuery;                                            \
      NEXT_MACRO();                                                            \
    } else {                                                                   \
      using HeapAdd = LockingHeapAddSymmetric;                                 \
      NEXT_MACRO();                                                            \
    }                                                                          \
  } else {                                                                     \
    using MergeImpl = SerialHeapImpl;                                          \
    MergeImpl merge_impl(block_size);                                          \
    if (is_query) {                                                            \
      using HeapAdd = HeapAddQuery;                                            \
      NEXT_MACRO();                                                            \
    } else {                                                                   \
      using HeapAdd = HeapAddSymmetric;                                        \
      NEXT_MACRO();                                                            \
    }                                                                          \
  }

#define MERGE_NN()                                                             \
  return merge_nn_impl<MergeImpl, HeapAdd>(nn_idx1, nn_dist1, nn_idx2,         \
                                           nn_dist2, merge_impl, verbose);

#define MERGE_NN_ALL()                                                         \
  return merge_nn_all_impl<MergeImpl, HeapAdd>(nn_graphs, merge_impl, verbose);

// [[Rcpp::export]]
List merge_nn(IntegerMatrix nn_idx1, NumericMatrix nn_dist1,
              IntegerMatrix nn_idx2, NumericMatrix nn_dist2, bool is_query,
              bool parallelize, std::size_t block_size,
              std::size_t grain_size = 1, bool verbose = false) {
  CONFIGURE_MERGE(MERGE_NN);
}

// [[Rcpp::export]]
List merge_nn_all(List nn_graphs, bool is_query, bool parallelize,
                  std::size_t block_size, std::size_t grain_size = 1,
                  bool verbose = false) {
  CONFIGURE_MERGE(MERGE_NN_ALL);
}

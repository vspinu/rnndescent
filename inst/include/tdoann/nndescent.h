// BSD 2-Clause License
//
// Copyright 2019 James Melville
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// OF SUCH DAMAGE.

#ifndef TDOANN_NNDESCENT_H
#define TDOANN_NNDESCENT_H

#include "candidatepriority.h"
#include "graphupdate.h"
#include "heap.h"
#include "nngraph.h"
#include "progress.h"
#include "typedefs.h"

namespace tdoann {
// mark any neighbor in the current graph that was retained in the new
// candidates as false
void flag_retained_new_candidates(NeighborHeap &current_graph,
                                  const NeighborHeap &new_candidate_neighbors,
                                  std::size_t begin, std::size_t end) {
  std::size_t n_nbrs = current_graph.n_nbrs;
  for (auto i = begin; i < end; i++) {
    std::size_t innbrs = i * n_nbrs;
    for (std::size_t j = 0; j < n_nbrs; j++) {
      std::size_t ij = innbrs + j;
      std::size_t idx = current_graph.idx[ij];

      if (new_candidate_neighbors.contains(i, idx)) {
        current_graph.flags[ij] = 0;
      }
    }
  }
}

// overload for serial processing case which does entire graph in one chunk
void flag_retained_new_candidates(NeighborHeap &current_graph,
                                  const NeighborHeap &new_candidate_neighbors) {
  flag_retained_new_candidates(current_graph, new_candidate_neighbors, 0,
                               current_graph.n_points);
}

// This corresponds to the construction of new, old, new' and old' in
// Algorithm 2, with some minor differences:
// 1. old' and new' (the reverse candidates) are build at the same time as old
// and new respectively, based on the fact that if j is a candidate of new[i],
// then i is a reverse candidate of new[j]. This saves on building the entire
// reverse candidates list and then down-sampling.
// 2. Not all old members of current KNN are retained in the old candidates
// list, nor are rho * K new candidates sampled. Instead, the current members
// of the KNN are assigned into old and new based on their flag value, with the
// size of the final candidate list controlled by the maximum size of
// the candidates neighbors lists.
template <typename CandidatePriority>
void build_candidates_full(NeighborHeap &current_graph,
                           CandidatePriority &candidate_priority,
                           NeighborHeap &new_candidate_neighbors,
                           NeighborHeap &old_candidate_neighbors) {
  std::size_t n_points = current_graph.n_points;
  std::size_t n_nbrs = current_graph.n_nbrs;

  for (std::size_t i = 0; i < n_points; i++) {
    std::size_t innbrs = i * n_nbrs;
    for (std::size_t j = 0; j < n_nbrs; j++) {
      std::size_t ij = innbrs + j;
      std::size_t idx = current_graph.idx[ij];

      double d = candidate_priority(current_graph, ij);
      char isn = current_graph.flags[ij];
      if (isn == 1) {
        new_candidate_neighbors.checked_push_pair(i, d, idx, isn);
      } else {
        old_candidate_neighbors.checked_push_pair(i, d, idx, isn);
      }
    }
  }
  flag_retained_new_candidates(current_graph, new_candidate_neighbors);
}

bool is_converged(std::size_t n_updates, double tol) {
  return static_cast<double>(n_updates) <= tol;
}

// Pretty close to the NNDescentFull algorithm (#2 in the paper)
template <typename Distance, typename GUFactoryT, typename Progress,
          typename CandidatePriorityFactory>
NNGraph nnd_build(const std::vector<typename Distance::Input> &data,
                  std::size_t ndim, const NNGraph &nn_init,
                  std::size_t max_candidates, std::size_t n_iters,
                  CandidatePriorityFactory &candidate_priority_factory,
                  double delta, bool verbose) {
  Distance distance(data, ndim);

  std::size_t n_points = nn_init.n_points;
  std::size_t n_nbrs = nn_init.n_nbrs;
  double tol = delta * n_nbrs * n_points;

  NeighborHeap current_graph(n_points, n_nbrs);
  graph_to_heap_serial<HeapAddSymmetric>(current_graph, nn_init, 1000, true);

  Progress progress(current_graph, n_iters, verbose);
  auto graph_updater = GUFactoryT::create(current_graph, distance);

  auto candidate_priority = candidate_priority_factory.create();

  for (std::size_t n = 0; n < n_iters; n++) {
    NeighborHeap new_nbrs(n_points, max_candidates);
    NeighborHeap old_nbrs(n_points, max_candidates);

    build_candidates_full(current_graph, candidate_priority, new_nbrs,
                          old_nbrs);

    if (CandidatePriorityFactory::should_sort) {
      new_nbrs.deheap_sort();
      old_nbrs.deheap_sort();
    }

    std::size_t c = local_join(graph_updater, new_nbrs, old_nbrs, n_points,
                               max_candidates, progress);
    TDOANN_ITERFINISHED();
    if (is_converged(c, tol)) {
      progress.converged(c, tol);
      break;
    }
  }
  current_graph.deheap_sort();

  return heap_to_graph(current_graph);
}

// Local join update: instead of updating item i with the neighbors of the
// candidates of i, explore pairs (p, q) of candidates and treat q as a
// candidate for p, and vice versa.
template <template <typename> class GraphUpdater, typename Distance,
          typename Progress>
std::size_t local_join(GraphUpdater<Distance> &graph_updater,
                       const NeighborHeap &new_nbrs,
                       const NeighborHeap &old_nbrs, std::size_t n_points,
                       std::size_t max_candidates, Progress &progress) {
  progress.set_n_blocks(n_points);
  std::size_t c = 0;
  for (std::size_t i = 0; i < n_points; i++) {
    for (std::size_t j = 0; j < max_candidates; j++) {
      std::size_t p = new_nbrs.index(i, j);
      if (p == NeighborHeap::npos()) {
        continue;
      }
      for (std::size_t k = j; k < max_candidates; k++) {
        std::size_t q = new_nbrs.index(i, k);
        if (q == NeighborHeap::npos()) {
          continue;
        }
        c += graph_updater.generate_and_apply(p, q);
      }

      for (std::size_t k = 0; k < max_candidates; k++) {
        std::size_t q = old_nbrs.index(i, k);
        if (q == NeighborHeap::npos()) {
          continue;
        }
        c += graph_updater.generate_and_apply(p, q);
      }
    }
    TDOANN_BLOCKFINISHED();
  }
  return c;
}

// No local join available when querying because there's no symmetry in the
// distances to take advantage of, so this is similar to algo #1 in the NND
// paper with the following differences:
// 1. The existing "reference" knn graph doesn't get updated during a query,
//    so each query item has no reverse neighbors, only the "forward" neighbors,
//    i.e. the knn.
// 2. The members of the query knn are from the reference knn and they *do* have
//    reverse neighbors, so the overall search is: for each neighbor in the
//    "forward" neighbors (the current query knn), try each of its general
//    neighbors.
// 3. Because the reference knn doesn't get updated during the query, the
//    reference general neighbor list only needs to get built once.
// 4. Incremental search is also simplified. Each member of the query knn
//    is marked as new when it's selected for search as usual, but because of
//    the static nature of the reference general neighbors, we don't need to
//    keep track of old neighbors: if a neighbor is "new" we search all its
//    general neighbors; otherwise, we don't search it at all because we must
//    have already tried those candidates.
template <typename Distance, typename GUFactoryT, typename Progress,
          typename CandidatePriorityFactory>
NNGraph
nnd_query(const std::vector<typename Distance::Input> &reference,
          std::size_t ndim, const std::vector<typename Distance::Input> &query,
          const NNGraph &nn_init, const std::vector<std::size_t> &reference_idx,
          std::size_t max_candidates, std::size_t n_iters,
          CandidatePriorityFactory &candidate_priority_factory, double delta,
          bool verbose) {
  Distance distance(reference, query, ndim);

  std::size_t n_points = nn_init.n_points;
  std::size_t n_nbrs = nn_init.n_nbrs;
  double tol = delta * n_nbrs * n_points;

  NeighborHeap current_graph(n_points, n_nbrs);
  graph_to_heap_serial<HeapAddQuery>(current_graph, nn_init, 1000, true);

  Progress progress(current_graph, n_iters, verbose);
  auto graph_updater = GUFactoryT::create(current_graph, distance);

  auto candidate_priority = candidate_priority_factory.create();

  std::size_t n_ref_points = reference.size() / ndim;
  NeighborHeap gn_graph(n_ref_points, max_candidates);
  build_general_nbrs(reference_idx, gn_graph, candidate_priority, n_ref_points,
                     n_nbrs);
  const bool flag_on_add = max_candidates >= n_nbrs;

  for (std::size_t n = 0; n < n_iters; n++) {
    NeighborHeap new_nbrs(n_points, max_candidates);

    build_query_candidates(current_graph, candidate_priority, new_nbrs,
                           flag_on_add);
    if (!flag_on_add) {
      // Can't be sure all candidates that were pushed were retained, so we
      // check now: mark any neighbor in the current graph that was retained in
      // the new candidates
      flag_retained_new_candidates(current_graph, new_nbrs);
    }
    if (CandidatePriorityFactory::should_sort) {
      new_nbrs.deheap_sort();
    }

    std::size_t c = non_search_query(current_graph, graph_updater, new_nbrs,
                                     gn_graph, max_candidates, progress);

    TDOANN_ITERFINISHED();
    if (is_converged(c, tol)) {
      progress.converged(c, tol);
      break;
    }
  }
  current_graph.deheap_sort();
  return heap_to_graph(current_graph);
}

template <typename CandidatePriority>
void build_general_nbrs(const std::vector<std::size_t> &reference_idx,
                        NeighborHeap &gn_graph,
                        CandidatePriority &candidate_priority,
                        std::size_t n_points, std::size_t n_nbrs) {
  for (std::size_t i = 0; i < n_points; i++) {
    std::size_t innbrs = i * n_nbrs;
    for (std::size_t j = 0; j < n_nbrs; j++) {
      std::size_t ij = innbrs + j;
      double d = candidate_priority(gn_graph, ij);
      std::size_t ref = reference_idx[ij];
      gn_graph.checked_push_pair(i, d, ref);
    }
  }
}

template <typename CandidatePriority>
void build_query_candidates(NeighborHeap &current_graph,
                            CandidatePriority &candidate_priority,
                            NeighborHeap &new_candidate_neighbors,
                            std::size_t begin, std::size_t end,
                            bool flag_on_add) {
  std::size_t n_nbrs = current_graph.n_nbrs;
  for (auto i = begin; i < end; i++) {
    std::size_t innbrs = i * n_nbrs;
    for (std::size_t j = 0; j < n_nbrs; j++) {
      std::size_t ij = innbrs + j;
      char isn = current_graph.flags[ij];
      if (isn == 1) {
        double d = candidate_priority(current_graph, ij);
        new_candidate_neighbors.checked_push(i, d, current_graph.idx[ij], isn);
        if (flag_on_add) {
          current_graph.flags[ij] = 0;
        }
      }
    }
  }
}

template <typename CandidatePriority>
void build_query_candidates(NeighborHeap &current_graph,
                            CandidatePriority &candidate_priority,
                            NeighborHeap &new_candidate_neighbors,
                            bool flag_on_add) {
  std::size_t n_points = current_graph.n_points;
  build_query_candidates(current_graph, candidate_priority,
                         new_candidate_neighbors, 0, n_points, flag_on_add);
}

// Use neighbor-of-neighbor search rather than local join to update the kNN.
template <template <typename> class GraphUpdater, typename Distance,
          typename Progress>
std::size_t non_search_query(NeighborHeap &current_graph,
                             GraphUpdater<Distance> &graph_updater,
                             const NeighborHeap &new_nbrs,
                             const NeighborHeap &gn_graph,
                             std::size_t max_candidates, std::size_t begin,
                             std::size_t end, Progress &progress) {
  std::size_t c = 0;
  std::size_t ref_idx = 0;
  std::size_t nbr_ref_idx = 0;
  std::size_t n_nbrs = current_graph.n_nbrs;
  typename GraphUpdater<Distance>::NeighborSet seen(n_nbrs);

  for (std::size_t query_idx = begin; query_idx < end; query_idx++) {
    for (std::size_t j = 0; j < max_candidates; j++) {
      ref_idx = new_nbrs.index(query_idx, j);
      if (ref_idx == NeighborHeap::npos()) {
        continue;
      }
      std::size_t rnidx = ref_idx * max_candidates;
      for (std::size_t k = 0; k < max_candidates; k++) {
        nbr_ref_idx = gn_graph.idx[rnidx + k];
        if (nbr_ref_idx == NeighborHeap::npos() || seen.contains(nbr_ref_idx)) {
          continue;
        }
        c += graph_updater.generate_and_apply(query_idx, nbr_ref_idx);
      }
    }
    TDOANN_BLOCKFINISHED();
    seen.clear();
  }
  return c;
}

template <template <typename> class GraphUpdater, typename Distance,
          typename Progress>
std::size_t non_search_query(NeighborHeap &current_graph,
                             GraphUpdater<Distance> &graph_updater,
                             const NeighborHeap &new_nbrs,
                             const NeighborHeap &gn_graph,
                             std::size_t max_candidates, Progress &progress) {
  std::size_t n_points = current_graph.n_points;
  progress.set_n_blocks(n_points);
  return non_search_query(current_graph, graph_updater, new_nbrs, gn_graph,
                          max_candidates, 0, n_points, progress);
}
} // namespace tdoann
#endif // TDOANN_NNDESCENT_H

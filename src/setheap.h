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

#ifndef RNND_SETHEAP_H
#define RNND_SETHEAP_H

#include <unordered_set>

#include <boost/functional/hash.hpp>

#include "heap.h"

// Heap function for a pair
template <typename T>
struct pair_hash
{
  std::size_t operator()(
      const std::pair<T, T>& p
  ) const
  {
    std::size_t seed = 0;
    boost::hash_combine(seed, p.first);
    boost::hash_combine(seed, p.second);
    return seed;
  }
};

// Checks for duplicates by storing a set of already-seen pairs. Takes up more
// memory but might be faster if lots of duplicate pairs are expected
template <typename WeightMeasure>
struct SetHeap
{

  NeighborHeap neighbor_heap;
  WeightMeasure weight_measure;
  std::unordered_set<std::pair<std::size_t, std::size_t>,
                     pair_hash<std::size_t>> seen;
  std::size_t npairs;

  SetHeap(WeightMeasure& weight_measure,
          const std::size_t n_points,
          const std::size_t size)
    : neighbor_heap(n_points, size),
      weight_measure(weight_measure),
      seen(),
      npairs(0)
    {}

  unsigned int add_pair(
      std::size_t i,
      std::size_t j,
      bool flag)
  {
    ++npairs;

    if (i > j) {
      std::swap(i, j);
    }

    std::pair<std::size_t, std::size_t> p(i, j);
    if (seen.find(p) != seen.end()) {
      return 0;
    }
    seen.insert(p);

    double d = weight_measure(i, j);

    unsigned int c = 0;
    if (d < neighbor_heap.dist[i][0]) {
      c += neighbor_heap.unchecked_push(i, d, j, flag);
    }
    if (i != j && d < neighbor_heap.dist[j][0]) {
      c += neighbor_heap.unchecked_push(j, d, i, flag);
    }

    return c;
  }
};

#endif // RNND_SETHEAP_H

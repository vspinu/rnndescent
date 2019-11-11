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

// Generic parallel helper code

#ifndef RNN_PARALLEL_H
#define RNN_PARALLEL_H

#include <Rcpp.h>
// [[Rcpp::depends(RcppParallel)]]
#include <RcppParallel.h>

template <typename Progress>
void batch_parallel_for(RcppParallel::Worker &worker, Progress &progress,
                        std::size_t n, std::size_t block_size,
                        std::size_t grain_size) {
  if (n <= block_size) {
    RcppParallel::parallelFor(0, n, worker, grain_size);
  } else {
    std::size_t begin = 0;
    std::size_t end = block_size;
    while (true) {
      if (begin >= n) {
        break;
      }
      RcppParallel::parallelFor(begin, end, worker, grain_size);
      progress.update(end);
      if (progress.check_interrupt()) {
        break;
      }
      begin += block_size;
      end += block_size;
      end = std::min(end, n);
    }
  }
}

#endif // RNN_PARALLEL_H

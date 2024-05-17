/*
 *  This file is part of AQUAgpusph, a free CFD program based on SPH.
 *  Copyright (C) 2012  Jose Luis Cercos Pita <jl.cercos@upm.es>
 *
 *  AQUAgpusph is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  AQUAgpusph is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with AQUAgpusph.  If not, see <http://www.gnu.org/licenses/>.
 */

/** @addtogroup basic
 * @{
 */

/** @file
 *  @brief Sort the multiresolution involved particle properties
 */

#include "resources/Scripts/types/types.h"

/** @brief Sort the multiresolution involved particle properties
 *
 * @param m0_in Unsorted original mass \f$ m_0 \f$.
 * @param m0 Sorted original mass \f$ m_0 \f$.
 * @param miter_in Unsorted iteration of the mass transfer.
 * @param miter Mass transfer iteration (Positive for shrinking particles,
 * negative for growing particles).
 * @param ilevel_in Unsorted particle refinement level.
 * @param ilevel Sorted particle refinement level.
 * @param level_in Unsorted target refinement level.
 * @param level Sorted target refinement level.
 * @param id_sorted Permutations list from the unsorted space to the sorted
 * one.
 * @param N Number of particles.
 */
__kernel void entry(const __global float *m0_in, __global float *m0,
                    const __global int *miter_in, __global int *miter,
                    const __global uint *ilevel_in, __global uint *ilevel,
                    const __global uint *level_in, __global uint *level,
                    const __global usize *id_sorted,
                    usize N)
{
    const usize i = get_global_id(0);
    if(i >= N)
        return;

    const usize i_out = id_sorted[i];

    m0[i_out] = m0_in[i];
    miter[i_out] = miter_in[i];
    ilevel[i_out] = ilevel_in[i];
    level[i_out] = level_in[i];
}

/*
 * @}
 */

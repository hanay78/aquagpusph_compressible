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
 * @brief Buffer particles identify and usage.
 */

#include "resources/Scripts/types/types.h"

/** @brief Identify the buffer particles.
 *
 * The buffer particles are signaled with an imove=-255 flag.
 *
 * @param imove Moving flags (imove = -255 for buffer particles).
 * @param ibuffer 0 if the particle is not a buffer particle, 1 otherwise.
 * @param N Number of particles.
 */
__kernel void count(__global const int* imove,
                    __global unsigned int* ibuffer,
                    usize N)
{
    usize i = get_global_id(0);
    if(i >= N)
        return;

    if(imove[i] == -255)
        ibuffer[i] = 1;
    else
        ibuffer[i] = 0;
}

/** @brief Replace the particles with the imove flag imove=-256 by imove=-255.
 *
 * The particles with imove=-256 are "invalid particles", i.e. particles which
 * has been removed (e.g. particles out of the computational domain), while
 * imove=-255 are buffer particles.
 *
 * In that way, the removed particles in a time step are not taken into account
 * as buffer particles until they are sorted at the next time step.
 *
 * @param imove Moving flags (imove = -255 for buffer particles).
 * @param ibuffer 0 if the particle is not a buffer particle, 1 otherwise.
 * @param N Number of particles.
 */
__kernel void set_imove(__global int* imove,
                        usize N)
{
    const usize i = get_global_id(0);
    if(i >= N)
        return;

    if(imove[i] == -256)
        imove[i] = -255;
}

/*
 * @}
 */

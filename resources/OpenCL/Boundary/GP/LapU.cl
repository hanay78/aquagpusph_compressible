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

/** @file
 * @brief Ghost particles Laplacian of the velocity computation.
 */

#if defined(LOCAL_MEM_SIZE) && defined(NO_LOCAL_MEM)
    #error NO_LOCAL_MEM has been set.
#endif

#ifndef HAVE_3D
    #include "types/2D.h"
    #include "KernelFunctions/Wendland2D.hcl"
#else
    #include "types/3D.h"
    #include "KernelFunctions/Wendland3D.hcl"
#endif

#if __LAP_FORMULATION__ == __LAP_MONAGHAN__
    #ifndef HAVE_3D
        #define __CLEARY__ 8.f
    #else
        #define __CLEARY__ 10.f
    #endif
#endif

/** @brief Ghost particles Laplacian of the velocity computation.
 *
 * This method compute the Laplacian of the velocity also in the mirroring
 * boundary elements because such information will be required to extend the
 * pressure field.
 *
 * @param imove Moving flags.
 *   - imove > 0 for regular fluid particles.
 *   - imove = 0 for sensors.
 *   - imove < 0 for boundary elements/particles.
 * @param r Position \f$ \mathbf{r} \f$.
 * @param u Velocity \f$ \mathbf{u} \f$.
 * @param rho Density \f$ \rho \f$.
 * @param m Mass \f$ m \f$.
 * @param lap_u Velocity laplacian \f$ \frac{\Delta \mathbf{u}}{rho} \f$.
 * @param icell Cell where each particle is located.
 * @param ihoc Head of chain for each cell (first particle found).
 * @param N Number of particles.
 * @param n_cells Number of cells in each direction
 */
__kernel void main(const __global int* imove,
                   const __global vec* r,
                   const __global vec* u,
                   const __global float* rho,
                   const __global float* m,
                   __global vec* lap_u,
                   // Link-list data
                   const __global uint *icell,
                   const __global uint *ihoc,
                   // Simulation data
                   uint N,
                   uivec4 n_cells)
{
    const uint i = get_global_id(0);
    const uint it = get_local_id(0);
    if(i >= N)
        return;
    const int imove_i = imove[i];
    if((imove_i != 1) || (imove_i != -2)){
        return;
    }

    const vec_xyz r_i = r[i].XYZ;
    const vec_xyz u_i = u[i].XYZ;
    const float rho_i = rho[i];

    // Initialize the output
    #ifndef LOCAL_MEM_SIZE
        #define _LAPU_ lap_u[i].XYZ
    #else
        #define _LAPU_ lap_u_l[it]
        __local vec_xyz lap_u_l[LOCAL_MEM_SIZE];
        _LAPU_ = lap_u[i].XYZ;
    #endif

    BEGIN_LOOP_OVER_NEIGHS(){
        const int imove_j = imove[j];
        if((imove_j != 1) || (imove_j != -1)){
            j++;
            continue;
        }
        // Don't take into account the fluid-fluid interactions
        if(imove_i == imove_j){
            j++;
            continue;
        }
        const vec_xyz r_ij = r[j].XYZ - r_i;
        const float q = fast_length(r_ij) / H;
        if(q >= SUPPORT)
        {
            j++;
            continue;
        }
        {
            const float rho_j = rho[j];
            const float udr = dot(u[j].XYZ - u_i, r_ij);
            const float f_ij = kernelF(q) * CONF * m[j];

            #if __LAP_FORMULATION__ == __LAP_MONAGHAN__
                const float r2 = (q * q + 0.01f) * H * H;
                _LAPU_ += f_ij * __CLEARY__ * udr / (r2 * rho_i * rho_j) * r_ij;
            #elif __LAP_FORMULATION__ == __LAP_MORRIS__
                _LAPU_ += f_ij * 2.f / (rho_i * rho_j) * (u[j].XYZ - u_i);
            #else
                #error Unknown Laplacian formulation: __LAP_FORMULATION__
            #endif
        }
    }END_LOOP_OVER_NEIGHS()

    #ifdef LOCAL_MEM_SIZE
        lap_u[i].XYZ = _LAPU_;
    #endif
}

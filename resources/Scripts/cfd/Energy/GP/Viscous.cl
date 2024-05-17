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
 * @brief Tool to compute the fluid viscous mechanincal energy induced by a
 * solid boundary in the fluid particles.
 */

#if defined(LOCAL_MEM_SIZE) && defined(NO_LOCAL_MEM)
    #error NO_LOCAL_MEM has been set.
#endif

#include "resources/Scripts/types/types.h"
#include "resources/Scripts/KernelFunctions/Kernel.h"

#if __LAP_FORMULATION__ == __LAP_MONAGHAN__
    #ifndef HAVE_3D
        #define __CLEARY__ 8.f
    #else
        #define __CLEARY__ 10.f
    #endif
#endif

/** @brief Tool to compute the viscous energy due to the interaction with a
 * body.
 *
 * See the following paper:
 *
 * M. Antuono, S. Marrone, A. Colagrossi, B. Bouscasse, "Energy balance in the
 * delta-SPH scheme". Computer methods in applied mechanincs and engineering,
 * vol 289, pp 209-226, 2015.
 *
 * @param GP_energy_delapudt Energy induced by the ghost particles due to the
 * viscous term.
 * @param iset Set of particles index.
 * @param imove Moving flags.
 *   - imove > 0 for regular fluid particles.
 *   - imove = 0 for sensors.
 *   - imove < 0 for boundary elements/particles.
 * @param r Position \f$ \mathbf{r} \f$.
 * @param u Velocity \f$ \mathbf{u} \f$.
 * @param rho Density \f$ \rho \f$.
 * @param m Mass \f$ m \f$.
 * @param visc_dyn Dynamic viscosity \f$ \mu \f$.
 * @param icell Cell where each particle is located.
 * @param ihoc Head of chain for each cell (first particle found).
 * @param N Number of particles.
 * @param n_cells Number of cells in each direction
 * @param GP_energy_iset Ghost particles set to be considered.
 */
__kernel void main(__global float* GP_energy_delapudt,
                   const __global uint* iset,
                   const __global int* imove,
                   const __global vec* r,
                   const __global vec* u,
                   const __global float* rho,
                   const __global float* m,
                   __constant float* visc_dyn,
                   usize N,
                   uint GP_energy_iset,
                   LINKLIST_LOCAL_PARAMS)
{
    const usize i = get_global_id(0);
    const usize it = get_local_id(0);
    if(i >= N)
        return;

    if(imove[i] != 1){
        GP_energy_delapudt[i] = 0.f;
        return;
    }

    const vec_xyz r_i = r[i].XYZ;
    const vec_xyz u_i = u[i].XYZ;
    const float visc_dyn_i = visc_dyn[iset[i]];

    // Initialize the output
    #ifndef LOCAL_MEM_SIZE
        #define _E_LAPU_ GP_energy_delapudt[i]
    #else
        #define _E_LAPU_ delapudt_l[i]
        __local float delapudt_l[LOCAL_MEM_SIZE];
    #endif
    _E_LAPU_ = 0.f;

    const usize c_i = icell[i];
    BEGIN_NEIGHS(c_i, N, n_cells, icell, ihoc){
        if((iset[j] != GP_energy_iset) || (imove[j] != -1)){
            j++;
            continue;
        }
        const vec_xyz r_ij = r[j].XYZ - r_i;
        const float q = length(r_ij) / H;
        if(q >= SUPPORT)
        {
            j++;
            continue;
        }

        {
            const float f_ij = kernelF(q) * CONF * m[j] / rho[j];
            #if __LAP_FORMULATION__ == __LAP_MONAGHAN__
                const float r2 = (q * q + 0.01f) * H * H;
                const float udr = dot(u[j].XYZ - u_i, r_ij);
                _E_LAPU_ += dot(u_i, f_ij * __CLEARY__ * udr / r2 * r_ij);
            #elif __LAP_FORMULATION__ == __LAP_MORRIS__
                _E_LAPU_ += dot(u_i, f_ij * 2.f * (u[j].XYZ - u_i));
            #else
                #error Unknown Laplacian formulation: __LAP_FORMULATION__
            #endif
        }
    }END_NEIGHS()

    _E_LAPU_ *= visc_dyn_i * m[i] / rho[i];

    #ifdef LOCAL_MEM_SIZE
        GP_energy_delapudt[i] = _E_LAPU_;
    #endif
}

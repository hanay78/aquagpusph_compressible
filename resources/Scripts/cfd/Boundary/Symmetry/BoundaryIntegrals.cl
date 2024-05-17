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
 * @brief Boundary integral term computation.
 */

#if defined(LOCAL_MEM_SIZE) && defined(NO_LOCAL_MEM)
    #error NO_LOCAL_MEM has been set.
#endif

#include "resources/Scripts/types/types.h"
#include "resources/Scripts/KernelFunctions/Kernel.h"

/** @brief Performs the boundary effect on the fluid particles.
 * @param iset Set of particles index.
 * @param imove Moving flags.
 *   - imove > 0 for regular fluid particles.
 *   - imove = 0 for sensors.
 *   - imove < 0 for boundary elements/particles.
 * @param imirrored 0 if the particle has not been mirrored, 1 otherwise.
 * @param r Position \f$ \mathbf{r} \f$.
 * @param rmirrored Mirrored position of the particle, \a r if \a imirrored is
 * false (0).
 * @param normal Normal \f$ \mathbf{n} \f$.
 * @param nmirrored Mirrored normal of the particle, \a normal if \a imirrored
 * is false (0).
 * @param u Velocity \f$ \mathbf{u} \f$.
 * @param umirrored Mirrored velocity of the particle, \a u if \a imirrored is
 * false (0).
 * @param rho Density \f$ \rho \f$.
 * @param p Pressure \f$ p \f$.
 * @param mass Mass \f$ m \f$.
 * @param refd Density of reference \f$ \rho_0 \f$ (one per set of particles)
 * @param grad_p Pressure gradient \f$ \nabla p \f$.
 * @param div_u Velocity divergence \f$ \nabla \cdot \mathbf{u} \f$.
 * @param icell Cell where each particle is located.
 * @param ihoc Head of chain for each cell (first particle found).
 * @param N Number of particles.
 * @param n_cells Number of cells in each direction
 * @param g Gravity acceleration \f$ \mathbf{g} \f$.
 */
__kernel void entry(const __global uint* iset,
                    const __global int* imove,
                    const __global int* imirrored,
                    const __global vec* r,
                    const __global vec* rmirrored,
                    const __global vec* normal,
                    const __global vec* nmirrored,
                    const __global vec* u,
                    const __global vec* umirrored,
                    const __global float* rho,
                    const __global float* m,
                    const __global float* p,
                    __constant float* refd,
                    __global vec* grad_p,
                    __global float* div_u,
                    usize N,
                    vec g,
                    LINKLIST_LOCAL_PARAMS)
{
    const usize i = get_global_id(0);
    const usize it = get_local_id(0);
    if(i >= N)
        return;
    if((!imirrored[i]) || (imove[i] != 1))
        return;

    const vec_xyz r_i = r[i].XYZ;
    const vec_xyz u_i = u[i].XYZ;
    const float p_i = p[i];
    const float rho_i = rho[i];
    const float refd_i = refd[iset[i]];

    // Initialize the output
    #ifndef LOCAL_MEM_SIZE
        #define _GRADP_ grad_p[i].XYZ
        #define _DIVU_ div_u[i]
    #else
        #define _GRADP_ grad_p_l[it]
        #define _DIVU_ div_u_l[it]
        __local vec_xyz grad_p_l[LOCAL_MEM_SIZE];
        __local float div_u_l[LOCAL_MEM_SIZE];
        _GRADP_ = grad_p[i].XYZ;
        _DIVU_ = div_u[i];
    #endif

    const usize c_i = icell[i];
    BEGIN_NEIGHS(c_i, N, n_cells, icell, ihoc){
        if((!imirrored[j]) || (imove[j] != -3)){
            j++;
            continue;
        }
        const vec_xyz r_ij = rmirrored[j].XYZ - r_i;
        const float q = length(r_ij) / H;
        if(q >= SUPPORT)
        {
            j++;
            continue;
        }

        // const float rho_j = rho[j];
        // if(rho_j <= 0.01f * refd_i){
        //     j++;
        //     continue;
        // }

        {
            const vec_xyz n_j = nmirrored[j].XYZ;  // Assumed outwarding oriented
            const float area_j = m[j];
            const float p_j = p[j];
            const vec_xyz du = umirrored[j].XYZ - u_i;
            const float w_ij = kernelW(q) * CONW * area_j;

            _GRADP_ += (p_i + p_j) / rho_i * w_ij * n_j;
            _DIVU_ += rho_i * dot(du, n_j) * w_ij;
        }
    }END_NEIGHS()

    #ifdef LOCAL_MEM_SIZE
        grad_p[i].XYZ = _GRADP_;
        div_u[i] = _DIVU_;
    #endif
}

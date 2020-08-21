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
 * @brief Gaussian kernel definition (3D version). A kernelS function cannot
 * be efficiently provided, since it requires computing erf(q)
 */

#ifndef _KERNEL_H_INCLUDED_
#define _KERNEL_H_INCLUDED_

#ifndef M_PI
    /** @def M_PI
     * \f$ \pi \f$ value.
     */
    #define M_PI 3.14159265359f
#endif
#ifndef iM_PI
    /** @def iM_PI
     * \f$ \frac{1}{\pi} \f$ value.
     */
    #define iM_PI 0.318309886f
#endif

/** @brief The kernel value
 * \f$ W \left(\mathbf{r_j} - \mathbf{r_i}; h\right) \f$.
 * @param q Normalized distance \f$ \frac{\mathbf{r_j} - \mathbf{r_i}}{h} \f$.
 * @return Kernel value.
 */
float kernelW(float q)
{
    return 0.179587122f*exp(-q*q);               // 0.179587122f = 1/(pi^(3/2))
}

/** @brief The kernel gradient factor
 * \f$ F \left(\mathbf{r_j} - \mathbf{r_i}; h\right) \f$
 *
 * The factor \f$ F \f$ is defined such that
 * \f$ \nabla W \left(\mathbf{r_j} - \mathbf{r_i}; h\right) =
       \frac{\mathbf{r_j} - \mathbf{r_i}}{h} \cdot
       F \left(\mathbf{r_j} - \mathbf{r_i}; h\right)
   \f$.
 *
 * @param q Normalized distance \f$ \frac{\mathbf{r_j} - \mathbf{r_i}}{h} \f$.
 * @return Kernel amount
 */
float kernelF(float q)
{
    return 0.359174244f*exp(-q*q);               // 0.359174244f = 2/(pi^(3/2))
}

/** @brief The kernel partial derivative with respect to the characteristic
 * height \f$ \frac{\partial W}{\partial h} \f$
 *
 * The result returned by this function should be multiplied by
 * \f$ \frac{1}{h^{d + 1}} \f$, where d is 2,3 for 2D and 3D respectively.
 *
 * @param q Normalized distance \f$ \frac{\mathbf{r_j} - \mathbf{r_i}}{h} \f$.
 * @return Kernel partial derivative factor
 * @note This function is useful for variable resolution (non-constant kernel
 * height)
 * @see Iason Zisis, Bas van der Linden, Christina Giannopapa and Barry Koren,
 * On the derivation of SPH schemes for shocks through inhomogeneous media. Int.
 * Jnl. of Multiphysics (2015).
 */
float kernelH(float q)
{
    return 0.359174244f*(q*q - 1.f)*exp(-q*q);   // 0.359174244f = 2/(pi^(3/2))
}

#endif    // _KERNEL_H_INCLUDED_

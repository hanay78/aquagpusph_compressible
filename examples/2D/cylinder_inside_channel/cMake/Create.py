#! /usr/bin/env python
#########################################################################
#                                                                       #
#            #    ##   #  #   #                           #             #
#           # #  #  #  #  #  # #                          #             #
#          ##### #  #  #  # #####  ##  ###  #  #  ## ###  ###           #
#          #   # #  #  #  # #   # #  # #  # #  # #   #  # #  #          #
#          #   # #  #  #  # #   # #  # #  # #  #   # #  # #  #          #
#          #   #  ## #  ##  #   #  ### ###   ### ##  ###  #  #          #
#                                    # #             #                  #
#                                  ##  #             #                  #
#                                                                       #
#########################################################################
#
#  This file is part of AQUA-gpusph, a free CFD program based on SPH.
#  Copyright (C) 2012  Jose Luis Cercos Pita <jl.cercos@upm.es>
#
#  AQUA-gpusph is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  AQUA-gpusph is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with AQUA-gpusph.  If not, see <http://www.gnu.org/licenses/>.
#
#########################################################################

import os.path as path
import math

# Input data
# ==========

g = 0.0
hfac = 4.0
cs = 50.0
courant = 0.2
gamma = 1.0
refd = 1.0
alpha = 0.0
delta = 1.0
visc_dyn = 0.001
U = 1.0
# Channel dimensions
L = 1.0
H = 0.5 * L
# Number of fluid particles in y direction
ny = 100

# Distance between particles
# ==========================
sep = 2.0
dr = H / ny
h = hfac * dr
domain_min = (-2.0 * sep * h, -(0.5 * H + sep * h))
domain_max = (L + 3.0 * sep * h, 0.5 * H + 2.0 * sep * h)

# Ammount of required buffer particles
# ====================================
n_buffer_x = int(2.0 * sep * hfac) + 1
n_buffer_y = ny
n_buffer = n_buffer_x * n_buffer_y

# Artificial viscosity
# ====================
visc_dyn = max(alpha / 8.0 * refd * hfac * dr * cs, visc_dyn)

# Particles generation
# ====================
prb = cs * cs * refd / gamma

print("Opening fluid particles output file...")
output = open("Fluid.dat", "w")
string = """#############################################################
#                                                           #
#    #    ##   #  #   #                           #         #
#   # #  #  #  #  #  # #                          #         #
#  ##### #  #  #  # #####  ##  ###  #  #  ## ###  ###       #
#  #   # #  #  #  # #   # #  # #  # #  # #   #  # #  #      #
#  #   # #  #  #  # #   # #  # #  # #  #   # #  # #  #      #
#  #   #  ## #  ##  #   #  ### ###   ### ##  ###  #  #      #
#                            # #             #              #
#                          ##  #             #              #
#                                                           #
#############################################################
"""
output.write(string)
print(string)

string = """
    Writing fluid particles...
"""
print(string)

n = 0
Percentage = -1
x = 0.5 * dr - sep * h
while x < L + sep * h:
    percentage = int(round((x + sep * h) / (L + 2.0 * sep * h) * 100))
    if Percentage != percentage:
        Percentage = percentage
        if not Percentage % 10:
            string = '    {}%'.format(Percentage)
            print(string)
    y = -0.5 * H + 0.5 * dr
    while y < 0.5 * H:
        n += 1
        imove = 1
        vx = U
        vy = 0.0
        press = refd * g * (H - y)
        dens = pow(press / prb + 1.0, 1.0 / gamma) * refd
        mass = refd * dr**2.0
        string = ("{} {}, " * 4 + "{}, {}, {}, {}\n").format(
            x, y,
            0.0, 0.0,
            vx, vy,
            0.0, 0.0,
            dens,
            0.0,
            mass,
            imove)
        output.write(string)
        y += 0.5 * dr
    x += 0.5 * dr

string = """
    Writing the boundary elements...
"""
print(string)

Percentage = -1
x = 0.5 * dr - sep * h
while x < L - 0.5 * dr + sep * h:
    percentage = int(round((x + sep * h) / (L + 2.0 * sep * h) * 100))
    if Percentage != percentage:
        Percentage = percentage
        if not Percentage % 10:
            string = '    {}%'.format(Percentage)
            print(string)
    for i, y in enumerate([-0.5 * H, 0.5 * H]):
        n += 1
        imove = -3
        vx = U
        vy = 0.0
        nx = 0
        ny = 1 if i else -1
        press = refd * g * (H - y)
        dens = pow(press / prb + 1.0, 1.0 / gamma) * refd
        mass = dr
        string = ("{} {}, " * 4 + "{}, {}, {}, {}\n").format(
            x, y,
            nx, ny,
            vx, vy,
            0.0, 0.0,
            dens,
            0.0,
            mass,
            imove)
        output.write(string)
    x += 0.5 * dr

string = """
    Writing buffer particles...
"""
print(string)

Percentage = -1
x = domain_max[0] + sep * h
y = domain_max[1] + sep * h
for i in range(n_buffer):
    percentage = int(round((i + 1.0) / n_buffer * 100))
    if Percentage != percentage:
        Percentage = percentage
        if not Percentage % 10:
            string = '    {}%'.format(Percentage)
            print(string)
    n += 1
    imove = 0
    vx = 0.0
    vy = 0.0
    press = 0.0
    dens = refd
    mass = refd * dr**2.0
    string = ("{} {}, " * 4 + "{}, {}, {}, {}\n").format(
        x, y,
        0.0, 0.0,
        vx, vy,
        0.0, 0.0,
        dens,
        0.0,
        mass,
        imove)
    output.write(string)
output.close()
print('{} particles written'.format(n))

# XML definition generation
# =========================

templates_path = path.join('@EXAMPLE_DEST_DIR@', 'templates')
XML = ('Fluids.xml', 'Main.xml', 'Settings.xml', 'SPH.xml', 'Time.xml')

domain_min = str(domain_min).replace('(', '').replace(')', '')
domain_max = str(domain_max).replace('(', '').replace(')', '')

data = {'DR':str(dr), 'HFAC':str(hfac), 'CS':str(cs), 'COURANT':str(courant),
        'DOMAIN_MIN':domain_min, 'DOMAIN_MAX':domain_max, 'GAMMA':str(gamma),
        'REFD':str(refd), 'VISC_DYN':str(visc_dyn), 'DELTA':str(delta),
        'G':str(g), 'N':str(n), 'L':str(L)}
for fname in XML:
    # Read the template
    f = open(path.join(templates_path, fname), 'r')
    txt = f.read()
    f.close()
    # Replace the data
    for k in data.keys():
        txt = txt.replace('{{' + k + '}}', data[k])
    # Write the file
    f = open(fname, 'w')
    f.write(txt)
    f.close()
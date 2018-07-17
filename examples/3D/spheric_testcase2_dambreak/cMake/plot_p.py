#******************************************************************************
#                                                                             *
#              *    **   *  *   *                           *                 *
#             * *  *  *  *  *  * *                          *                 *
#            ***** *  *  *  * *****  **  ***  *  *  ** ***  ***               *
#            *   * *  *  *  * *   * *  * *  * *  * *   *  * *  *              *
#            *   * *  *  *  * *   * *  * *  * *  *   * *  * *  *              *
#            *   *  ** *  **  *   *  *** ***   *** **  ***  *  *              *
#                                      * *             *                      *
#                                    **  *             *                      *
#                                                                             *
#******************************************************************************
#                                                                             *
#  This file is part of AQUAgpusph, a free CFD program based on SPH.          *
#  Copyright (C) 2012  Jose Luis Cercos Pita <jl.cercos@upm.es>               *
#                                                                             *
#  AQUAgpusph is free software: you can redistribute it and/or modify         *
#  it under the terms of the GNU General Public License as published by       *
#  the Free Software Foundation, either version 3 of the License, or          *
#  (at your option) any later version.                                        *
#                                                                             *
#  AQUAgpusph is distributed in the hope that it will be useful,              *
#  but WITHOUT ANY WARRANTY; without even the implied warranty of             *
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
#  GNU General Public License for more details.                               *
#                                                                             *
#  You should have received a copy of the GNU General Public License          *
#  along with AQUAgpusph.  If not, see <http://www.gnu.org/licenses/>.        *
#                                                                             *
#******************************************************************************

import sys
import os
from os import path
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation


def readFile(filepath):
    """ Read and extract data from a file
    :param filepath File ot read
    """
    abspath = filepath
    if not path.isabs(filepath):
        abspath = path.join(path.dirname(path.abspath(__file__)), filepath)
    # Read the file by lines
    f = open(abspath, "r")
    lines = f.readlines()
    f.close()
    data = []
    for l in lines[1:-1]:  # Skip the last line, which may be unready
        l = l.strip()
        while l.find('  ') != -1:
            l = l.replace('  ', ' ')
        fields = l.split(' ')
        try:
            data.append(map(float, fields))
        except:
            continue
    # Transpose the data
    return [list(d) for d in zip(*data)]


lines = []


def update(frame_index):
    data = readFile('sensors.out')
    t = data[0]
    pp = (data[1], data[3], data[5], data[7])
    for i, p in enumerate(pp):
        lines[i].set_data(t, p)


fig = plt.figure()
ax11 = fig.add_subplot(221)
ax21 = fig.add_subplot(222)
ax12 = fig.add_subplot(223)
ax22 = fig.add_subplot(224)
axes = (ax11, ax21, ax12, ax22)

FNAME = path.join('@EXAMPLE_DEST_DIR@', 'test_case_2_exp_data.dat')
T,P1,P2,P3,P4,P5,P6,P7,P8,_,_,_,_, = readFile(FNAME)
exp_t = T
exp_p = (P1, P3, P5, P7)
titles = ('P1', 'P3', 'P5', 'P7')

for i, ax in enumerate(axes):
    t = [0.0]
    p = [0.0]
    line, = ax.plot(t,
                    p,
                    label=r'$p_{SPH}$',
                    color="black",
                    linewidth=1.0)
    lines.append(line)
    ax.plot(exp_t,
            exp_p[i],
            label=r'$p_{Exp}$',
            color="red",
            linewidth=1.0)
    # Set some options
    ax.grid()
    ax.legend(loc='best')
    ax.set_title(titles[i])
    ax.set_xlim(0, 6)
    ax.set_ylim(-1000, 15000)
    ax.set_autoscale_on(False)
    ax.set_xlabel(r"$t \, [\mathrm{s}]$", fontsize=21)
    ax.set_ylabel(r"$p \, [\mathrm{Pa}]$", fontsize=21)

ani = animation.FuncAnimation(fig, update, interval=5000)
plt.show()

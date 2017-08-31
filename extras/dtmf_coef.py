###############################################################################
# Copyright (c) 2017, PulseRain Technology LLC 
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License (LGPL) as 
# published by the Free Software Foundation, either version 3 of the License,
# or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but 
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY 
# or FITNESS FOR A PARTICULAR PURPOSE.  
# See the GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
###############################################################################



###############################################################################
# Remarks:
#     Python script to generate the 2*cos(w) for Goertzel Algorithm
###############################################################################

import math

N = 256
sample_rate = 8000

scale_factor = 16384


dtmf_freq = [697, 770, 852, 941, 1209, 1336, 1477, 1633]
  
for i in range(8):
    k = round(N * dtmf_freq[i] / sample_rate)
    coef = 2 * math.cos (2 * 3.1415927 * k / N)
    coef_int = round(coef * scale_factor)
    print ("i = ", i, "k = ", k, "coef = ", coef, "coef_int = ", coef_int)
    
    
print ("\n\n\n")

# second harmonic    
for i in range(8):
    k = round(N * dtmf_freq[i] * 2 / sample_rate)
    coef = 2 * math.cos (2 * 3.1415927 * k / N)
    coef_int = round(coef * scale_factor)
    print ("i = ", i, "k = ", k, "coef = ", coef, "coef_int = ", coef_int)
    
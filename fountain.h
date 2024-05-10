/*
 *      (C) 2024 Petra Csereoka <petra.csereoka@cs.upt.ro>
 *       
 *      This software is used internally at the Politehnica University of Timisoara to upload files through data diodes and recover the missing packets.
 *      It is based on Beej's Guide on Network Programming and uses code snippets from Numerical Recipes by William H. Press, Saul A. Teukolsky,
 *      William T. Vetterling and Brian P. Flannery.
 *
 *      Principal Investigator: Alin-Adrian Anton <alin.anton@cs.upt.ro>
 *      Project members: Razvan-Dorel Cioarga <razvan.cioarga@cs.upt.ro>
 *                       Eugenia Capota <eugenia.capota@cs.upt.ro>
 *                       Petra Csereoka <petra.csereoka@cs.upt.ro>
 *                       Bianca Gusita <bianca.gusita@cs.upt.ro>
 *
 *      This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation,
 *      either version 3 of the License, or (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *      See the GNU General Public License for more details.
 *      You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>. 
 *
 *      An unofficial Romanian translation of the GNU General Public License is available here: <https://staff.cs.upt.ro/~gnu/Licenta_GPL-3-0_RO.html>.                                        
*/ 

#ifndef __FOUNTAIN_LIB__
#define __FOUNTAIN_LIB__

#include <stdio.h>
#include <time.h>
#include <stdint.h>

/* v is initial value (IV) for pseudorandom generator from Numerical Recipes The Art of Scientific Computing 3rd edition */
/* Ranq1 supports 10^12 calls, we only need one good shuffle */

void seed(uint64_t seed);

uint32_t int32();
uint64_t int64();

uint32_t Random32(uint64_t seed);
uint64_t Random64(uint64_t seed);

double RandomDouble();

void swap32(uint32_t *a, uint32_t *b);

void printArray32(uint32_t arr[], uint32_t n);
void printlineArray32(uint32_t arr[], uint32_t n);

// Knuth version of Fisher-Yates
void shuffle32(uint32_t arr32[], uint32_t n);
void indexed_shuffle32(uint32_t arr32[], uint32_t lookup[], uint32_t n);

#endif

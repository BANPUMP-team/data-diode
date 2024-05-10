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

#include "fountain.h"

uint64_t v = 4101842887655102017LL;
uint64_t vv = 2685821657736338717LL;

void seed(uint64_t seed) {
	Random32(seed);
}

uint32_t int32() { 
	return (uint32_t) int64(); 
} 

uint64_t int64() {
	v ^= v >> 21; 
	v ^= v << 35; 
	v ^= v >> 4;
	return v * vv;
}

uint32_t Random32(uint64_t seed) {
	v ^= seed;
	v = int64();

	return (uint32_t) v;
}

uint64_t Random64(uint64_t seed) {
	v ^= seed;
	v = int64();

	return v;
}

double RandomDouble() { 
	return 5.42101086242752217E-20 * int64(); 
}

void swap32(uint32_t *a, uint32_t *b) {
	uint32_t temp = *a;
	*a = *b;
	*b = temp;
}
 
void printArray32(uint32_t arr[], uint32_t n) {
	uint32_t i;
	for (i = 0; i < n; i++) 
		printf("[%d] = %u\n", i, arr[i]);
	printf("\n");
}

void printlineArray32(uint32_t arr[], uint32_t n) {
	uint32_t i;
	
	for (i = 0; i < n; i++) 
		printf("%u ", arr[i]);
	printf("\n");
}
 
void shuffle32(uint32_t arr32[], uint32_t n) // Knuth version of Fisher-Yates
{
	uint32_t i,j;

	for (i = n-1; i > 0; i--) {
		// Pick a random 32bit index from 0 to i
        	j = int64() % (i+1);
        	swap32(&arr32[i], &arr32[j]);
	}
}

void indexed_shuffle32(uint32_t arr32[], uint32_t lookup[], uint32_t n) // Knuth version of Fisher-Yates
{
	uint32_t i,j;

	for (i = n-1; i > 0; i--) {
		// Pick a random 32bit index from 0 to i
        	j = int64() % (i+1);
			swap32(&lookup[arr32[i]], &lookup[arr32[j]]);
        	swap32(&arr32[i], &arr32[j]);
	}
}

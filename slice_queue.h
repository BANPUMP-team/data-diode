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

#ifndef __QUEUE_FOUNTAIN__
#define __QUEUE_FOUNTAIN__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

struct queue_elem {
    uint32_t value;
    struct queue_elem *nxt;
};
typedef struct queue_elem Qnode_t;

struct queue_elems {
    Qnode_t *head;
    Qnode_t *tail;
};
typedef struct queue_elems Queue_t;

Queue_t *createQueue(void);
void pushNode(Queue_t *q, uint32_t value);
Qnode_t *popNode(Queue_t *q);
uint8_t peekQueue(Queue_t *q);
void printQueue(Queue_t *q);

#endif

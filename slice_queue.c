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

#include "slice_queue.h"

Qnode_t *createNode(uint32_t val) {
    Qnode_t *temp = (Qnode_t *)malloc(sizeof(Qnode_t));
    temp->value = val;
    temp->nxt = NULL;
    return temp;
}
 
Queue_t *createQueue(void) {
    Queue_t *q = (Queue_t *)malloc(sizeof(Queue_t));
    q->head = NULL;
    q->tail = NULL;
    return q;
}
 
void pushNode(Queue_t *q, uint32_t value) {
    Qnode_t *temp = createNode(value);
 
    if (q->tail == NULL) {
        q->head = temp;
        q->tail = temp;
    }
    else {
        q->tail->nxt = temp;
        q->tail = temp;
    }
}
 
Qnode_t *popNode(Queue_t *q) {
    if (q->head == NULL)
        return NULL;
 
    Qnode_t *temp = q->head;
    q->head = q->head->nxt;
 
    if (q->head == NULL)
        q->head = NULL;
 
    return temp;
}

uint8_t peekQueue(Queue_t *q) {
    if(q->head == NULL)
        return 0;
    return 1;
}

void printQueue(Queue_t *q) {
    Qnode_t *nod = q->head;
    printf("Queue components: ");
    while(nod != NULL) {
        printf("%d, ", nod->value);
    }
    printf("\n");
}


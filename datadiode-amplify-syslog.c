/*
 *      (C) 2024 Alin Anton <alin.anton@cs.upt.ro>
* 
 *      This software servers as an example of how to amplify and pipe syslog UDP messages through optical data diodes in order to mitigate for
 *      UDP packet loss. 
 *
 *      It is based on "Beej's Guide on Network Programming". 
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


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>

#define AMPFACTOR 1000   // send each line AMPFACTOR times using AMPFACTOR packets
		       
// listener port
#define MYPORT "1514"    // 514 default SYSLOG port requires root, RFC 5424, need to drop privileges and maybe chroot

// destination port for datadiode-deamplify514
#define SERVERPORT "2514"    // the port users will be connecting to, same 514 from RFC 5424
			     
#define MAXBUFLEN 1024 + sizeof(uint16_t)  // needs jumbo frames for longer lines like 8192+2, set MTU to 9000 on data-diode interfaces

uint16_t counter=0;   // this normally overflows, as it should. we just want to clear duplicate packets on the receiver side

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void)
{
    int sockfd, sockfdout,i;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes,len;
    struct sockaddr_storage their_addr;
    char buf[MAXBUFLEN];
    socklen_t addr_len;
//    char s[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6; // set to AF_INET6 to use IPv6
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, MYPORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("listener: socket");
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("listener: bind");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "listener: failed to bind socket\n");
        return 2;
    }

    freeaddrinfo(servinfo);

    printf("listener: waiting to recvfrom...\n");
    addr_len = sizeof their_addr;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6; // set to AF_INET6 to use IPv6
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo("localhost", SERVERPORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and make a socket
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfdout = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("talker: socket");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "talker: failed to create socket\n");
        return 2;
    }

    counter = 0;
    char *ptr = buf + sizeof(uint16_t);
    uint8_t *conv = (uint8_t *) &counter;
    while (1) {  // infinite loop, in UDP packets may be lost so this is preferred 
	if ((numbytes = recvfrom(sockfd, ptr, MAXBUFLEN-1-sizeof(uint16_t) , 0, 
	    (struct sockaddr *)&their_addr, &addr_len)) == -1) {
		perror("recvfrom");
	}
	ptr[numbytes] = '\0';
	len = numbytes;

	
	for (i=0; i<sizeof(uint16_t); i++) buf[i] = conv[i];

	for (i=0; i<AMPFACTOR; i++) {
		if ((numbytes = sendto(sockfdout, buf, len-1+sizeof(uint16_t), 0, p->ai_addr, p->ai_addrlen)) == -1) { // send AMPFACTOR times
        		perror("talker: sendto");
		}


	}
	counter++; // this normally overflows
    }

    /* 
     * we do not reach here 
     */

    freeaddrinfo(servinfo);
    close(sockfd); close(sockfdout);

    return 0;
}









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

// listener port
#define MYPORT "2514"    // 514 default SYSLOG port requires root, RFC 5424, need to drop privileges and maybe chroot

// destination port for datadiode-deamplify514
#define SERVERPORT "514"    // the port users will be connecting to, same 514 from RFC 5424
			     
#define MAXBUFLEN 1024 + sizeof(uint16_t)  // needs jumbo frames for longer lines like 8192+2, set MTU to 9000 on data-diode interfaces

uint16_t counter=65535, prevcounter=0;   // this normally overflows, as it should. we just want to clear duplicate packets on the receiver side

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

uint16_t uint8_to_uint16(uint8_t high_byte, uint8_t low_byte) {
    uint16_t result;
    uint8_t *ptr = (uint8_t *)&result;

    // Store high_byte in the appropriate position based on endianness
    #if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && defined(__ORDER_LITTLE_ENDIAN__)
        #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            ptr[0] = high_byte;
            ptr[1] = low_byte;
        #elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            ptr[0] = low_byte;
            ptr[1] = high_byte;
        #else
            #error "Unknown endianness!"
        #endif
    #else
        // Handle endianness at runtime if compiler-specific macros are not available
        union {
            uint16_t u16;
            uint8_t u8[2];
        } endian_test = {0x0102};
        
        if (endian_test.u8[0] == 0x01) { // Big-endian
            ptr[0] = high_byte;
            ptr[1] = low_byte;
        } else if (endian_test.u8[0] == 0x02) { // Little-endian
            ptr[0] = low_byte;
            ptr[1] = high_byte;
        } else {
            #error "Unknown endianness!"
        }
    #endif

    return result;
}

int main(void)
{
    int sockfd, sockfdout;
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

    prevcounter = 65535;
    char *ptr = buf + sizeof(uint16_t);
    while (1) {  // infinite loop, in UDP packets may be lost so this is preferred 
	if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1, 0, 
	    (struct sockaddr *)&their_addr, &addr_len)) == -1) {
		perror("recvfrom");
	}
	buf[numbytes] = '\0';
	len = numbytes;
	counter=uint8_to_uint16((uint8_t) buf[0], (uint8_t) buf[1]);

	if (counter != prevcounter) { // skip duplicate (amplified) lines
		if ((numbytes = sendto(sockfdout, ptr, len-sizeof(uint16_t), 0, p->ai_addr, p->ai_addrlen)) == -1) { // send to syslog
       			perror("talker: sendto");
		}
		prevcounter = counter;
	}
    }

    /* 
     * we do not reach here 
     */

    freeaddrinfo(servinfo);
    close(sockfd); close(sockfdout);

    return 0;
}









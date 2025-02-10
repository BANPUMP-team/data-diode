/*
 *      (C) 2024, 2025 Petra Csereoka <petra.csereoka@cs.upt.ro>
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
 *                       Catalin Cereanu <catalin.cereanu@cs.upt.ro> (student)
 *			 Honu Andrei (student)
 *      This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation,
 *      either version 3 of the License, or (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *      See the GNU General Public License for more details.
 *      You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>. 
 *
 *      An unofficial Romanian translation of the GNU General Public License is available here: <https://staff.cs.upt.ro/~gnu/Licenta_GPL-3-0_RO.html>.                                        
*/ 

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <sys/select.h>
#include <time.h>

/* CHUNK_SIZE is MTU > MAXBUFLEN */
#define CHUNK_SIZE 1500 // 1048576 1 MB chunks for efficient high-speed transfer
#define TARGET_MBPS 900  // Target bandwidth in Megabits per second
			
/* verbose debug information */
//#define DEBUG
/* basic debug information */
//#define DEBUG2

#include "fountain.h"
#define SEED 777		
uint8_t SPRAY = 6;
uint8_t CLEAR_SPRAY = SPRAY; // can be SPRAY/2+1
uint8_t XOR_GROUP_SIZE = 4; 

/* protocol description 
*		File ID 				: 100 bytes		-> FILEIDLEN
*		File size				: 4 bytes		-> TOTALLEN
*		Part Number				: 4 bytes		-> PARTLEN
*		Data					: 1364 bytes 	-> DATALEN
*		TOTAL => 1364 + 2*4 + 100 = 1472		-> MAXBUFLEN
*
*		std: max 1500 to not fragment, need 28 to store IPv4+UDP header 
*			-> max_payload = 1472 
*/

#define FILEIDLEN 100
#define TOTALLEN 4
#define PARTLEN 4
#define DATALEN 1364
#define MAXBUFLEN 1472

uint64_t total_bytes = 0;  // Total bytes processed, maximum is 18.4 exabytes
struct timespec start_time; // for bandwidth pacing

// contain information related to networking
typedef struct {
	struct addrinfo *dest;
	struct addrinfo *res;
	int socketfd;
	char port[13];
	char IP[20];
} destination_t;

// contain information related to data packets
typedef struct {
	char *file_path;
	uint32_t file_size;
	uint32_t part_no;
	unsigned char *data;
} packet_t;
/* part_no:
	0  		= checksum
	-1 		= EOF packet
	1..N 	= packet with index
*/

uint32_t fnv_hash (void* key, uint32_t len) {
    unsigned char* p = (unsigned char *)key;
    uint32_t h = 2166136261;
    uint32_t i;
    for (i = 0; i < len; i++)
        h = (h*16777619) ^ p[i];
    return h;
}

// configure socket related steps
void get_socket(destination_t *dest) {
	int status = 1;
	struct addrinfo hints;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET; 
	hints.ai_socktype = SOCK_DGRAM; // UDP

	if ((status = getaddrinfo(dest->IP, dest->port, &hints, &(dest->res))) != 0) {		
		fprintf(stderr, "[sender] getaddrinfo failed: %s\n", gai_strerror(status));	
		exit(1);
	}
	
	// loop through results and choose the first valid
	int sockfd = -1;
	for(dest->dest = dest->res; dest->dest != NULL; dest->dest = (dest->dest)->ai_next) {
		if ((sockfd = socket((dest->dest)->ai_family, (dest->dest)->ai_socktype, (dest->dest)->ai_protocol)) == -1) {
			perror("[sender] socket failed");
			continue;
		}
		break;
	}
		
	// check if managed to lock
	if((dest->dest) == NULL) {
		fprintf(stderr, "[sender] Failed to initiate socket setup");
		exit(2);		
	}
	
	#ifdef DEBUG2
		printf("[sender] Finished config for IP: %s and port: %s\n", dest->IP, dest->port);
	#endif 

	dest->socketfd = sockfd;
}

// randomize slice indexes to create xor groups
uint32_t *prepare_fountain(uint32_t slices) {
	// configure fountain seed
	seed(SEED);
	
	// prepare indexes
	uint32_t *index = (uint32_t*)malloc(slices * sizeof(uint32_t));
	if(index == NULL) {
		perror("[sender] prepare_fountain failed to allocate\n");
		exit(3);
	}
	for (uint32_t i=0; i<slices; i++) 
		*(index + i) = i;
	
	// create random pairings of 4 slices
	shuffle32(index, slices);

	#ifdef DEBUG
		printArray32(index, slices);
	#endif

	return index;
}

// get data chunk from input file at specified location
void fill_clear_data(int fd, uint32_t part, unsigned char *data_clear) {
	memset(data_clear, 0, DATALEN * sizeof(char));
	
	// get data from file - padded with zero if less than DATALEN at last chunk
	int n = 0;
	lseek(fd, part*DATALEN, SEEK_SET);
	if((n = read(fd, data_clear, DATALEN)) < 0) {
		perror("[sender] read failed");
		exit(4);
	}
}

// build xored data for the current slice
void fill_xor_data(int fd, uint32_t *index, uint32_t group, uint32_t slices, unsigned char *data_xored) {
	uint32_t slice_index[XOR_GROUP_SIZE];
	for(uint8_t i=0; i<XOR_GROUP_SIZE; i++) {
		slice_index[i] = index[(group+i) % slices];
	}
	#ifdef DEBUG2
		printf("%d Xor group { %d, %d, %d, %d}\n", group, slice_index[0], slice_index[1], slice_index[2], slice_index[3]);
	#endif
	
	memset(data_xored, 0, DATALEN * sizeof(char));
	unsigned char data[DATALEN];
	int n = 0;
	
	// store first batch as it is
	lseek(fd, slice_index[0] * DATALEN, SEEK_SET);
	if((n = read(fd, data_xored, DATALEN)) < 0) {
		perror("[sender] read failed");
		exit(5);
	}
		
	// get data from file
	for(uint8_t i=1; i<XOR_GROUP_SIZE; i++) {
		lseek(fd, slice_index[i] * DATALEN, SEEK_SET);
		if((n = read(fd, data, DATALEN)) < 0) {
			perror("[sender] xor_data read failed");
			exit(6);
		}
		// perform XOR - at last slice, xor until how much was read
		// at receive clears padded with 0 -> neutral at xor
		for(uint32_t j=0; j<n; j++) {
			*(data_xored + j) = *(data_xored + j) ^ data[j];
		}
	}
}

// build checksum
unsigned char *get_checksum(int fd, uint32_t slices) {
	unsigned char *checksum = (unsigned char *)malloc(DATALEN * sizeof(char));
	if(checksum == NULL) {
		perror("[sender] xor_data failed to allocate\n");
		exit(7);
	}
	memset(checksum, 0, DATALEN * sizeof(char));
	
	unsigned char data[DATALEN];
	int n = 0;
	
	// store first batch as it is
	lseek(fd, 0, SEEK_SET);
	if((n = read(fd, checksum, DATALEN)) < 0) {
		perror("[sender] read failed");
		exit(8);
	}
		
	// get rest from file
	for(uint32_t i=1; i<slices; i++) {
		if((n = read(fd, data, DATALEN)) < 0) {
			perror("[sender] xor_data read failed");
			exit(9);
		}
		// perform XOR - at last slice, xor until how much was read
		// at receive clears padded with 0 -> neutral at xor
		for(uint32_t j=0; j<n; j++) {
			*(checksum + j) = *(checksum + j) ^ data[j];
		}
	}

	#ifdef DEBUG2
		printf("Checksum computation finished.\n");
	#endif
	
	return checksum;
}

// serialize information from the struct into packet data field
void serialize(packet_t packet, unsigned char *msg) { //FIXME reversed
	memset(msg, 0, MAXBUFLEN);

	// copy fileid
	uint32_t LEN = strlen (packet.file_path);
	LEN = LEN < FILEIDLEN? LEN : FILEIDLEN;
	for(uint32_t i=0; i<LEN; i++)
		*(msg + i) = *(packet.file_path + i);
	
	// copy file size
	uint32_t offset = FILEIDLEN;
	for(uint32_t i=0; i<sizeof(uint32_t); i++){				// divide into bytes: uint32_t -> 4 bytes	
		*(msg + offset + i) = ((packet.file_size) >> ((3-i)*8)) & 0xFF;
	}
	
	// copy part number
	offset = FILEIDLEN + TOTALLEN;
	for(uint32_t i=0; i<sizeof(uint32_t); i++){				// divide into bytes: uint32_t -> 4 bytes	
		*(msg + offset + i) = ((packet.part_no) >> ((3-i)*8)) & 0xFF;
	}

	// copy data content
	offset = FILEIDLEN + TOTALLEN + PARTLEN;
	for(uint32_t i=0; i<DATALEN; i++){				
		*(msg + offset + i) = *(packet.data + i);
	}

	#ifdef DEBUG
		printf("Serialization finished for packet [%d].\n", packet.part_no);
	#endif
}

// send a slice from a file to the receiver
void send_slice(int sockfd, unsigned char *msg, struct addrinfo *dest) {
	int numbytes = 0;
	struct timespec current_time;
		
	if ((numbytes = sendto(sockfd, msg, MAXBUFLEN, 0, dest->ai_addr, dest->ai_addrlen)) == -1) { 
		perror("[sender] sendto failed for socket1");
		exit(10);
	}

	total_bytes += numbytes;

   // Calculate the expected time to process CHUNK_SIZE at TARGET_MBPS
 //   double target_time_per_chunk = (8.0 * CHUNK_SIZE) / (TARGET_MBPS * 1000000.0);  // in seconds

    // Calculate the expected elapsed time (in seconds) for the amount of data sent
	double expected_time = (total_bytes * 8.0) / (TARGET_MBPS * 1000000.0);

	// Get current time and compute actual elapsed time
	if (clock_gettime(CLOCK_MONOTONIC, &current_time) != 0) {
	    perror("clock_gettime");
	    exit(EXIT_FAILURE);
	}
	double elapsed_time = (current_time.tv_sec - start_time.tv_sec) +
        	              (current_time.tv_nsec - start_time.tv_nsec) / 1e9;

	// If we’re ahead of schedule, sleep for the remaining time
	if (elapsed_time < expected_time) {
	    double sleep_time = expected_time - elapsed_time;
	    struct timeval delay;
	    delay.tv_sec = (int)sleep_time;
	    delay.tv_usec = (sleep_time - delay.tv_sec) * 1e6;
	    select(0, NULL, NULL, NULL, &delay);
	}

	#ifdef DEBUG
		printf("numbytes sendto = %d\n ->", numbytes);
		for(int64_t i=0; i<MAXBUFLEN; i++)
			printf("[%ld]: %x\n", i, msg[i]);
	#endif
}

// send over the file with clear, xored and checksum type packets
void send_file(char *file_path, destination_t *dest_clear, destination_t *dest_xored, destination_t *dest_check) {
	// compute number of packets
	struct stat st;
	if(lstat(file_path, &st) == -1) {
		perror("[sender] lstat failed");
		exit(11);	
	}
	uint32_t slices = (st.st_size + (DATALEN - 1))/ DATALEN; //round up

	// prepare file for processing
	int fd = open(file_path, O_RDONLY);
	if(fd == -1) {
		perror("[sender] open failed");
		exit(12);
	}
	
	// build checksum
	unsigned char *checksum = get_checksum(fd, slices);

	uint32_t len = strlen(file_path); 	
	uint32_t hash = fnv_hash(file_path, len);
	srand(hash); 

	// file too small pad with zeroes
	if(slices < XOR_GROUP_SIZE) {
		slices = XOR_GROUP_SIZE;
	}
	printf("[INFO] %s file_size=%lu slices=%u\n", file_path, st.st_size, slices);

	// prepare indices for fountain codes
	uint32_t *index = prepare_fountain(slices);
	
	/* BUILD DATA PACKETS */
	packet_t msg;
	unsigned char *pack = (unsigned char *)malloc(MAXBUFLEN * sizeof(char));
	if(pack == NULL) {
		perror("[sender] packet failed to allocate\n");
		exit(13);
	}
	
	// add file ID
	char *p = NULL;
	p = strrchr(file_path, '/'); 
	if(p == NULL) p = file_path;
	else p++;
	msg.file_path = p;
	
	// add total file size
	msg.file_size = st.st_size;

	// allocate message field
	unsigned char *databuf = (unsigned char *)malloc(DATALEN * sizeof(char));
	if(databuf == NULL) {
		perror("[sender] msg.data failed to allocate\n");
		exit(14);
	}
	
    /* === NEW LOOP: Send the full file in clear, sequentially === */
	for (uint32_t s = 0; s < slices; s++) {
		msg.part_no = s + 1;  
		// Use slice index+1 as part number (parts are numbered from 1)
		fill_clear_data(fd, msg.part_no - 1, databuf);  
		msg.data = databuf;
		// Read the slice from file at position s
		serialize(msg, pack);  
     		// Build the packet (file id, size, part number, and data)
		send_slice(dest_clear->socketfd, pack, dest_clear->dest);  
		// Send over clear channel, this call must be bw paced
		usleep(100); //usleep(100);
	}

	fprintf(stderr, "Sent the sequencial packets.\n");
	fprintf(stderr, "Wait half a second...\n");
	usleep(500000); // wait half a second
	fprintf(stderr, "Now sending shuffled clear/XORed packets mix + checksum\n");

	// add part number and content corresponding to each slice
	uint32_t rounds = (slices + (10 - 1))/ 10;		// 10% of the slices rounded up
	uint32_t parts1 = 0, parts2 = 0;

	// send: checksum -> 10% clear -> checksum -> 10% xored | repeat 10 times
	for(uint32_t i=0; i<10; i++) {	
		
		{	
			// send checksum
			msg.part_no = 0;
			msg.data = checksum;
			serialize(msg, pack);
			send_slice(dest_check -> socketfd, pack, dest_check -> dest);
		}
		
		// send packets in clear ; make CLEAR_SPRAY=1 here
		msg.data = databuf;
		for(uint32_t j=0; j<rounds*CLEAR_SPRAY; j++) {
			if(parts1 >= slices*CLEAR_SPRAY) 	// skip rest of the cycle if already sent all packets
				break;
			//msg.part_no = i*rounds + j + 1; ---> for in order transmission
			msg.part_no = rand() % slices + 1;
			fill_clear_data(fd, msg.part_no - 1, databuf);	
			serialize(msg, pack);
			send_slice(dest_clear -> socketfd, pack, dest_clear -> dest);
			parts1++;
		}
		
		{	
			// send checksum
			msg.part_no = 0;
			msg.data = checksum;
			serialize(msg, pack);
			send_slice(dest_check -> socketfd, pack, dest_check -> dest);
		}
		
		// send packets in xor mode
		msg.data = databuf;
		for(uint32_t j=0; j<rounds*SPRAY; j++) {
			if(parts2 >= slices*SPRAY) 	// skip rest of the cycle if already sent all packets
				break;
			//msg.part_no = i*rounds + j + 1; ---> for in order transmission
			msg.part_no = rand() % slices + 1;
			fill_xor_data(fd, index, msg.part_no-1, slices, databuf);	
			serialize(msg, pack);
			send_slice(dest_xored -> socketfd, pack, dest_xored -> dest);
			parts2++;
		}
	}

	fprintf(stderr, "Done sending shuffled clear/XORed packets mix.\n");
	fprintf(stderr, "Now sending 1000 EOF packets for 10 seconds..\n");
	// send EOF packet 
	for(uint32_t j=0; j<10000; j++)
	{
		msg.part_no = (unsigned)(-1);
		msg.data = checksum;
		serialize(msg, pack);
		send_slice(dest_check -> socketfd, pack, dest_check -> dest);
		usleep(1000); // could use send_slice paced at 70 Mbps
	}
	
	fprintf(stderr, "Finished sending EOF.\n");
	fprintf(stderr, "Done.\n");

	/* CLEAN UP */
	free(index);
	free(databuf);
	free(checksum);
	free(pack);
	
	if(close(fd) == -1) {
		perror("[sender] close failed");
		exit(15);
	}
}

int main(int argc, char *argv[]) {

	// process data from outside
	if(argc != 6) {
		fprintf(stderr, "[usage] <program> <IP> <port> <filename> <xor-size> <spray>\n");
		fprintf(stderr, "[usage] File will be sent on 3 consecutive ports starting with <port> at %u Mbps\n", TARGET_MBPS);
		exit(16);
	}
		
	/* CONFIGURE SOCKET RELATED ELEMENTS */
	destination_t dest_clear, dest_xored, dest_check;
	char receiver_port[13];
	
	// configure target for clear packets
	strcpy(dest_clear.port, argv[2]);
	strcpy(dest_clear.IP, argv[1]);
	get_socket(&dest_clear);
	
	// configure target for xored packets
	int iport = atoi(argv[2]); 
	iport++; snprintf(receiver_port, 12, "%d", iport);
	strcpy(dest_xored.port, receiver_port);
	strcpy(dest_xored.IP, argv[1]);
	get_socket(&dest_xored);
	
	// configure target for checksum
	iport++; snprintf(receiver_port, 12, "%d", iport);
	strcpy(dest_check.port, receiver_port);
	strcpy(dest_check.IP, argv[1]);
	get_socket(&dest_check);

	// configure fountain related elements
	XOR_GROUP_SIZE = atoi(argv[4]);
	SPRAY = atoi(argv[5]);
	
	/* SEND FILE */
	// Get the start time for the overall copy
	if (clock_gettime(CLOCK_MONOTONIC, &start_time) != 0) {
	    perror("clock_gettime");
	    exit(EXIT_FAILURE);
	}
	send_file(argv[3], &dest_clear, &dest_xored, &dest_check);

	/* CLEAN UP */
	freeaddrinfo(dest_clear.res);
	freeaddrinfo(dest_xored.res);
	freeaddrinfo(dest_check.res);
	
	if(close(dest_clear.socketfd) == -1) {
		perror("[sender] close socketfd failed");
		exit(17);
	}
	
	if(close(dest_xored.socketfd) == -1) {
		perror("[sender] close socketfd failed");
		exit(18);
	}
	
	if(close(dest_check.socketfd) == -1) {
		perror("[sender] close socketfd failed");
		exit(19);
	}
	
	return 0;
}

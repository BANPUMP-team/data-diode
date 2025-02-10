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
 *
 *  Increase UDP send and receive buffers to 2GB if you can afford it:
 *
*/

#define _GNU_SOURCE
#include <netinet/in.h>
#include <errno.h>
#include <sys/poll.h>
#include <sys/types.h>
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
#include <pthread.h>
#include <sched.h>
#include <errno.h>


/* verbose debug information */
//#define DEBUG

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
#define MAGICNUMBER 42

int set_affinity_thread(int core_id) {
   int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
   if (core_id < 0 || core_id >= num_cores)
      return -1;

   cpu_set_t cpuset;
   CPU_ZERO(&cpuset);
   CPU_SET(core_id, &cpuset);

   pthread_t current_thread = pthread_self();    
   return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

// contain information related to networking
typedef struct {
	struct addrinfo *dest;
	struct addrinfo *res;
	int socketfd;
	char port[13];
} destination_t;

// receiving thread arguments
typedef struct {
	char *file_path;
	char *temp_folder;
	char *slice_path;
	destination_t *dest;
	void (*process_information)(void *, unsigned char *);
	int core;
} receive_thread_arg_t;

// configure socket related things
void get_socket(destination_t *dest) {
	int status;
	struct addrinfo hints;
	int yes = 1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET; 
	hints.ai_socktype = SOCK_DGRAM; // UDP
	hints.ai_flags = AI_PASSIVE; 

	if ((status = getaddrinfo(NULL, dest->port, &hints, &(dest->res))) != 0) {		
		fprintf(stderr, "[receiver] getaddrinfo failed: %s\n", gai_strerror(status));	
		exit(1);
	}
	
	// loop through results and choose the first valid
	int sockfd = -1;
	for(dest->dest = dest->res; dest->dest != NULL; dest->dest = (dest->dest)->ai_next) {
		if ((sockfd = socket((dest->dest)->ai_family, (dest->dest)->ai_socktype, (dest->dest)->ai_protocol)) == -1) {
			perror("[receiver] socket failed");
			continue;
		}
		
		// clear hanging so can reuse port immediately
 		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
			perror("[receiver] setsockopt failed");
			freeaddrinfo(dest->res);
			exit(2);
		}
		
		// bind needed for receiver, but not for sender
		if (bind(sockfd, (dest->dest)->ai_addr, (dest->dest)->ai_addrlen) == -1) {
			close(sockfd);
			perror("[receiver] bind failed");
			continue;
		}
		
		break;
	}
		
	// check if managed to lock
	if((dest->dest) == NULL) {
		fprintf(stderr, "[receiver] Failed to initiate socket setup");
		exit(3);		
	}
	
	printf("[receiver] Finished config for port: %s\n", dest->port);
	
	dest->socketfd = sockfd;
}

// store checksum in local file
void process_checksum(void *arg, unsigned char *buf) {
	receive_thread_arg_t *args = (receive_thread_arg_t *)(arg);
	
	// get part number from packet
	uint32_t part_no = 0;
	unsigned char *p = buf + FILEIDLEN + TOTALLEN;
	for(uint8_t i=0; i<sizeof(uint32_t); i++){			// build from bytes: uint32_t <- 4 bytes	
		part_no = (part_no << 8) | (*(p+i));
	}
	if(part_no == (unsigned)(-1)) {
		// create local file for inotify but ONLY if file is not already recovered
		struct stat str;
		char pathr[256];
		snprintf(pathr, 255, "%s/%.100s", args->temp_folder, buf);
		if(!stat(pathr, &str) == 0) {
			char inotifypath[256];
			snprintf(inotifypath, 255, "%s/%.100s.finished", args->temp_folder, buf);
			int res = open(inotifypath, O_RDWR | O_CREAT | O_EXCL, 0666);
			if (res != -1) {
				printf("[INFO] ********* EOF packet detected: %.100s *********\n", buf);
				close(res);
			}
			else {	
				if(errno == EEXIST)
					return;
				else {
					perror("[recovery] Failed to create tempfile");
					exit(4);
				}
			}
		} else return; // checksum is also sent multiple times, not necessary if file is recovered 
	}
	
	// check if file already exists -> then no longer store
	struct stat st;
	char path[256];
	snprintf(path, 255, "%s/%.100s%s", args->temp_folder, buf, args->file_path);
	if(stat(path, &st) == 0)
		return;
	
	// store fileid, file size and checksum
	int fd = open(path, O_RDWR | O_CREAT, 0666);
	if(fd == -1) {
		perror("[receiver] open failed for checksum");
		exit(5);
	}
	
	// save file name from packet
	int n;
	p = buf;
	if((n = write(fd, p, FILEIDLEN)) < 0) {
		perror("[receiver] write failed for checksum");
		exit(6);
	}
	
	// save file size from packet
	p = buf + FILEIDLEN;
	if((n = write(fd, p, TOTALLEN)) < 0) {
		perror("[receiver] write failed for checksum");
		exit(7);
	}
	
	// save actual checksum
	p = buf + FILEIDLEN + TOTALLEN + PARTLEN;
	if((n = write(fd, p, DATALEN)) < 0) {
		perror("[receiver] write failed for checksum");
		exit(8);
	}
	
	// clean up
	if(close(fd) == -1) {
		perror("[receiver] close failed for checksum");
		exit(9);
	}
}

// store clear / xor data in local files
void process_data(void *arg, unsigned char *buf) {
	receive_thread_arg_t *args = (receive_thread_arg_t *)(arg);
	
	// get part number from packet
	uint32_t part_no = 0;
	unsigned char *p = buf + FILEIDLEN + TOTALLEN;
	for(uint8_t i=0; i<sizeof(uint32_t); i++){			// build from bytes: uint32_t <- 4 bytes	
		part_no = (part_no << 8) | (*(p+i));
	}

	// build local files' names
	char data_path[256], slice_path[256];
	snprintf(data_path, 255, "%s/%.100s%s", args->temp_folder, buf, args->file_path);
	snprintf(slice_path, 255, "%s/%.100s%s", args->temp_folder, buf, args->slice_path);

	// check if slice is already present
	int fd_slice = open(slice_path, O_RDWR | O_CREAT, 0666);
	if(fd_slice == -1) {
		perror("[receiver] open failed for slice");
		exit(10);
	}
	char aux = 0;
	uint32_t offset = part_no - 1;			// part_no 1..N -> 0..N-1
	lseek(fd_slice, offset, SEEK_SET);	
	if(read(fd_slice, &aux, 1) == 1 && aux == MAGICNUMBER) {
		// slice exists -> skip
		if(close(fd_slice) == -1) {
			perror("[receiver] close failed for slice");
			exit(11);
		}
		return;
	}
	else {
		// new slice -> mark in slice file
		aux = MAGICNUMBER;
		lseek(fd_slice, offset, SEEK_SET);
		if(write(fd_slice, &aux, 1) != 1) {
			perror("[receiver] write failed for slice");
			exit(12);
		}
	}
	
	// store data slice in local file
	int fd_data = open(data_path, O_RDWR | O_CREAT, 0666);
	if(fd_data == -1) {
		perror("[receiver] open failed for data");
		exit(13);
	}
	int n = 0;
	p = buf + FILEIDLEN + TOTALLEN + PARTLEN;
	lseek(fd_data, (part_no-1) * DATALEN, SEEK_SET);
	if((n = write(fd_data, p, DATALEN)) != DATALEN) {
		perror("[receiver] write less than DATALEN");
		exit(14);
	}
	
	// clean up
	if(close(fd_data) == -1) {
		perror("[receiver] close failed for data");
		exit(15);
	}
	if(close(fd_slice) == -1) {
		perror("[receiver] close failed for slice");
		exit(16);
	}
}

void *thread_routine(void *arg) {
	receive_thread_arg_t *args = (receive_thread_arg_t *)(arg);
	struct sockaddr_storage their_addr;
	socklen_t addr_len = sizeof(their_addr);
	
	printf("[receiver] Thread starting\n");
	
	set_affinity_thread(args->core);
	
	unsigned char buf[MAXBUFLEN] = {0};
	int64_t numbytes;
	int sockfd = (args->dest)->socketfd;
	
	while(1) {
		if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN, 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
			perror("[receiver] recvfrom failed");
			exit(17);
		}	
		args->process_information(arg, buf);
	}
	
	printf("[receiver] Thread exiting\n");
	pthread_exit(NULL);	
}

int main(int argc, char *argv[]) {

	// process data from outside
	if(argc != 3) {
		fprintf(stderr, "[usage] <program> <port> <temp-folder>\n");
		fprintf(stderr, "[usage] File will be received on 3 consecutive ports starting with <port>\n");
		exit(18);
	}

	// local temporary storage for file slices
	char subpaths[5][256];
	strcpy(subpaths[0], "_clear_data.in");
	strcpy(subpaths[1], "_xor_data.in");
	strcpy(subpaths[2], "_checksum.in");
	strcpy(subpaths[3], "_clear_list.in");
	strcpy(subpaths[4], "_xor_list.in");
	
	
	/* CONFIGURE SOCKET RELATED THINGS */
	destination_t dest_clear, dest_xored, dest_check;
	char receiver_port[13];
	
	// configure target for clear packets
	strncpy(dest_clear.port, argv[1], 12);
	get_socket(&dest_clear);
	
	// configure target for xored packets
	int iport = atoi(argv[1]); 
	iport++; snprintf(receiver_port, 12, "%d", iport);
	strcpy(dest_xored.port, receiver_port);
	get_socket(&dest_xored);
	
	// configure target for checksum
	iport++; snprintf(receiver_port, 12, "%d", iport);
	strcpy(dest_check.port, receiver_port);
	get_socket(&dest_check);
	
	/* RECEIVE FILES */
	pthread_t threadID[3];
	receive_thread_arg_t arg[3];
	int ret;
	
	// create thread to receive clear packets on the first port
	arg[0].dest = &dest_clear;
	arg[0].file_path = subpaths[0];
	arg[0].temp_folder = argv[2];
	arg[0].slice_path = subpaths[3];
	arg[0].process_information = process_data;
	arg[0].core = 0;
	ret = pthread_create(&threadID[0], NULL, thread_routine, (void *)(&arg[0]));
	if(ret) {
		perror("[receiver] clear packet thread creation failed");
		exit(19);
	}

	// create thread to receive xored packets on the second port
	arg[1].dest = &dest_xored;
	arg[1].file_path = subpaths[1];
	arg[1].temp_folder = argv[2];
	arg[1].slice_path = subpaths[4];
	arg[1].process_information = process_data;
	arg[1].core = 1;
	ret = pthread_create(&threadID[1], NULL, thread_routine, (void *)(&arg[1]));
	if(ret) {
		perror("[receiver] clear packet thread creation failed");
		exit(20);
	}
	
	// create thread to receive checksum packets on the third port
	arg[2].dest = &dest_check;
	arg[2].file_path = subpaths[2];
	arg[2].temp_folder = argv[2];
	arg[2].slice_path = NULL;
	arg[2].process_information = process_checksum;
	arg[2].core = 2;
	ret = pthread_create(&threadID[2], NULL, thread_routine, (void *)(&arg[2]));
	if(ret) {
		perror("[receiver] clear packet thread creation failed");
		exit(21);
	}
	
	// collect threads after they end
	if(pthread_join(threadID[2], NULL)) {
		perror("[receiver] checksum packet thread join failed");
		exit(22);
	}
	
	if(pthread_join(threadID[0], NULL)) {
		perror("[receiver] clear packet thread join failed");
		exit(23);
	}
	
	if(pthread_join(threadID[1], NULL)) {
		perror("[receiver] xored packet thread join failed");
		exit(24);
	}
	
	/* CLEAN UP */
	freeaddrinfo(dest_clear.res);
	freeaddrinfo(dest_xored.res);
	freeaddrinfo(dest_check.res);
	
	if(close(dest_clear.socketfd) == -1) {
		perror("[receiver] close socketfd failed");
		exit(25);
	}
	
	if(close(dest_xored.socketfd) == -1) {
		perror("[receiver] close socketfd failed");
		exit(26);
	}
	
	if(close(dest_check.socketfd) == -1) {
		perror("[receiver] close socketfd failed");
		exit(27);
	}

	// TODO:: set signal handler for ctrl+c to exit gracefully
	
	return 0;
}

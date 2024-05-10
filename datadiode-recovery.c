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

#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* verbose debug information */
//#define DEBUG
/* essential information at runtime */
#define DEBUG2

#include "slice_queue.h"
#include "fountain.h"
#define SEED 777		
uint8_t XOR_GROUP_SIZE = 4; 

#define FILEIDLEN 100
#define TOTALLEN 4
#define PARTLEN 4
#define DATALEN 1364 
#define MAGICNUMBER 42

char paths[5][256];
char path[256];

int open_file(char *path) {
	int fd = open(path, O_RDWR);
	if(fd == -1) {
		fprintf(stderr,"[recovery] Error opening %s\n", path);
		perror("[recovery] ");
		exit(1);
	}
	
	return fd;
}

void close_file(int fd) {
	if(close(fd) == -1) {
		perror("[recovery] close failed");
		exit(2);
	}
}

// reconstruct randomized indices for xor and the inverse array
void prepare_fountain(uint32_t **index, uint32_t **lookup, uint32_t slices) {
	// configure fountain seed
	seed(SEED);
	
	// prepare indices
	*index = (uint32_t *)malloc(slices * sizeof(uint32_t));
	if(*index == NULL) {
		perror("[recovery] prepare_fountain failed to allocate\n");
		exit(3);
	}
	
	*lookup = (uint32_t *)malloc(slices * sizeof(uint32_t));
	if(*lookup == NULL) {
		perror("[recovery] prepare_fountain failed to allocate\n");
		exit(4);
	}
	
	for (uint32_t i=0; i<slices; i++) {
		*(*index + i) = i;
		*(*lookup + i) = i;
	}
	
	// shuffle ordered array of slices and construct inverse array
	indexed_shuffle32(*index, *lookup, slices);

	#ifdef DEBUG
		printf("Fountain shuffle:\n");
		printlineArray32(*index, slices);
		printf("Fountain lookup:\n");
		printlineArray32(*lookup, slices);
	#endif
}

// retrieve checksum from the checksum file
unsigned char *get_checksum(int fd) {

	unsigned char *buf = (unsigned char *)malloc(DATALEN * sizeof(char));
	if(buf == NULL) {
		perror("[recovery] get_checksum failed to allocate\n");
		exit(5);
	}
	
	uint32_t offset = FILEIDLEN + TOTALLEN;
	lseek(fd, offset, SEEK_SET);
	int n = 0;
	if((n = read(fd, buf, DATALEN)) < 1) {
		perror("[recovery] read checksum failed.");
		exit(6);
	}
	
	return buf;
}

// retrieve real file size from checksum file
uint32_t get_filesize(int fd) {
	uint32_t file_size = 0;
	unsigned char ssize[TOTALLEN] = {0};
	
	uint32_t offset = FILEIDLEN;
	lseek(fd, offset, SEEK_SET);
	if(read(fd, ssize, TOTALLEN) < TOTALLEN) {
		perror("[recovery] read file size failed");
		exit(7);
	}
	
	for(uint32_t i=0; i<TOTALLEN; i++){					
		file_size = file_size << 8;
		file_size = file_size | ssize[i];
	}
	#ifdef DEBUG
		printf("File size %d\n", file_size);
	#endif
	
	return file_size;
}

// extract checksum and file size
void extract_checksum_info(char *path, unsigned char **checksum, uint32_t *fsize ){
	int checkfd = open_file(path);
	*checksum = get_checksum(checkfd);
	*fsize = get_filesize(checkfd);
	close_file(checkfd);
}

// keep track of how many times a xored packet was unxored
unsigned char *build_remainder(uint32_t slices) {
	unsigned char *remaining = (unsigned char *)malloc(slices * sizeof(unsigned char));
	if(remaining == NULL) {
		perror("[recovery] malloc failed for remaining slices");
		exit(8);
	}
	for(uint32_t i=0; i<slices; i++) {
		remaining[i] = XOR_GROUP_SIZE;
	}

	return remaining;
}

// given a clear data slice and the in-memory checksum, de-xor clear from checksum
void unxor_from_checksum(unsigned char *toberemoved, unsigned char* buf) {
	#ifdef DEBUG
		printf("Removing data from checksum\n");
	#endif
	
	for(uint32_t i=0; i<DATALEN; i++) {
		buf[i] = buf[i] ^ toberemoved[i];
	}
}

// given a clear data slice, find all xor groups it is part of, then un-xor and update files
void find_and_unxor_from_xor_groups(int xorfd, int slxorfd, unsigned char *remaining, uint32_t slices, uint32_t *lookup, 
	uint32_t clear_index, unsigned char *clear_slice, uint32_t *slice_index) {
	
	// locate clear slice index in randomized array
	uint32_t pos = lookup[clear_index];
				
	// find xored packet indexes in which clear packet was part of
	slice_index[0] = pos;
	for(uint32_t i=1; i<XOR_GROUP_SIZE; i++) {
		slice_index[i] = (pos < i) ? (slices + pos - i) : (pos - i);
	}
	#ifdef DEBUG
		printf("Removing clear packet ID: %d, from grouping %d, %d, %d, %d\n", i, slice_index[0], slice_index[1], slice_index[2], slice_index[3]);
	#endif
				
	// check if xored packet was stored; if yes -> unxor
	unsigned char store = 0;
	uint32_t offset = 0;
	uint32_t position;
	unsigned char buf[DATALEN];
	
	// for each xor group, un-xor the current clear data
	for(int k=0; k<XOR_GROUP_SIZE; k++) {
		offset = slice_index[k]; 
		lseek(slxorfd, offset, SEEK_SET);
		if(read(slxorfd, &store, 1) == 1 && store == MAGICNUMBER) {
			#ifdef DEBUG
				printf("Group xor ID: %d found in xor file\n", slice_index[k]);
			#endif

			position = slice_index[k] * DATALEN;
			lseek(xorfd, position, SEEK_SET);
			if(read(xorfd, buf, DATALEN) != DATALEN) {
				perror("[recovery] read failed for xor remove");
				exit(9);
			}
			
			for(uint32_t i=0; i<DATALEN; i++) {
				buf[i] = buf[i] ^ clear_slice[i];
			}
			
			lseek(xorfd, position, SEEK_SET);
			if(write(xorfd, buf, DATALEN) != DATALEN) {
				perror("[recovery] write failed for xor remove");
				exit(10);
			}
					
			remaining[slice_index[k]]--;
		}
	}
}

// load all fresh clear slices and un-xor them from available xor groups
void unxor_clears_from_xor_file(int clearfd, int xorfd, int slclearfd, int slxorfd, unsigned char *checksum, unsigned char *remaining, 
	uint32_t slices, uint32_t *lookup) {
	unsigned char clear_slice[DATALEN];
	uint32_t slice_index[XOR_GROUP_SIZE];
	unsigned char store = 0;
	uint32_t offset_clear = 0;

	// unxor clear packets from xored packets
	for(uint32_t clear_index=0; clear_index<slices; clear_index++) {
		
		lseek(slclearfd, clear_index, SEEK_SET);
		if(read(slclearfd, &store, 1) == 1 && store == MAGICNUMBER) { // slice present in clear
			// get slice in clear
			offset_clear = clear_index * DATALEN;
			lseek(clearfd, offset_clear, SEEK_SET);
			int n = read(clearfd, clear_slice, DATALEN);
			if(n<DATALEN) {
				fprintf(stderr, "[recovery] read clear less than expected %d\n", n);
				exit(11);
			}

			#ifdef DEBUG
				printf("Clear packet [%d] found\n", i);
			#endif
			
			// remove from total checksum
			unxor_from_checksum(clear_slice, checksum);
			
			// remove from xored slices stored in the xor file
			find_and_unxor_from_xor_groups(xorfd, slxorfd, remaining, slices, lookup, clear_index, clear_slice, slice_index);
		}
		else
		{
			#ifdef DEBUG
				printf("!Missing clear packet [%d]\n", i);
			#endif
		}	
	}
}

// analyze files 
uint8_t log_at_zero_round(int slclearfd, int slxorfd, uint32_t slices, char *filename) {
	int n = 0;
	uint32_t i = 0;
	uint32_t clear_stats=0;
	uint32_t xor_stats=0;
	unsigned char buf[4096];

	lseek(slclearfd, 0, SEEK_SET);
	while((n = read(slclearfd, buf, 4096)) > 0) {
		i = 0;
		while(i<n) {
			if(buf[i] == MAGICNUMBER)
				clear_stats++;	
			i++;
		}		
	}
	
	lseek(slxorfd, 0, SEEK_SET);
	while((n = read(slxorfd, buf, 4096)) > 0) {
		i = 0;
		while(i<n) {
			if(buf[i] == MAGICNUMBER)
				xor_stats++;	
			i++;
		}		
	}

	#ifdef DEBUG2
		printf("**** File name: %s ****\n", filename);
		printf("Clear file present: \t%d/%d \t [%%] = %lf\n", clear_stats, slices, 100.0*clear_stats/slices);
		printf("Xor file present: \t%d/%d \t [%%] = %lf\n", xor_stats, slices, 100.0*xor_stats/slices);
	#endif

	return (clear_stats == slices ? 1 : 0);
}

// print status information after processing all clear data slices 
void log_after_first_round(int slclearfd, int slxorfd, uint32_t slices, unsigned char *remaining) {
	char store = 0;
	printf("Missing clear slices:\n");
	lseek(slclearfd, 0, SEEK_SET);
	for(uint32_t i=0; i<slices; i++) {
		if(read(slclearfd, &store, 1) == 1 && store != MAGICNUMBER) {
			printf("%d ", i);
		}
	}		
	
	printf("\nMissing xor slices:\n");
	lseek(slxorfd, 0, SEEK_SET);
	for(uint32_t i=0; i<slices; i++) {
		if(read(slxorfd, &store, 1) == 1 && store != MAGICNUMBER) {
			printf("%d ", i);
		}
	}	

	printf("\nXor group statistics:\n");
	for(uint32_t i=0; i<slices; i++) {
		printf("[%d] %d ", i, remaining[i]);
	}

}

// perform first layer of recovery: from xor groups with 1 component left, retrieve clear
void recovery_layer1(int clearfd, int xorfd, int slclearfd, int slxorfd, uint32_t slices, unsigned char *checksum, unsigned char *remaining, 
	uint32_t *index, uint32_t *lookup) {
	
	uint32_t components[XOR_GROUP_SIZE];
	unsigned char data_slice[DATALEN];
	uint32_t slice_index[XOR_GROUP_SIZE];
	unsigned char store = 0;

	// build queue with xor groups that now contain clear data
	Qnode_t *qnode = NULL;
	Queue_t *que = createQueue();
	for(uint32_t i=0; i<slices; i++) {
		if(remaining[i] == 1) {
			pushNode(que, i);
		}
	}

	while(peekQueue(que)) {
		qnode = popNode(que);

		// retrieve group components
		components[0] = index[qnode->value];
		for(uint32_t j=0; j<XOR_GROUP_SIZE; j++) {
			components[j] = index[((qnode->value)+j) % slices];
		}

		#ifdef DEBUG
			printf("Found single clear in xor at ID %d\n", i);
			printf("Impacts random indexes: ");
			for(uint32_t j=0; j<XOR_GROUP_SIZE; j++) {
				printf("%d, ", components[j]);
			}
		#endif
				
		for(uint32_t j=0; j<XOR_GROUP_SIZE; j++) {
			lseek(slclearfd, components[j], SEEK_SET);
					
			if(read(slclearfd, &store, 1) == 1 && store != MAGICNUMBER) {
				#ifdef DEBUG
					printf("Missing element found: %d\n", components[j]);
				#endif
						
				lseek(clearfd, components[j] * DATALEN, SEEK_SET);
				lseek(xorfd, (qnode->value) * DATALEN, SEEK_SET);
				lseek(slclearfd, components[j], SEEK_SET);
						
				if(read(xorfd, data_slice, DATALEN) < DATALEN) {
					perror("[recovery] read failed for xor load");
					exit(12);
				}
				if(write(clearfd, data_slice, DATALEN) < DATALEN) {
					perror("[recovery] write failed for clear store");
					exit(13);
				}
				store = MAGICNUMBER;
				if(write(slclearfd, &store, 1) < 1) {
					perror("[recovery] write failed for slice store");
					exit(14);
				}
						
				// remove from total checksum
				unxor_from_checksum(data_slice, checksum);
				
				// remove from xored slices
				find_and_unxor_from_xor_groups(xorfd, slxorfd, remaining, slices, lookup, components[j], data_slice, slice_index);

				// update queue
				for(uint32_t k=0; k<XOR_GROUP_SIZE; k++) {
					if(remaining[slice_index[k]] == 1) {
						pushNode(que, slice_index[k]);
					}
				}
			}
		}

		free(qnode);
	}

	free(que);
}

// will do at some point
int check_the_checksum(char paths[][256]){
	return 1;
}

void clean_tempfiles(char paths[][256], char *newpath) {
	char inotifypath[256];

	// clear-data
	if(rename(paths[0], newpath)) {
		perror("[recovery] failed to rename clear data file");
	}

	check_the_checksum(paths); // will do at some point

	
	if(unlink(paths[1])) {
		perror("[recovery] failed to delete temporary xor-data file");
	}
	
	if(unlink(paths[2])) {
		perror("[recovery] failed to delete temporary checksum file");
	}
	
	if(unlink(paths[3])) {
		perror("[recovery] failed to delete temporary clear slice marker file");
	}
	
	if(unlink(paths[4])) {
		perror("[recovery] failed to delete temporary xor slice marker file");
	}

	snprintf(inotifypath, 255, "%.100s.finished", newpath);
	if(unlink(inotifypath)) {
		perror("[recovery] failed to delete temporary inotify file");
	} else fprintf(stderr, "Deleted |%s|\n", inotifypath);
}

uint8_t recover(char paths[][256]) {
	// open local files
	int clearfd = open_file(paths[0]);
	int xorfd = open_file(paths[1]);
	int slclearfd = open_file(paths[3]);
	int slxorfd = open_file(paths[4]);
	
	// extract checksum and file size
	unsigned char *checksum = NULL;
	uint32_t file_size = 0;
	extract_checksum_info(paths[2], &checksum, &file_size);
	
	// start processing slices
	uint32_t slices = (file_size + (DATALEN-1)) / DATALEN;
	if(slices < XOR_GROUP_SIZE)
		slices = XOR_GROUP_SIZE;
	#ifdef DEBUG
		printf("Slices total = %d\n", slices);
	#endif

	// check if file is complete and print statistics related to % of arrived slices
	if(log_at_zero_round(slclearfd, slxorfd, slices, paths[0])) {
		
		// delete padding characters
		if(ftruncate(clearfd, file_size) == -1) {
			perror("[recovery] truncating failed");
			exit(15);
		}
		
		close_file(clearfd);
		close_file(xorfd);
		close_file(slclearfd);
		close_file(slxorfd);
		free(checksum);

		fprintf(stderr, "[INFO] file was received completely, exiting recovery...\n");
		clean_tempfiles(paths, path);

		exit(0);
	//	return 1;
	}
	
	// prepare indices for fountain codes
	uint32_t *index = NULL;
	uint32_t *lookup = NULL;
	prepare_fountain(&index, &lookup, slices);
		
	// keep track of how many times a xored packet was unxored
	unsigned char *remaining = build_remainder(slices);
	
	// unxor clear packets from xored packets
	unxor_clears_from_xor_file(clearfd, xorfd, slclearfd, slxorfd, checksum, remaining, slices, lookup);
	
	#ifdef DEBUG
		log_after_first_round(slclearfd, slxorfd, slices, remaining);
	#endif
	
	// try to recover clear slices from single xored slices
	recovery_layer1(clearfd, xorfd, slclearfd, slxorfd, slices, checksum, remaining, index, lookup);
	
	uint8_t don = log_at_zero_round(slclearfd, slxorfd, slices, paths[0]);

	// delete padding characters
	if(ftruncate(clearfd, file_size) == -1) {
		perror("[recovery] truncating failed");
		exit(16);
	}
	
	// clean up
	close_file(clearfd);
	close_file(xorfd);
	close_file(slclearfd);
	close_file(slxorfd);

	free(checksum);
	free(remaining);
	free(index);
	free(lookup);
	
	return don;
}

int main(int argc, char *argv[]) {
	
	// process data from outside
	if(argc != 4) {
		fprintf(stderr, "[usage] <program> <input-folder> <file-basename> <xor-size>\n");
		exit(17);
	}
	
	// recovery data file names
	char subpaths[5][256];
	strcpy(subpaths[0], "_clear_data.in");
	strcpy(subpaths[1], "_xor_data.in");
	strcpy(subpaths[2], "_checksum.in");
	strcpy(subpaths[3], "_clear_list.in");
	strcpy(subpaths[4], "_xor_list.in");
	
	// build path to files
	strcpy(path, argv[1]);
	strcat(path, "/");
	strcat(path, argv[2]);
	
	strcpy(paths[0], path);
	strcat(paths[0], subpaths[0]);
	strcpy(paths[1], path);
	strcat(paths[1], subpaths[1]);
	strcpy(paths[2], path);
	strcat(paths[2], subpaths[2]);
	strcpy(paths[3], path);
	strcat(paths[3], subpaths[3]);
	strcpy(paths[4], path);
	strcat(paths[4], subpaths[4]);
	
	XOR_GROUP_SIZE = atoi(argv[3]);

	uint8_t retval = recover(paths);

	if(retval)
		clean_tempfiles(paths, path);

	return 0;
}

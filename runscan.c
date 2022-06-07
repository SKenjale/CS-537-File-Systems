#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include "ext2_fs.h"
#include "read_ext2.h"

int main(int argc, char **argv) {
	if (argc != 3) {
		printf("expected usage: ./runscan inputfile outputfile\n");
		exit(0);
	}

	DIR *dir = opendir(argv[2]);
	if (dir != NULL) {
		printf("error: directory '%s' already exists\n", argv[2]);
		exit(0);
	}
	
	if (mkdir(argv[2], S_IRWXU | S_IRWXG | S_IRWXO) == -1) {
        printf("error: could not create directory '%s'\n", argv[2]);
    }

	int fd;

	fd = open(argv[1], O_RDONLY);    /* open disk image */

    ext2_read_init(fd);

	struct ext2_super_block super;
	struct ext2_group_desc group;

    for (uint groupNum = 0; groupNum < num_groups; groupNum++) {
	
	// must read first the super-block and group-descriptor
	read_super_block(fd, groupNum, &super);
	read_group_desc(fd, groupNum, &group);
	
	off_t start_inode_table = locate_inode_table(groupNum, &group);

    char dirBuffer[1024];
    char buffer[1024];
    struct ext2_inode *dirInode = malloc(sizeof(struct ext2_inode)); // Stores inode for current directory
    struct ext2_inode *inode = malloc (sizeof(struct ext2_inode)); // Stores inode for file in current directory
    struct ext2_dir_entry_2 *dirEntry = malloc(sizeof(struct ext2_dir_entry_2)); // Stores data about current directory
	uint groupOffset;
	uint groupSize = block_size * blocks_per_group;

    // Iterate over blocks in inode table
    for (uint blockNum = 0; blockNum < itable_blocks; blockNum++) {
		groupOffset = groupNum * groupSize;
        // Iterate over inodes in block
	    for (uint i = 0; i < inodes_per_block; i++) {
            uint inodeNum = i + (blockNum * inodes_per_block);
            read_inode(fd, groupNum, start_inode_table, inodeNum, dirInode);

            if (S_ISDIR(dirInode->i_mode)) {
                lseek(fd, groupOffset + BLOCK_OFFSET(dirInode->i_block[0]), SEEK_SET);
                read(fd, dirBuffer, 1024);

                int offset = 0;
                char *name = malloc(256 * sizeof(char));

                 while(offset < 1024){

                    memcpy(&dirEntry->inode, dirBuffer + offset, 4);
                    memcpy(&dirEntry->rec_len, dirBuffer + offset + 4, 2);
                    memcpy(&dirEntry->name_len, dirBuffer + offset + 6, 1);
                    memcpy(name, dirBuffer + offset + 8, dirEntry->name_len);

					//printf(" INODE NUMBER - %u\n", dirEntry->inode);

                //    if(dirEntry->inode == 1005){
                //     printf(" OFFSET1 - %u\n", offset);
                //     printf(" REC_LEN - %u\n", dirEntry->rec_len);
                //     printf(" NAME_LEN - %u\n", dirEntry->name_len);
                //     printf("%d\n", a);
                //      for(uint i = 0; i < 1024; i++){
                //           printf("%d",dirBuffer[i]);
                //       }
                //    }

                    if (!dirEntry->rec_len)
                        break;
                    
                    if (dirEntry->name_len % 4 == 0) {
                        offset += 8 + dirEntry->name_len;
                    } else {
                        offset += 8 + dirEntry->name_len + 4 - (dirEntry->name_len % 4);
                    }

                    read_inode(fd, groupNum, start_inode_table, dirEntry->inode, inode);

                    if (S_ISREG(inode->i_mode)) {
                        lseek(fd, groupOffset + BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
                        read(fd, buffer, 1024);

                        int is_jpg = 0;
                        if (buffer[0] == (char)0xff && buffer[1] == (char)0xd8 && buffer[2] == (char)0xff &&
                           (buffer[3] == (char)0xe0 || buffer[3] == (char)0xe1 || buffer[3] == (char)0xe8)) {
                            is_jpg = 1;
                        }

                        if (is_jpg) {

                            uint bytesToWrite = inode->i_size;
                            mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
                            char filepath1[512];
                            snprintf(filepath1, sizeof(filepath1), "./%s/file-%u.jpg", argv[2], dirEntry->inode);
                            int file1 = open(filepath1, O_CREAT | O_WRONLY | O_RDONLY | O_TRUNC, mode);

                            char filepath2[512];
                            snprintf(filepath2, sizeof(filepath2), "./%s/%s", argv[2], name);
                            int file2 = open(filepath2, O_CREAT | O_WRONLY | O_RDONLY | O_TRUNC, mode);
                
                            if (bytesToWrite <= 1024) { // If data is only contained in data block 0
                                write(file1, buffer, bytesToWrite);
                                write(file2, buffer, bytesToWrite);
                            } 
                            else { // Data spans multiple data blocks

                                write(file1, buffer, 1024);
                                write(file2, buffer, 1024);
                                bytesToWrite -= 1024;

                                for (int j = 1; j < 12; j++) {
                                    lseek(fd, groupOffset + BLOCK_OFFSET(inode->i_block[j]), SEEK_SET);
                                    read(fd, buffer, 1024);
                        
                                    if (bytesToWrite <= 1024 && bytesToWrite > 0) { // If this data block is the last block with data
                                        write(file1, buffer, bytesToWrite);
                                        write(file2, buffer, bytesToWrite);
                                        bytesToWrite = 0;
                                        break;
                                    }
                                    else { // There are more data blocks
                                        write(file1, buffer, 1024);
                                        write(file2, buffer, 1024);
                                        bytesToWrite -= 1024;
                                    }
                                }
                    
                                uint ipbuffer[256]; // Indirect Pointer Buffer
                    
                                if (bytesToWrite != 0) { // Data is stored in indirect pointer
                                    lseek(fd, groupOffset + BLOCK_OFFSET(inode->i_block[12]), SEEK_SET);
                                    read(fd, ipbuffer, 1024);
                        
                                    for (int j = 0; j < 256; j++) {
                                        lseek(fd, groupOffset + BLOCK_OFFSET(ipbuffer[j]), SEEK_SET);
                                        read(fd, buffer, 1024);
                            
                                        if (bytesToWrite <= 1024 && bytesToWrite > 0) { // If this data block is the last block with data
                                            write(file1, buffer, bytesToWrite);
                                            write(file2, buffer, bytesToWrite);
                                            bytesToWrite = 0;
                                            break;
                                        }
                                        else { // There are more data blocks
                                            write(file1, buffer, 1024);
                                            write(file2, buffer, 1024);
                                            bytesToWrite -= 1024;
                                        }
                                    }
                                }
                    
                                uint dipbuffer[1024]; // Double Indirect Pointer Buffer
                    
                                if (bytesToWrite != 0) { // Data is stored in double indirect pointer
                                    lseek(fd,groupOffset +  BLOCK_OFFSET(inode->i_block[13]), SEEK_SET);
                                    read(fd, dipbuffer, 1024);
                        
                                    for (int j = 0; j < 256; j++) {
                                        lseek(fd,groupOffset +  BLOCK_OFFSET(dipbuffer[j]), SEEK_SET);
                                        read(fd, ipbuffer, 1024);
                            
                                        for (int k = 0; k < 256; k++) {
                                            lseek(fd, groupOffset + BLOCK_OFFSET(ipbuffer[k]), SEEK_SET);
                                            read(fd, buffer, 1024);
                            
                                            if (bytesToWrite <= 1024) { // If this data block is the last block with data
                                                write(file1, buffer, bytesToWrite);
                                                write(file2, buffer, bytesToWrite);
                                                bytesToWrite = 0;
                                                break;
                                            }
                                            else { // There are more data blocks
                                                write(file1, buffer, 1024);
                                                write(file2, buffer, 1024);
                                                bytesToWrite -= 1024;
                                            }
                                            
                                        }
                                    }
                                }
                            }
                            close(file1);
                            close(file2);
                        }
                    }
                    memset(name, 0, 256);
                }
                free(name);
            }
        }            
    }
    free(dirInode);
    free(inode);
    free(dirEntry);
    close(fd);
    }
}

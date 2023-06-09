// The MIT License (MIT)
//
// Copyright (c) 2016 Trevor Bakker
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include "../include/headers.h"

// Input handling
#define WHITESPACE " \t\n"
#define MAX_COMMAND_SIZE 255
#define MAX_NUM_ARGUMENTS 12

// FS related
#define BLOCK_SIZE 1024
#define NUM_BLOCKS 65536
#define BLOCKS_PER_FILE 1024
#define NUM_FILES 256
#define FIRST_DATA_BLOCK 1364
#define MAX_FILE_SIZE 1048576

// File attributes
#define HIDDEN_ATTR 0
#define READONLY_ATTR 1

uint8_t *free_blocks;
uint8_t *free_inodes;
uint8_t data[NUM_BLOCKS][BLOCK_SIZE];

struct directoryEntry {
  char filename[64];
  short in_use;
  int32_t inode;
};

struct inode {
  int32_t blocks[BLOCKS_PER_FILE];
  short in_use;
  uint8_t attribute;
  uint32_t file_size;
};

struct directoryEntry *directory;
struct inode          *inodes;

FILE    *fp;
char    image_name[64];
uint8_t image_open;

//----------FS functions----------
void printInodeInfo(uint32_t inode_num)
{
  struct inode thisInode = inodes[inode_num];
  if(!thisInode.in_use)
  {
    printf("Inode %d not in use\n",inode_num);
    return;
  }

  printf("Inode %d blocks: \n",inode_num);
  uint32_t i;
  for(i=0; thisInode.blocks[i] != -1; i++)
    printf("%d ",thisInode.blocks[i]);
  
  printf("\n");
  return;
}

//Returns file index in directory, or -1 if not found
int32_t fileExists(char* filename)
{
  int32_t i;
  for(i = 0; i<NUM_FILES; i++)
    if(directory[i].in_use && strcmp(directory[i].filename, filename) == 0)
      return i;
    
  return -1;
}

int32_t findDeletedFile(char* filename)
{
  int32_t i;
  for(i = 0; i<NUM_FILES; i++)
    if(!(directory[i].in_use) && strcmp(directory[i].filename, filename) == 0)
      return i;
    
  return -1;
}

int32_t findFreeBlock()
{
  int32_t i;
  for(i = 0; i<NUM_BLOCKS; i++)
    if(free_blocks[i]) return i;
  
  return -1;
}

int32_t findFreeInode()
{
  int32_t i;
  for(i = 0; i < NUM_FILES; i++)
    if(free_inodes[i]) return i;
  
  return -1;
}

int32_t findFreeInodeBlock(int32_t inode)
{
  int32_t i;
  for(i = 0; i < BLOCKS_PER_FILE; i++)
    if(inodes[inode].blocks[i] == -1) return i;
  
  return -1;
}

int main()
{
  char *command_string = (char *)malloc(MAX_COMMAND_SIZE);

  fp = NULL;

  init();

  while(1)
  {
//----------Input string handling----------
    //Print out the prompt
    printf("FS> ");

    //Wait for command to read
    while(!fgets(command_string, MAX_COMMAND_SIZE, stdin));

    //Parse input
    char *token[MAX_NUM_ARGUMENTS];

    uint32_t i, token_count = 0;
    for(i = 0; i < MAX_NUM_ARGUMENTS; i++)
      token[i] = NULL;
    
    char *argument_ptr = NULL;
    char *working_string = strdup(command_string);
    char *head_ptr = working_string;

    //Tokenize the input strings with whitespace as delimiter
    while(((argument_ptr = strsep(&working_string, WHITESPACE)) != NULL) &&
           (token_count < MAX_NUM_ARGUMENTS))
    {
      token[token_count] = strndup(argument_ptr, MAX_COMMAND_SIZE);
      if(strlen(token[token_count]) == 0) token[token_count] = NULL;
      token_count++;
    }

//----------Command handling----------

    if(token[0] == NULL)
      continue;

    if(strcmp(token[0], "quit") == 0)
      exit(0);

    if(strcmp("createfs", token[0]) == 0)
    {
      if (token[1] == NULL)
      {
        printf("Error: No filename specified\n");
        continue;
      }
      createfs(token[1]);
    }
    else if(strcmp("savefs", token[0]) == 0)
    {
      savefs();
    }
    else if( strcmp("open", token[0]) == 0)
    {
      if(token[1] == NULL)
      {
        printf("Error: No filename specified\n");
        continue;
      }
      openfs(token[1]);
    }
    else if(strcmp("close", token[0]) == 0)
    {
      closefs();
    }
    else if(strcmp("list", token[0]) == 0)
    {
      if(!image_open)
      {
        printf("Error: No image open\n");
        continue;
      }
      char *param1, *param2;
      if(token[1] == NULL) param1 = "\0";
      else param1 = token[1];

      if(token[2] == NULL) param2 = "\0";
      else param2 = token[2];
      
      list(param1, param2);
    }
    else if(strcmp("df", token[0]) == 0)
    {
      if(!image_open)
      {
        printf("Error: No image open\n");
        continue;
      }
      printf("%d bytes free\n",df());
    }
    else if(strcmp("insert", token[0]) == 0)
    {
      if(token[1] == NULL)
      {
        printf("Error: No filename specified\n");
        continue;
      }
      insert(token[1]);
    }
    else if(strcmp("read", token[0]) == 0)
    {
      uint32_t start, num;

      if(token[1] == NULL)
      {
        printf("Error: No filename specified\n");
        continue;
      }
      if(!image_open)
      {
        printf("Error: No image open\n");
        continue;
      }

      //If starting byte not provided, default to zero
      if(token[2] == NULL) start = 0;
      else start = atoi(token[2]);

      //If num bytes to read not provided, default to file size
      if(token[3] == NULL)
      {
        int32_t ret = fileExists(token[1]);
        if(ret == -1)
        {
          printf("Error: File %s doesn't exist\n",token[1]);
          continue;
        }
        num = inodes[directory[ret].inode].file_size;
      }
      else num = atoi(token[3]);

      if(start < 0 || num < 0)
      {
        printf("Error: Invalid read range\n");
        continue;
      }
      readData(token[1],start, num);
    }
    else if(strcmp("attrib", token[0]) == 0)
    {
      int32_t fileIndex;
      uint32_t i, j;
      for(i = 1; i < MAX_NUM_ARGUMENTS; i++)
      {
        if(token[i] == NULL)
        {
          printf("Error: Incorrect parameters. Ex: attrib [+attribute] <filename>\n");
          break;
        }
        else
        {
          if(token[i][0] == '+' || token[i][0] == '-')
          {
            continue;
          }
          else if((fileIndex = fileExists(token[i])) >=0)
          {
            for(j = i; j > 0; --j)
              set_attribute(fileIndex, token[j]);
            
            break;
          }
          else
          {
            if(fileIndex < 0) printf("Error: File %s doesn't exist\n", token[i]);
            else printf("Error: Incorrect param format\nEx: attrib [+attribute] [-attribute] <filename>\n");
            break;
          }
        }
      }
    }
    else if(strcmp("retrieve", token[0]) == 0)
    {
      if(token[1] == NULL)
      {
        printf("Error: No file specified to retrieve\n");
        continue;
      }
      retrieve(token[1], token[2]);
    }
    else if(strcmp("encrypt", token[0]) == 0 || strcmp("decrypt", token[0]) == 0)
    {
      if(token[1] == NULL)
      {
        printf("Error: No file specified to encrypt\nEx: encrypt <filename> <cipher>\n");
        continue;
      }
      if(token[2] == NULL)
      {
        printf("Error: No cipher specified\nEx: encrypt <filename> <cipher>\n");
        continue;
      }
      encryptFile(token[1], (uint8_t) token[2][0]);
    }
    else if(strcmp("delete", token[0]) == 0)
    {
      if(token[1] == NULL)
      {
        printf("Error: No file specified to delete\n");
        continue;
      }
      deleteFile(token[1]);
    }
    else if(strcmp("undelete", token[0]) == 0)
    {
      if(token[1] == NULL)
      {
        printf("Error: No file specified to undelete\n");
        continue;
      }
      undeleteFile(token[1]);
    }
    else printf("Error: Unsupported command %s\n",token[0]);
    
    
    //Cleanup allocated memory
    for(i = 0; i < MAX_NUM_ARGUMENTS; i++)
      if(token[i] != NULL) free(token[i]);

    free(head_ptr);
  }

  free(command_string);
  return 0;
}

//----------Command functions----------

void deleteFile(char *filename)
{
  int32_t ret = fileExists(filename);
  if(ret == -1)
  {
    printf("Error: File %s doesn't exist\n", filename);
    return;
  }
  
  struct directoryEntry thisDir = directory[ret];
  
  //Set directory entry to not in use
  thisDir.in_use = 0;

  struct inode thisInode = inodes[thisDir.inode];

  //Set it's inode to not in use
  thisInode.in_use = 0;

  //Set the inode as free
  free_inodes[thisDir.inode] = 1;

  //Increment block count to account for partially filled end block
  uint32_t block_count = thisInode.file_size / BLOCK_SIZE;
  if(thisInode.file_size % BLOCK_SIZE) block_count++;

  //Set the inode blocks as free
  uint32_t i;
  for(i = 0; i < block_count; i++)
    free_blocks[ thisInode.blocks[i] ] = 1;
  
  //Save changes
  inodes[thisDir.inode] = thisInode;
  directory[ret] = thisDir;
}

//If a file's inode, or any of it's blocks are not free undelete fails.
void undeleteFile(char *filename)
{
  int32_t ret = findDeletedFile(filename);
  if(ret == -1)
  {
    printf("Error: No such deleted file %s to recover\n", filename);
    return;
  }

  struct directoryEntry thisDir = directory[ret];
  struct inode thisInode = inodes[thisDir.inode];

  if(thisInode.in_use || !(free_inodes[thisDir.inode]))
  {
    printf("Error: File %s inode overwritten, cannot undelete\n", filename);
    return;
  }

  uint32_t block_count = thisInode.file_size / BLOCK_SIZE;
  if(thisInode.file_size % BLOCK_SIZE) block_count++;

  uint32_t i;
  for(i = 0; i < block_count; i++)
  {
    if(!(free_blocks[ thisInode.blocks[i] ]))
    {
      printf("Error: File %s block overwritten, cannot undelete\n", filename);
      return;
    }
  }

  for(i = 0; i < block_count; i++)
    free_blocks[ thisInode.blocks[i] ] = 0;
  
  thisInode.in_use = 1;
  thisDir.in_use = 1;

  inodes[thisDir.inode] = thisInode;
  directory[ret] = thisDir;
}

//Use provided 1 byte cipher to encrypt specified file
void encryptFile(char *filename, uint8_t cipher)
{
  int32_t ret = fileExists(filename);
  if(ret == -1)
  {
    printf("Error: File %s doesn't exist\n", filename);
    return;
  }

  struct inode thisInode = inodes[directory[ret].inode];

  uint32_t i, j, block_count, remainder, fileSize = thisInode.file_size;
  
  block_count = fileSize / BLOCK_SIZE;

  if(block_count < 1)
  {
    remainder = fileSize % BLOCK_SIZE;
    for(i = 0; i < remainder; i++)
      data[ FIRST_DATA_BLOCK + thisInode.blocks[0] ][ i ] ^= cipher;
  }
  else
  {
    for(i = 0; i < block_count; i++)
      for(j = 0; j < BLOCK_SIZE; j++)
        data[ FIRST_DATA_BLOCK + thisInode.blocks[i] ][j] ^= cipher;
    
    //In case incomplete final block
    remainder = fileSize % BLOCK_SIZE;
    for(j = 0; j < remainder; j++)
      data[ FIRST_DATA_BLOCK + thisInode.blocks[i] ][j] ^= cipher;
  }
}

//Output hex of specified file, with optional starting byte, and optional number of bytes to read
//Defaults to read entire file if range not specified.
void readData(char *file, uint32_t startByte, uint32_t numBytes)
{
  uint32_t i, j, fileSize;
  struct inode thisInode;
  int32_t foundDir = fileExists(file);

  if(foundDir == -1)
  {
    printf("Error: Read Failed. File %s not found\n", file);
    return;
  }

  thisInode = inodes[directory[foundDir].inode];
  fileSize = thisInode.file_size;

  if(startByte > fileSize)
  {
    printf("Error: Impossible starting byte\n");
    return;
  }
  if((startByte + numBytes) > fileSize)
  {
    numBytes = fileSize - startByte;
    printf("Requested read of too many bytes, reading %d instead\n", numBytes);
  }

  uint32_t startingBlock = startByte / BLOCK_SIZE;
  
  //Real starting byte relative to the first block
  startByte = startByte % BLOCK_SIZE;

  //Bytes left to read from first block
  uint32_t remainder = BLOCK_SIZE - startByte;
  
  //Cover edge case where numBytes to read is less than what's left of the first block
  if(remainder > numBytes) remainder = numBytes;

  uint32_t blockToRead = FIRST_DATA_BLOCK + thisInode.blocks[startingBlock];

//---Read First block---
  for(i = 0; i < remainder; i++)
    printf("%0x ", data[blockToRead][i]);
  
  printf("\n");

  numBytes -= remainder;

  uint32_t currBlock, numBlocksToRead = numBytes / BLOCK_SIZE;

//---Read middle block(s)---
  for(i = startingBlock+1; i <= numBlocksToRead; i++)
  {
    currBlock = thisInode.blocks[i];
    for(j = 0; j < BLOCK_SIZE; j++)
      printf("%0x ", data[currBlock + FIRST_DATA_BLOCK][j]);
    
    printf("\n");
  }

  numBytes -= (numBlocksToRead * BLOCK_SIZE);
  
  if(numBytes == 0) return;
  
  currBlock = thisInode.blocks[i];

//---Read last block---
  for(j=0; j<numBytes; j++)
    printf("%0x ", data[currBlock + FIRST_DATA_BLOCK][j]);
  
  printf("\n");
}

//Only called when file confirmed to exist, and attr in +/- prefix format
void set_attribute(uint32_t file_number, char* attr)
{
  struct directoryEntry thisFile = directory[file_number];
  struct inode thisInode = inodes[thisFile.inode];
  uint8_t setBit=0;
  if(attr[0] == '+') setBit = 1;

  //Utilize bitmask, with OR to set bit, or NOT on AND of the byte to unset the bit.
  if(attr[1] == 'h')
  {
    if(setBit) thisInode.attribute |= (1 << HIDDEN_ATTR);
    else thisInode.attribute &= ~(1 << HIDDEN_ATTR);
  }
  else if(attr[1] == 'r')
  {
    if(setBit) thisInode.attribute |= (1 << READONLY_ATTR);
    else thisInode.attribute &= ~(1 << READONLY_ATTR);
  }

  //Save inode changes
  inodes[thisFile.inode] = thisInode;
}

//List files within the opened FS image
//-a to include current file attribute state
//-h to include hidden files among listed files
void list(char *param1,char *param2)
{
  int32_t i, j, not_found = 1;
  uint32_t hidden = 0, print_attr = 0;

  if(param1[1] == 'h' || param2[1] == 'h') hidden = 1;
  if(param1[1] == 'a' || param2[1] == 'a') print_attr = 1;

  printf("Contents of image: %s\n",image_name);

  for(i = 0; i < NUM_FILES; i++)
  {
    if( directory[i].in_use)
    {
      if(!hidden && (inodes[directory[i].inode].attribute) & (1 << HIDDEN_ATTR))
        continue;
      
      not_found=0;
      char filename[64];
      
      memset(filename, 0, 64);
      strncpy(filename, directory[i].filename, strlen(directory[i].filename));

      if(print_attr)
      {
        printf("%s - Attr: ",filename);
        for(j=7; j>=0; j--)
        {
          if((inodes[directory[i].inode].attribute) & (1 << j)) printf("1");
          else printf("0");
        }
        printf("\n");
      }
      else printf("%s\n",filename);
    }
  }

  if(not_found) printf("Error: No files found\n");
}

//Copy specified file from image to the current directory
//May optionally specify a new filename for the created file
void retrieve(char *fileToRetrieve, char *newFilename)
{
  int32_t file_num = fileExists(fileToRetrieve);

  if(file_num == -1)
  {
    printf("Error: Filename %s not found\n",fileToRetrieve);
    return;
  }

  //If no new filename specified, use the current name
  if(newFilename == NULL) newFilename = fileToRetrieve;

  fp = fopen(newFilename,"w");

  struct directoryEntry thisFile = directory[file_num];
  struct inode thisInode = inodes[thisFile.inode];

  uint32_t fileSize, block_count, i, j;
  
  fileSize = thisInode.file_size;
  block_count = fileSize / BLOCK_SIZE;

  //Filesize will be zero, within one block, or over multiple blocks
  if(block_count < 1)
  {
    fwrite(&data[FIRST_DATA_BLOCK + thisInode.blocks[0]][0], 1, fileSize, fp);
  }
  else
  {
    for(i=0; i<block_count; i++)
      fwrite(&data[ FIRST_DATA_BLOCK + thisInode.blocks[i]][0], BLOCK_SIZE, 1, fp);
    
    //In case last block is not full
    uint32_t remainder = fileSize % BLOCK_SIZE;
    if(remainder) fwrite(&data[ FIRST_DATA_BLOCK + thisInode.blocks[block_count]][0], 1, remainder, fp);
  }

  fclose(fp);
}

//Used to initialize newly created FS image
void init()
{
  directory = (struct directoryEntry *)&data[0][0];
  free_inodes = (uint8_t *)&data[18][0];
  free_blocks = (uint8_t *)&data[19][0];
  inodes = (struct inode *)&data[84][0];

  memset(image_name, 0, 64);

  image_open = 0;

  uint32_t i,j;
  for(i = 0; i < NUM_FILES; i++)
  {
    directory[i].in_use = 0;
    directory[i].inode = -1;
    free_inodes[i] = 1;

    memset(directory[i].filename, 0, 64);

    for(j = 0; j < NUM_BLOCKS; j++)
    {
      inodes[i].blocks[j] = -1;
      inodes[i].in_use = 0;
      inodes[i].attribute = 0;
      inodes[i].file_size = 0;
    }
  }

  for(j = 0; j < NUM_BLOCKS; j++)
    free_blocks[j] = 1;
}

//Print total data free in the open image
uint32_t df()
{
  uint32_t j, count=0;
  for(j = 0; j < NUM_BLOCKS; j++)
    if(free_blocks[j]) count++;
  
  return count * BLOCK_SIZE;
}

//Create new FS image with specified file name
void createfs(char *filename)
{
  fp = fopen(filename, "w");

  strncpy(image_name, filename, strlen(filename));

  memset(data, 0, NUM_BLOCKS * BLOCK_SIZE);

  image_open = 1;

  uint32_t i, j;

  for(i = 0; i < NUM_FILES; i++)
  {
    directory[i].in_use = 0;
    directory[i].inode = -1;
    free_inodes[i] = 1;

    memset( directory[i].filename, 0, 64);

    for(j = 0; j < NUM_BLOCKS; j++)
    {
      inodes[i].blocks[j] = -1;
      inodes[i].in_use = 0;
      inodes[i].attribute = 0;
      inodes[i].file_size = 0;
    }
  }

  for(j = 0; j < NUM_BLOCKS; j++ ) 
    free_blocks[j] = 1;
  
  fclose(fp);
}

//Save changes to the open image
void savefs()
{
  if(image_open == 0)
  {
    printf("Error: No image open to be saved\n");
    return;
  }

  fp = fopen(image_name, "w");

  fwrite(&data[0][0], BLOCK_SIZE, NUM_BLOCKS, fp);

  printf("Saved image: %s\n",image_name);
  
  fclose(fp);
}

//To open or change the current open FS image
//Does not verify that the specified file is a valid FS image
void openfs(char *filename)
{
  fp = fopen(filename, "r");

  memset(image_name, 0, 64);
  strncpy(image_name, filename, strlen(filename));
  
  fread(&data[0][0], BLOCK_SIZE, NUM_BLOCKS, fp);

  image_open = 1;

  fclose(fp);
}

//Close the opened image if there's one
//Does not save changes if any
void closefs()
{
  if(image_open == 0) 
  {
    printf("Error: Disk image not open\n");
    return;
  }

  image_open = 0;
  memset(image_name, 0, 64);
}

//Insert file into the open FS image
void insert(char *filename)
{
  if(!image_open)
  {
    printf("Error: No image open\n");
    return;
  }

  //verify file exists
  struct stat buf;
  int32_t ret = stat(filename, &buf);

  if(ret == -1)
  {
    printf("Error: File %s doesn't exist\n", filename);
    return;
  }

  //verify file isnt too big
  if(buf.st_size > MAX_FILE_SIZE)
  {
    printf("Error: File exceeds max filesize\n");
    return;
  }

  //verify there's enough space
  if(buf.st_size > df())
  {
    printf("Error: Not enough free space\n");
    return;
  }

  //find empty directory entry
  uint32_t i;
  int32_t directory_entry = -1;
  for(i = 0; i < NUM_FILES; i++)
  {
    if(directory[i].in_use == 0)
    {
      directory_entry = i;
      break;
    }
  }

  if(directory_entry == -1)
  {
    printf("Error: No empty directory entries available\n");
    return;
  }

  //find free inodes and place the file
  //-------Code from block_copy----------
  // Open the input file read-only 
  FILE *ifp = fopen (filename, "r");

  // Save off the size of the input file since we'll use it in a couple of places and 
  // also initialize our index variables to zero. 
  int32_t copy_size   = buf.st_size;

  // We want to copy and write in chunks of BLOCK_SIZE. So to do this 
  // we are going to use fseek to move along our file stream in chunks of BLOCK_SIZE.
  // We will copy bytes, increment our file pointer by BLOCK_SIZE and repeat.
  int32_t offset      = 0;

  // We are going to copy and store our file in BLOCK_SIZE chunks instead of one big 
  // memory pool. Why? We are simulating the way the file system stores file data in
  // blocks of space on the disk. block_index will keep us pointing to the area of
  // the area that we will read from or write to.
  int32_t block_index = -1;

  //find a free inode
  int32_t inode_index = findFreeInode();
  
  if(inode_index == -1)
  {
    printf("Error: No free inode\n");
    return;
  }

  //Set the inode to in use and not free
  free_inodes[inode_index] = 0;
  inodes[inode_index].in_use = 1;

  //place file info in the directory
  directory[directory_entry].in_use = 1;
  directory[directory_entry].inode = inode_index;
  strncpy(directory[directory_entry].filename, filename, strlen(filename));
  //strcpy(directory[directory_entry].filename , filename);

  inodes[inode_index].file_size = copy_size;

  // copy_size is initialized to the size of the input file so each loop iteration we
  // will copy BLOCK_SIZE bytes from the file then reduce our copy_size counter by
  // BLOCK_SIZE number of bytes. When copy_size is less than or equal to zero we know
  // we have copied all the data from the input file.
  while(copy_size > 0)
  {
    fseek(ifp, offset, SEEK_SET);

    // Read BLOCK_SIZE number of bytes from the input file and store them in our
    // data array.

    //find a free block
    block_index = findFreeBlock();

    if(block_index == -1)
    {
      printf("Error: No free blocks\n");
      return;
    }

    int32_t bytes = fread(data[block_index + FIRST_DATA_BLOCK], BLOCK_SIZE, 1, ifp);

    //save the block number in the inode block[]
    int32_t inode_block_index = findFreeInodeBlock(inode_index);
    inodes[inode_index].blocks[inode_block_index] = block_index;

    free_blocks[block_index] = 0; 

    // If bytes == 0 and we haven't reached the end of the file then something is 
    // wrong. If 0 is returned and we also have the EOF flag set then that is OK.
    // It means we've reached the end of our input file.
    if(bytes == 0 && !feof(ifp))
    {
      printf("An error occured reading from the input file.\n");
      return;
    }

    // Clear the EOF file flag.
    clearerr(ifp);

    // Reduce copy_size by the BLOCK_SIZE bytes.
    copy_size -= BLOCK_SIZE;
    
    // Increase the offset into our input file by BLOCK_SIZE.  This will allow
    // the fseek at the top of the loop to position us to the correct spot.
    offset    += BLOCK_SIZE;

    // Increment the index into the block array 
    // DO NOT just increment block index in your file 
    
    block_index = findFreeBlock();
  }
  // We are done copying from the input file so close it out.
  fclose(ifp);
}
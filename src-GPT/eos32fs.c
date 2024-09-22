/*
 * eos32fs.c -- EOS32 file system driver (just a dummy for now)
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define FUSE_USE_VERSION	31
#include <fuse.h>

#include "gpt.h"


#define SECTOR_SIZE	512
#define BLOCK_SIZE	4096
#define SPB		(BLOCK_SIZE / SECTOR_SIZE)


/**************************************************************/


void error(char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  printf("Error: ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
  exit(1);
}


void warning(char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  printf("Warning: ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
}


/**************************************************************/


int main(int argc, char *argv[]) {
  char *diskName;
  FILE *disk;
  unsigned int diskSize;
  unsigned int fsStart;
  unsigned int fsSize;
  int partNumber;
  char *endptr;
  char *partType;
  unsigned int numBlocks;

  if (argc < 4) {
    printf("Usage:\n"
           "    %s <disk> <part> <mnt> [<opts>]\n"
           "        <disk>  disk image file\n"
           "        <part>  partition number for EOS32 file system\n"
           "                0   search for first one with matching type\n"
           "                '*' treat whole disk as a single file system\n"
           "        <mnt>   mount point (directory) for EOS32 file system\n"
           "        <opts>  other mount options (for FUSE)\n",
           argv[0]);
    exit(1);
  }
  diskName = argv[1];
  disk = fopen(diskName, "r+b");
  if (disk == NULL) {
    error("cannot open disk image '%s'", diskName);
  }
  fseek(disk, 0, SEEK_END);
  diskSize = ftell(disk) / SECTOR_SIZE;
  /* set fsStart and fsSize */
  if (strcmp(argv[2], "*") == 0) {
    /* whole disk contains one single file system */
    fsStart = 0;
    fsSize = diskSize;
  } else {
    /* argv[2] is partition number of file system */
    partNumber = strtoul(argv[2], &endptr, 0);
    if (*endptr != '\0') {
      error("cannot read partition number '%s'", argv[2]);
    }
    partType = EOS32_FS;
    gptGetPartInfo(disk, diskSize,
                   partNumber, partType,
                   &fsStart, &fsSize);
  }
  printf("File system start is at sector %u (0x%X).\n",
         fsStart, fsStart);
  printf("File system space is %u (0x%X) sectors of %d bytes each.\n",
         fsSize, fsSize, SECTOR_SIZE);
  if (fsSize % SPB != 0) {
    warning("file system space is not a multiple of block size");
  }
  numBlocks = fsSize / SPB;
  printf("This space equals %u (0x%X) blocks of %d bytes each.\n",
         numBlocks, numBlocks, BLOCK_SIZE);
  if (numBlocks < 2) {
    error("file system has less than 2 blocks");
  }
  printf("------------------------------------\n");
  printf("This is just a dummy for now, sorry.\n");
  printf("------------------------------------\n");
  return 0;
}

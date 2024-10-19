/*
 * eos32fs.c -- EOS32 file system driver (just a dummy for now)
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define FUSE_USE_VERSION	31
#include <fuse3/fuse.h>

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


void usage(char *myself) {
  printf("Usage:\n"
         "    %s <disk> <part> <mnt> [<opts>]\n"
         "        <disk>  disk image file\n"
         "        <part>  partition number for EOS32 file system\n"
         "                '*' treat whole disk as a single file system\n"
         "        <mnt>   mount point (directory) for EOS32 file system\n"
         "        <opts>  other mount options (for FUSE)\n",
         myself);
  exit(1);
}


int main(int argc, char *argv[]) {
  char *diskName;
  FILE *disk;
  unsigned int diskSize;
  int partNumber;
  char *endptr;
  GptEntry entry;
  unsigned int fsStart;
  unsigned int fsSize;
  unsigned int numBlocks;

  if (argc < 4) {
    usage(argv[0]);
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
    gptRead(disk, diskSize);
    gptGetEntry(partNumber, &entry);
    if (strcmp(entry.type, GPT_NULL_UUID) == 0) {
      error("partition %d is not used", partNumber);
    }
    if (strcmp(entry.type, "2736CFB2-27C3-40C6-AC7A-40A7BE06476D") != 0 &&
        strcmp(entry.type, "36F2469F-834E-466E-9D2C-6D6F9664B1CB") != 0) {
      error("partition %d is not an EOS32 file system", partNumber);
    }
    fsStart = entry.start;
    fsSize = entry.end - entry.start + 1;
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

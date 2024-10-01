/*
 * eos32fs.c -- EOS32 file system driver (just a dummy for now)
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FUSE_USE_VERSION	31
#include <fuse.h>


int main(int argc, char *argv[]) {
  printf("mount and unmount functions are not yet supported, sorry\n");
  return 0;
}

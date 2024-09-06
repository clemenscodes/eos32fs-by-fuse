#
# Makefile for 'passthrough' file system example
#

INCS=-I/usr/include/fuse3
LIBS=-lfuse3 -lpthread

all:		passthrough

passthrough:	passthrough.c helpers.h
		gcc -Wall -o passthrough $(INCS) passthrough.c $(LIBS)

mount:		passthrough
		mkdir -p mnt
		./passthrough mnt

umount:
		fusermount3 -u mnt

clean:
		rm -rf mnt
		rm -f *~ passthrough

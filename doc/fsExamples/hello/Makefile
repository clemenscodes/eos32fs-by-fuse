#
# Makefile for 'hello' file system example
#

INCS=-I/usr/include/fuse3
LIBS=-lfuse3 -lpthread

all:		hello

hello:		hello.c
		gcc -Wall -o hello $(INCS) hello.c $(LIBS)

mount:		hello
		mkdir -p mnt
		./hello mnt

umount:
		fusermount3 -u mnt

clean:
		rm -rf mnt
		rm -f *~ hello

#
# Makefile for EOS32 filesystem driver
#

BUILD = ../build

CC = gcc
CFLAGS = -g -Wall -D_FILE_OFFSET_BITS=64 -I/usr/include/fuse3
LDFLAGS = -g
LDLIBS = -luuid -lfuse3

SRCS = eos32fs.c gpt.c
OBJS = $(patsubst %.c,%.o,$(SRCS))
BIN = eos32fs

.PHONY:		all install clean

all:		$(BIN)

install:	$(BIN)
		mkdir -p $(BUILD)/bin
		cp $(BIN) $(BUILD)/bin

$(BIN):		$(OBJS)
		$(CC) $(LDFLAGS) -o $(BIN) $(OBJS) $(LDLIBS)

%.o:		%.c
		$(CC) $(CFLAGS) -o $@ -c $<

depend.mak:
		$(CC) -MM -MG $(CFLAGS) $(SRCS) >depend.mak

-include depend.mak

clean:
		rm -f *~ depend.mak
		rm -f $(OBJS) $(BIN)

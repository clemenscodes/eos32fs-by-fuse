#
# Makefile for eos32fs-by-fuse project
#

DIRS = doc disk src

.PHONY:		all $(DIRS) clean

all:		$(DIRS)

$(DIRS):
		$(MAKE) -C $@ install

clean:
		for i in $(DIRS) ; do $(MAKE) -C $$i clean ; done
		rm -rf build
		rm -f *~

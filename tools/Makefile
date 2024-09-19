#
# Makefile for eos32fs tools
#

DIRS = cmpx

.PHONY:		all $(DIRS) clean

all:		$(DIRS)

$(DIRS):
		$(MAKE) -C $@ all

clean:
		for i in $(DIRS) ; do $(MAKE) -C $$i clean ; done
		rm -f *~

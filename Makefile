SUBDIRS := ./kernfs ./libfs ./libfs/tests
.PHONY: all 

all: 
	make -C ./kernfs
	make -C ./kernfs/tests
	make -C ./libfs
	make -C ./libfs/tests


#
# mruby/c  sample_c/Makefile
#
# Copyright (C) 2015-2020 Kyushu Institute of Technology.
# Copyright (C) 2015-2020 Shimane IT Open-Innovation Center.
#
#  This file is distributed under BSD 3-Clause License.
#

TARGETS = sample_scheduler sample_no_scheduler sample_include sample_concurrent sample_myclass
CFLAGS += -g -I ../src -Wall -Wpointer-arith
LDFLAGS +=
LIBMRUBYC = ../src/libmrubyc.a

all: $(TARGETS)

sample_scheduler: sample_scheduler.c $(LIBMRUBYC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(@:=.c) $(LIBMRUBYC)

sample_no_scheduler: sample_no_scheduler.c $(LIBMRUBYC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(@:=.c) $(LIBMRUBYC)

sample_include: sample_include.c $(LIBMRUBYC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(@:=.c) $(LIBMRUBYC)

sample_concurrent: sample_concurrent.c $(LIBMRUBYC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(@:=.c) $(LIBMRUBYC)

sample_myclass: sample_myclass.c $(LIBMRUBYC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(@:=.c) $(LIBMRUBYC)

clean:
	@rm -rf $(TARGETS) *.o *.dSYM *~

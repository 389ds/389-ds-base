#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# Makefile for netsite.h

MCOM_ROOT = ../../../..
MODULE=netsiteIncludeBase

include ../../../nsdefs.mk

HDRDEST=$(OBJDIR)/include/base

PREFIX=../../copyrght.h


NOSTDSTRIP=true
NOSTDDEPEND=true

#HDRS=$(wildcard *.h)
HDRS=daemon.h cinfo.h crit.h ereport.h buffer.h net.h pblock.h sem.h session.h shexp.h shmem.h systhr.h util.h file.h pool.h regexp.h systems.h


BINS=$(addprefix $(HDRDEST)/,$(HDRS))

all: $(HDRDEST) $(BINS)

$(HDRDEST):
	mkdir -p $(HDRDEST)

strip:
depend:

include ../../../nsconfig.mk

$(HDRDEST)/%.h: %.h
	cat $(PREFIX) $< > $(HDRDEST)/$*.h

#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# Makefile for netsite.h

BUILD_ROOT = ../..
MODULE=netsiteIncludeBase

include $(BUILD_ROOT)/nsdefs.mk

HDRDEST=$(OBJDIR)/include/base

PREFIX=../copyrght.h


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

include $(BUILD_ROOT)/nsconfig.mk

$(HDRDEST)/%.h: %.h
	cat $(PREFIX) $< > $(HDRDEST)/$*.h

#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# Makefile for include/public/nsacl public header files

BUILD_ROOT = ../../..
MODULE=netsiteIncludeNsacl

include $(BUILD_ROOT)/nsdefs.mk

HDRDEST=$(OBJDIR)/include/nsacl

PREFIX=copyrght.h


NOSTDSTRIP=true
NOSTDDEPEND=true

HDRS=$(wildcard *.h)

BINS=$(addprefix $(HDRDEST)/,$(HDRS))

all: $(HDRDEST) $(BINS)

$(HDRDEST):
	mkdir -p $(HDRDEST)

strip:
depend:

include $(BUILD_ROOT)/nsconfig.mk

$(HDRDEST)/%.h: %.h
	cat $(PREFIX) $< > $(HDRDEST)/$*.h

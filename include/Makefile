#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# Makefile for netsite.h

BUILD_ROOT = ..
MODULE=netsiteInclude

include $(BUILD_ROOT)/nsdefs.mk

HDRDEST=$(OBJDIR)/include

NSPRDEST=$(HDRDEST)
NSPRHDRS= \
	prio.h \
	prlong.h \
	prtypes.h \
	prtime.h \
	prthread.h \
	prinrval.h \
	md/prcpucfg.h \
	obsolete/protypes.h

NSPRBINS=$(addprefix $(NSPRDEST)/, $(NSPRHDRS))

PREFIX=copyrght.h


NOSTDSTRIP=true
NOSTDDEPEND=true

HDRS=netsite.h version.h

BINS=$(addprefix $(HDRDEST)/,$(HDRS))

all: stuff nspr

strip:
depend:

include $(BUILD_ROOT)/nsconfig.mk

ifeq ($(NSAPI_CAPABLE), true)

stuff: $(HDRDEST) $(BINS) sub-hdrs

$(HDRDEST):
	mkdir -p $(HDRDEST)

ifeq ($(PRODUCT), "Netscape Proxy Server")
sub-hdrs:
	cd base; gmake
	cd frame; gmake
	cd libproxy; gmake
else
sub-hdrs:
	cd base; gmake
	cd frame; gmake
endif

$(HDRDEST)/%.h: %.h
	cat $(PREFIX) $< > $(HDRDEST)/$*.h

else
stuff:

endif

$(NSPRDEST): 
	mkdir -p $(NSPRDEST)

$(NSPRDEST)/md: $(NSPRDEST)
	mkdir -p $(NSPRDEST)/md

$(NSPRDEST)/obsolete: $(NSPRDEST)
	mkdir -p $(NSPRDEST)/obsolete

$(NSPRDEST)/%.h: 
	cp $(NSCP_DISTDIR)/include/nspr20/pr/$*.h $(NSPRDEST)/$*.h

nspr: $(NSPRDEST)/md $(NSPRDEST)/obsolete $(NSPRBINS)

#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# The base library contains the base set of functionality for ldapserver
# servers. Cross-platform I/O and other generic functions are
# provided here.


MCOM_ROOT=../../..
MODULE=LibBase

include ../../nsdefs.mk

OBJDEST=$(OBJDIR)/lib/base

ifeq ($(ARCH), WINNT)
LIBS=$(OBJDIR)/lib/libbase.lib
ifeq ($(BSCINFO), yes)
BSCS=$(OBJDIR)/lib/libbase.bsc
endif
else
LIBS=$(OBJDIR)/lib/libbase.a
endif

include ../../nsconfig.mk

MCC_INCLUDE += $(ADMINUTIL_INCLUDE)

LOCAL_DEPS = $(NSPR_DEP) $(ADMINUTIL_DEP) $(SECURITY_DEP) $(DBM_DEP)

all: $(LOCAL_DEPS) $(OBJDEST) $(LIBS) $(BSCS)

$(OBJDEST):
	mkdir -p $(OBJDEST)

ifeq ($(ARCH), WINNT)
OSOBJS = nterrors.o eventlog.o eventhandler.o ntpipe.o pathnames.o
else
OSOBJS = 
endif

OBJS = $(addprefix $(OBJDEST)/,	\
				dstats.o \
				daemon.o \
				shexp.o \
				regexp.o \
				pblock.o \
				plist.o \
				buffer.o \
				netbuf.o \
				file.o \
				net.o \
				session.o \
				cinfo.o \
				util.o \
				ereport.o \
				sem.o \
				fsmutex.o \
				systhr.o \
				crit.o \
				rwlock.o \
				dns.o \
				dnsdmain.o \
				shmem.o \
				dll.o \
				servssl.o \
				lexer.o \
		                restart.o \
				objndx.o \
				cache.o \
				pool.o \
				time_cache.o \
				dns_cache.o \
				system.o \
				xp.o \
				nscperror.o \
				language.o \
				fasttime.o \
				secpwd.o \
				$(OSOBJS) \
				) 

MODULE_CFLAGS=-I$(NSROOT)/include/base

ifeq ($(PRODUCT),"Netscape Catalog Server")
ifeq ($(ARCH), WINNT)
MCC_INCLUDE := $(subst -I$(MCOM_ROOT)/lib/libnet,,$(MCC_INCLUDE))
endif
endif

#$(OBJDEST)/fasttime.o : $(OBJDIR)/fasttime.o
#	cp $< $@

$(LIBS): $(OBJS)
	rm -f $@
	$(AR) $(OBJS)
	$(RANLIB) $@

ifeq ($(ARCH), WINNT)
ifeq ($(BSCINFO), yes)
$(BSCS): $(OBJS)
	$(BSCMAKE) $(OBJDEST)/*.sbr
endif
endif

include $(INCLUDE_DEPENDS)


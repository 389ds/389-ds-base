#
# BEGIN COPYRIGHT BLOCK
# This Program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; version 2 of the License.
# 
# This Program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA 02111-1307 USA.
# 
# In addition, as a special exception, Red Hat, Inc. gives You the additional
# right to link the code of this Program with code not covered under the GNU
# General Public License ("Non-GPL Code") and to distribute linked combinations
# including the two, subject to the limitations in this paragraph. Non-GPL Code
# permitted under this exception must only link to the code of this Program
# through those well defined interfaces identified in the file named EXCEPTION
# found in the source code files (the "Approved Interfaces"). The files of
# Non-GPL Code may instantiate templates or use macros or inline functions from
# the Approved Interfaces without causing the resulting work to be covered by
# the GNU General Public License. Only Red Hat, Inc. may make changes or
# additions to the list of Approved Interfaces. You must obey the GNU General
# Public License in all respects for all of the Program code and other code used
# in conjunction with the Program except the Non-GPL Code covered by this
# exception. If you modify this file, you may extend this exception to your
# version of the file, but you are not obligated to do so. If you do not wish to
# provide this exception without modification, you must delete this exception
# statement from your version and license this file solely under the GPL without
# exception. 
# 
# 
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

ifneq ($(ARCH), WINNT)

SRCDIR=$(BUILD_ROOT)/httpd/src

AR = ar

ifeq ($(ARCH), OSF1)
DLL_LDFLAGS += -soname $(SONAME)
EXTRA_LIBS += -Wl,-rpath,.:../../lib:../../bin/https:../../plugins/java/bin:../../wai/lib
ADM_EXTRA = -lcxx -lpthread -lmach -lexc -lc
AR = rm -f ________64ELEL_ ; ar
endif

ifeq ($(ARCH), IRIX)
DLL_LDFLAGS += -soname $(SONAME)
ifeq ($(USE_N32), 1)
 DLL_LDFLAGS += -n32 -mips3
endif
EXTRA_LIBS += -rpath .:../../lib:../../bin/https:../../plugins/java/bin:../wai/lib
# IRIX likes it twice!
SOLINK2=$(SOLINK)
endif

ifeq ($(ARCH), SOLARIS)
ifdef NS_USE_NATIVE
DLL_LDFLAGS += -h $(SONAME)
EXTRA_LIBS += -R .:../../lib:../../bin/https:../../plugins/java/bin:../wai/lib
else
EXTRA_LIBS += -Wl,-R,.:../../lib:../../bin/https:../../plugins/java/bin:../wai/lib
endif
ADM_EXTRA = $(GCCLIB)
endif

ifdef USE_LD_RUN_PATH
EXTRA_LIBS += -L.
export LD_RUN_PATH=./.:../../lib:../../bin/https:../../plugins/java/bin:../wai/lib
endif

ifeq ($(ARCH), SONY)
DLL_LDFLAGS += -soname $(SONAME)
EXTRA_LIBS += -rpath .
endif

ifeq ($(ARCH), NEC)
DLL_LDFLAGS += -h $(SONAME)
endif

ifeq ($(ARCH), HPUX)
DLL_LDFLAGS += -L. 
SOLINK=-L. -l$(HTTPDSO_NAME)$(DLL_PRESUF)
EXTRA_LIBS += -Wl,+b.:../../lib:../../bin/https:../../plugins/java/bin:../wai/lib
# The line below is required for LiveWire DB2 to work.
EXTRA_LIBS += -Wl,-uallow_unaligned_data_access -lhppa
LD=$(CCC) 
# Well HPUX's not happy about including libnspr.sl(-lnspr) into our executable,
# it's that ___ +eh again.  
NSPRLINK=$(NOTHING)
#LDAPLINK=-L. $(addsuffix .a, $(addprefix lib, $(LDAP_DOTALIB_NAMES))) \
#	 $(addprefix -l, $(LDAP_SOLIB_NAMES))
endif

ifeq ($(ARCH), AIX)
MKSHLIB_FLAGS += -berok -brtl
SOLINK=-L. -L../../lib -lns-dshttpd$(DLL_PRESUF)
#LDAPLINK=-L. $(addsuffix .a, $(addprefix lib, $(LDAP_DOTALIB_NAMES))) \
#	 $(addprefix -l, $(LDAP_SOLIB_NAMES))
#NSPRLINK = -L. -lnspr$(DLL_PRESUF)
#NSPRLINK = -L. -ldsnspr$(DLL_PRESUF)
ADM_EXTRA := -L. -L../../lib $(LDAPLINK) $(NSPRLINK) $(EXTRA_LIBS) 
DEF_LIBPATH := .:../../lib:$(DEF_LIBPATH)
endif

ifeq ($(ARCH), SUNOS4)
EXTRA_LIBS += -L.
ADM_EXTRA = $(EXTRA_LIBS)
endif

ifeq ($(ARCH), UnixWare)
DLL_LDFLAGS += -h $(SONAME)
NSPRLINK = -L. -ldsnspr$(DLL_PRESUF)
endif

EXTRA_LIBS += $(MATHLIB)

ifndef SONAME
SONAME=$(HTTPDSO_NAME)$(DLL_PRESUF).$(DLL_SUFFIX)
endif

ifndef SOLINK
SOLINK=./$(HTTPDSO_NAME)$(DLL_PRESUF).$(DLL_SUFFIX)
endif

#ifndef LDAPLINK
#LDAPLINK=$(LDAPOBJNAME)
#endif

ifndef NSPRLINK
NSPRLINK=libnspr$(DLL_PRESUF).$(DLL_SUFFIX)
endif

# Temporary directory for the libraries and their object files
$(OBJDIR)/httpd-lib:
ifeq ($(ARCH), HPUX)
	mkdir -p $(OBJDIR)/httpd-lib/nspr20
endif
	mkdir -p $(OBJDIR)/httpd-lib/sslio
	mkdir -p $(OBJDIR)/httpd-lib/arlib
	mkdir -p $(OBJDIR)/httpd-lib/libsec
	mkdir -p $(OBJDIR)/httpd-lib/libdbm
	mkdir -p $(OBJDIR)/httpd-lib/xp

# Define a LIBSEC which doesn't include libdbm and xp.
LIBSECNAME=$(MCOM_LIBDIR)/libsec/$(NSOBJDIR_NAME)/libsec-$(WHICHA).$(LIB_SUFFIX)
ifndef LIBSECOBJS
LIBSEC1=$(LIBSECNAME)
else
LIBSEC1=$(LIBSECOBJS)
endif

DEPLIBS = $(OBJDIR)/lib/libsi18n.a $(ADMLIB) $(LDAPSDK_DEP)

DEPLINK = $(OBJDIR)/lib/libsi18n.a
DEPLINK +=	$(OBJDIR)/lib/libmsgdisp.a
DEPLINK +=	$(SOLINK) $(LDAPLINK) $(NSPRLINK) $(SOLINK2)

# Relative to the directory that contains the .so
BUILTDIR = .

ifndef NO_VERITY

ifeq ($(DO_SEARCH), yes)
ifdef VERITY_TASKSTUB
TASKSTUB = ./taskstub.o
else
TASKSTUB =
endif

ifndef VERITY_SOLINK
VERITY_SOLINK=$(TASKSTUB) -L. $(addprefix -l, $(VERITY_LIBNAMES))
endif

VERITYDEP=$(addprefix $(OBJDIR)/, $(VERITYOBJNAMES) $(TASKSTUB))

$(VERITYDEP) : $(LIBVERITY)
	cp $(LIBVERITY) $(VERITY_TASKSTUB) $(OBJDIR)

DEPLINK += $(VERITY_SOLINK)
DEPLIBS += $(VERITYDEP)
endif

endif

SERVLIBS = $(addprefix $(OBJDIR)/lib/, libadmin.a libaccess.a \
		libldapu.a libbase.a libsi18n.a)
SERVLIB_DIRS = $(addprefix $(OBJDIR)/lib/, libadmin libaccess base \
                           ldaputil libmsgdisp libsi18n)
SERVLIB_OBJS = $(subst $(OBJDIR)/,$(BUILTDIR)/, \
                               $(wildcard $(addsuffix /*.o, $(SERVLIB_DIRS))))

# Removed for ns-security integration
#NSLIBS = $(SECLIB) $(LIBSSLIO)

ADMLIB_LIBS = $(SERVLIBS) $(NSLIBS)


admobjs:
ifeq ($(ARCH), HPUX)
	cd $(OBJDIR)/httpd-lib/nspr20; $(AR) x $(LIBNSPR)
endif
	cd $(OBJDIR)/httpd-lib/sslio; $(AR) x $(LIBSSLIO)
	cd $(OBJDIR)/httpd-lib/libdbm; $(AR) x $(LIBDBM)
	cd $(OBJDIR)/httpd-lib/xp; $(AR) x $(LIBXP)
	cd $(OBJDIR)/httpd-lib/libdbm; $(AR) x $(LIBDBM)
	cd $(OBJDIR)/httpd-lib/xp; $(AR) x $(LIBXP)
	rm -f $(addprefix $(OBJDIR)/httpd-lib/xp/, xp_time.o xplocale.o \
                                                   xp_cntxt.o)

#$(LDAPOBJNAME):	ldapobjs

#ldapobjs:
#	(cd $(OBJDIR); rm -f $(LDAPOBJNAME))
#	cp $(LIBLDAP) $(OBJDIR)

# Removed for ns-security integration.
#OBJRULES += ldapobjs

# Removed the httpd-lib from link for ns-security integration.
#ADMOBJS=$(SERVLIB_OBJS) $(BUILTDIR)/httpd-lib/*/*.o
#OBJRULES += admobjs
ADMLIB=$(OBJDIR)/$(SONAME)

$(ADMLIB): $(ADMLIB_LIBS)
ifeq ($(ARCH), IRIX)
    ifeq ($(USE_N32), 1)	# no -objectlist any more
	cd $(OBJDIR) ; \
	$(LINK_DLL) \
             $(SERVLIB_OBJS) $(ADM_EXTRA)
    else
	echo "$(SERVLIB_OBJS)" > /tmp/objectlist
	tr ' ' '\012' < /tmp/objectlist > /tmp/objectlist.NEW
	mv /tmp/objectlist.NEW /tmp/objectlist
	cd $(OBJDIR) ; \
	$(LINK_DLL) \
	     -objectlist /tmp/objectlist $(ADM_EXTRA)
	rm /tmp/objectlist
    endif
else
	cd $(OBJDIR) ; \
	$(LINK_DLL) \
             $(SERVLIB_OBJS) $(ADM_EXTRA)
endif


endif

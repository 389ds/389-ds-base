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
# do so, delete this exception statement from your version. 
# 
# 
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# The libsi18n library support for getting strings from the
# string database.

BUILD_ROOT=../..
MODULE=LibsI18N
include $(BUILD_ROOT)/nsdefs.mk

NSDEFS_PRODUCT = $(NS_PRODUCT)

OBJDEST=$(OBJDIR)/lib/libsi18n

L10NDIR = $(BUILD_ROOT)/l10n

ifeq ($(ARCH), WINNT)
CCOUT0=  /Fo
CCOUT1=  /Fe
LIBS=$(OBJDIR)/lib/libsi18n.lib
ifeq ($(BSCINFO), yes)
BSCS=$(OBJDIR)/lib/libsi18n.bsc
endif
else
LIBS=$(OBJDIR)/lib/libsi18n.a
CCOUT0=  -o 
CCOUT1=  -o 
endif

ifeq ($(ARCH), IRIX)
 ifeq ($(USE_N32), 1)
  MYLDFLAG = -n32
 else
  MYLDFLAG =
 endif
else
 MYLDFLAG =
endif

ifeq ($(USE_64), 1)
ifeq ($(ARCH), HPUX)
  MYLDFLAG = +DA2.0W
endif
ifeq ($(ARCH), SOLARIS)
  MYLDFLAG = -xarch=v9 
endif
endif

ifeq ($(BUILD_MODULE), DIRECTORY)
 StringDatabase = ns-slapd.properties
 DBTheaders = \
    ../../include/base/dbtbase.h \
    ../../include/libaccess/dbtlibaccess.h \
    ../../include/libadmin/dbtlibadmin.h \
    ../../ldap/clients/dsgw/dbtdsgw.h \
    gsslapd.h
 endif

include $(BUILD_ROOT)/nsconfig.mk

MCC_INCLUDE += $(ADMINUTIL_INCLUDE)

all: $(OBJDEST) $(LIBS) $(BSCS) $(OBJDEST)/$(StringDatabase)

$(OBJDEST)/$(StringDatabase): makstrdb.c ../../include/i18n.h $(DBTheaders)
	@echo "Building String Database for $(BUILD_MODULE) in"
	@echo "$(OBJDEST)/$(StringDatabase)"
	$(CC) -g $(CFLAGS) $(MCC_INCLUDE) -c makstrdb.c \
		$(CCOUT0)$(OBJDEST)/makstrdb.o
	$(CC) $(MYLDFLAG) -g $(OBJDEST)/makstrdb.o \
		$(CCOUT1)$(OBJDEST)/makstrdb.exe
	cd $(OBJDEST); ./makstrdb.exe
	rm $(OBJDEST)/makstrdb.exe

$(OBJDEST):
	mkdir -p $(OBJDEST)

OBJS = 	\
	$(OBJDEST)/acclanglist.o \
	$(OBJDEST)/getstrprop.o \
	$(OBJDEST)/getlang.o \
	$(OBJDEST)/coreres.o \
	$(OBJDEST)/txtfile.o \
	$(OBJDEST)/propset.o \
	$(OBJDEST)/reshash.o



$(LIBS): $(OBJS)
	rm -f $@
	$(AR) $(OBJS)
	$(RANLIB) $@

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
# Makefile for Directory Server 40 installation plug-in 
#
#
# XXXstevross: note change the makeclean to work
#
#

BUILD_ROOT=../../..
LDAP_SRC= $(BUILD_ROOT)/ldap

NOSTDCLEAN=1

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

OUTDIR=$(OBJDIR)/setup

CFLAGS=/nologo /MD /W3 /Gm /GX /Zi /Od $(SETUPSDK_INCLUDE) $(LDAPSDK_INCLUDE) \
 /I "$(LDAP_SRC)/admin/include" /I "$(LDAP_SRC)/admin/lib" \
 /I "$(LDAP_SRC)/admin/src" /D \
 "WIN32" /D "_DEBUG" /D "_WINDOWS" /Fp"$(OUTDIR)/dsinst.pch" /YX \
 /Fo"$@" /Fd"$(OUTDIR)/" /c

RCFLAGS=/l 0x409 /d "_DEBUG"

LFLAGS= /nologo /subsystem:windows /dll /incremental:no\
	/pdb:none /debug /machine:I386 

DSLFLAGS= /out:"$(OUTDIR)/dsinst.dll" /implib:"$(OUTDIR)/dsinst.lib" 

LIBS= \
	kernel32.lib user32.lib gdi32.lib comdlg32.lib\
	comctl32.lib advapi32.lib shell32.lib uuid.lib\
	wsock32.lib\
	$(SETUPSDKLINK) $(LDAP_SDK_LIBLDAP_DLL) $(NSPRLINK)

DSOBJS= \
	$(OUTDIR)/dsinst_dsalib_dn.obj \
	$(OUTDIR)/dsinst.obj \
	$(OUTDIR)/dsinst.res

CONSOLOBJS= \
	$(OUTDIR)/consolinst.obj

LIBINSTOBJS = \
	$(OUTDIR)/libinst.obj

CFLAGS+= /I "$(BUILD_ROOT)/include/nt" /I "$(BUILD_ROOT)/ldap/include" /I "$(BUILD_ROOT)/include" $(ADMINUTIL_INCLUDE) $(NSPR_INCLUDE) /D "NS_DS" /D "XP_WIN32"

CC=cl.exe
LD=link.exe
RC=rc.exe
RM=erase /F /Q
FIXINF= ../newinst/fixINF.pl

all: $(SETUPSDK_DEP) $(LDAPSDK_DEP) $(NSPR_DEP) $(ADMINUTIL_DEP) $(OUTDIR)/dsinst.dll $(OUTDIR)/slapd.inf

clean:
	-rm -rf "$(OUTDIR)"

$(OUTDIR):
	@mkdir "$(OUTDIR)"

# dependencies only, no commands
$(OUTDIR)/dsinst.obj: dsinst.c dsinst.h 

$(OUTDIR)/dsinst_dsalib_dn.obj: dsinst_dsalib_dn.c

$(OUTDIR)/consolinst.obj: consolinst.c consolinst.h 

$(OUTDIR)/libinst.obj: libinst.c libinst.h 

# now the commands
$(OUTDIR)/dsinst.dll: $(OUTDIR) $(DSOBJS) $(CONSOLOBJS) $(LIBINSTOBJS) 
	$(LD) $(LFLAGS) $(DSLFLAGS) $(DSOBJS) $(CONSOLOBJS) $(LIBINSTOBJS) $(LIBS) 

$(OUTDIR)/%.obj: %.c
	$(CC) $(CFLAGS) $<

$(OUTDIR)/%.inf: %.inf
	$(PERL) $(FIXINF) $(BUILD_MODULE) $(DIR_VERSION) $(BUILD_ROOT)/$(BUILD_ARCH)/buildnum.dat $< $(SECURITY) "DoesntOnNT" $(IS_DIR_LITE) '$(INSTANCE_NAME_PREFIX)' $@ $(BUILD_BOMB)

$(OUTDIR)/%.res: %.rc
	$(RC) $(RCFLAGS) /fo"$@" $<

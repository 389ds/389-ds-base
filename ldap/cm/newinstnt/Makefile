#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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
	$(SETUPSDKLINK) $(LDAP_SDK_LIBLDAP_DLL)

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

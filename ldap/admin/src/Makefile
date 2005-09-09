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
# Source for the admin forms and CGI programs

LDAP_SRC = ../..
BUILD_ROOT = ../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

BINDIR=$(LDAP_ADMIN_BIN_RELDIR)
OBJDEST=$(LDAP_ADMOBJDIR)

SCRIPTSDIR=$(LDAP_BASE_RELDIR)/admin/scripts

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(USE_ADMINSERVER), 1)
MCC_INCLUDE += $(ADMINUTIL_INCLUDE)
endif

INCLUDES += -I$(LDAP_SRC)/admin/include

DS_SERVER_DEFS = -DNS_DS

ifneq ($(ARCH), WINNT)
EXTRALDFLAGS += $(SSLLIBFLAG)
endif

ifeq ($(BUILD_DLL), yes)
DYNAMIC_DEPLIBS=$(LDAP_ADMLIB) $(LDAP_COMMON_LIBS_DEP)
ifeq ($(USE_ADMINSERVER), 1)
  DYNAMIC_DEPLIBS += $(ADMINUTIL)
endif
DYNAMIC_DEPLINK=$(DYNAMIC_DEPLIBS)
else
DYNAMIC_DEPLIBS=$(LDAP_COMMON_LIBS_DEP)
DYNAMIC_DEPLINK=$(LDAP_ADMLIB) $(LDAP_COMMON_LIBS)
endif

EXTRA_LIBS_DEP += $(NSPR_DEP) $(LDAPSDK_DEP)
ifeq ($(USE_ADMINSERVER), 1)
  EXTRA_LIBS_DEP += $(ADMINUTIL_DEP)
endif
EXTRA_LIBS_DEP += $(ICU_DEP)
# we don't want to build with warnings-as-errors for the admin/ stuff, because
# it's got crappy C++ code which is LITTERED with warnings, most of which we
# can't fix because it comes from files in dist/, etc.
ifeq ($(ARCH), Linux)
CFLAGS := $(subst -Werror,,$(CFLAGS))
endif

OLD_EXTRA_LIBS := $(EXTRA_LIBS)
EXTRA_LIBS = $(DYNAMIC_DEPLINK) $(LDAP_NOSSL_LINK)
ifeq ($(USE_ADMINSERVER), 1)
  EXTRA_LIBS += $(ADMINUTIL_LINK)
endif
EXTRA_LIBS += $(SECURITYLINK) $(NSPRLINK)
ifeq ($(USE_SETUPUTIL), 1)
  EXTRA_LIBS += $(SETUPUTIL_S_LINK)
endif
EXTRA_LIBS += $(ICULINK) $(OLD_EXTRA_LIBS)

# these are the libraries to use when building the installer for the open source version
OPENSOURCE_LIBS = $(LDAP_ADMLIB) $(LDAP_NOSSL_LINK) $(SECURITYLINK) $(NSPRLINK)

ifeq ($(ARCH), WINNT)
PLATFORM_INCLUDE = -I$(BUILD_ROOT)/include/nt
SUBSYSTEM=console
EXTRA_LIBS+=comctl32.lib $(LDAP_LIBUTIL)
OPENSOURCE_LIBS += comctl32.lib $(LDAP_LIBUTIL)
EXTRA_LIBS_DEP+=$(LDAP_LIBUTIL_DEP)

ifeq ($(DEBUG), optimize)
#NT_NOLIBS = /NODEFAULTLIB:LIBCMT /NODEFAULTLIB:MSVCRT
else
NT_NOLIBS = /NODEFAULTLIB:LIBCMTD /NODEFAULTLIB:MSVCRTD
endif

else
ifeq ($(ARCH), IRIX)
EXCEPTIONS=-exceptions
else
ifeq ($(ARCH),SOLARIS)
ifeq ($(USE_64), 1)
EXTRALDFLAGS += -xarch=v9
endif
EXTRA_LIBS += -lsocket -lnsl -lgen -lm -lposix4 -lthread
OPENSOURCE_LIBS += -lsocket -lnsl -lgen -lm -lposix4 -lthread
else
ifeq ($(ARCH),SOLARISx86)
EXTRA_LIBS += -lsocket -lnsl -lgen -lm -lposix4 -lthread
OPENSOURCE_LIBS += -lsocket -lnsl -lgen -lm -lposix4 -lthread
else
ifeq ($(ARCH),HPUX)
ifdef FORTEZZA 
# link with libci.a for FORTEZZA builds. On other platforms, libci.a is
# linked into libds_admin.so, but not on HPUX
EXTRA_LIBS_DEP += $(FORTEZZA_DRIVER)
EXTRA_LIBS += $(FORTEZZA_DRIVER)
endif
ifeq ($(USE_64), 1)
EXTRALDFLAGS += +DA2.0W +DS2.0 +Z
endif
else
ifeq ($(ARCH),OSF1)
#CC += -E
#CXX += -Wl,-ymain
else
ifeq ($(ARCH),ReliantUNIX)
else
ifeq ($(ARCH), AIX)
EXTRA_LIBS += -lcurses
else
ifeq ($(ARCH), UnixWare)
# rgc:
EXTRA_LIBS += -lsocket -lnsl -lgen -lm
else 
ifeq ($(ARCH), Linux)
EXTRA_LIBS += -lcrypt
else
#this will help with porting other platforms
EXTRA_LIBS+="you need to define EXTRA_LIBS for $(ARCH) in ldap/admin/src/Makefile"
endif # Linux
endif # UnixWare
endif # AIX
endif # ReliantUNIX
endif # OSF1
endif # HPUX
endif # SOLARISx86
endif # SOLARIS
endif # IRIX
endif # WINNT

ifneq ($(ARCH), WINNT)
TEMP_EXTRALDFLAGS:=$(EXTRALDFLAGS)
EXTRALDFLAGS = -lpthread $(TEMP_EXTRALDFLAGS)
endif

ifeq ($(SECURITY),domestic)
SECUS_BINS=
MODULE_CFLAGS += -DUS_VERSION -DEXPORT_VERSION
endif

ifeq ($(SECURITY),export)
MODULE_CFLAGS += -DEXPORT_VERSION
endif

ifneq ($(SECURITY),none)
SECURE_BINS=
SECLIB=$(LIBSECURITY)
endif

ifeq ($(USE_ADMINSERVER), 1)
  ADMIN_DLLGLUEOBJ=$(BUILD_ROOT)/built/$(ARCH)-$(SECURITY)-$(DEBUG)-admin/admin-lib/dllglue.o
endif

ifeq ($(ARCH),AIX)
DLLGLUEOBJ=
GLUEOBJS=
endif

$(OBJDEST)/key.res: key.rc
	$(RC) $(OFFLAG)$(OBJDEST)/key.res ey.rc

PROGS = ds_newinst
ifeq ($(USE_ADMINSERVER), 1)
PROGS += start   restart  shutdown   ds_ldif2db \
	ds_db2ldif   ds_db2bak   ds_listdb  \
	ds_bak2db ds_rmdb ds_create \
	ds_remove ds_snmpctrl vlvindex addindex
endif

ifeq ($(ARCH), WINNT)
SERVER_PROGS = namegen latest_file
endif

ifeq ($(USE_ADMINSERVER), 1)
OBJECTS= init_ds_env.o
endif

ifeq ($(ARCH), WINNT)
OBJECTS += namegen.o latest_file.o
ifeq ($(USE_SETUPUTIL), 1)
  OBJECTS += ds_remove_uninst.o
endif
endif

ifeq ($(ARCH), WINNT)
BINS = $(addprefix $(BINDIR)/, $(addsuffix .exe, $(PROGS))) $(addprefix $(LDAP_SERVER_RELDIR)/, $(addsuffix .exe, $(SERVER_PROGS)))
else
BINS = $(addprefix $(BINDIR)/, $(PROGS))
endif
ALLOBJS = $(addprefix $(OBJDEST)/, $(OBJECTS))

PERL_SCRIPTS = migrateTo4 uname.lib Cgi.pm migrateInstance getConfigInfo migrateLocalDB migratePwdFile ds_viewlog.pl upgradeServer updatedsgw logconv.pl ds_newinst.pl

PERL_SCRIPTS_DEST = $(addprefix $(BINDIR)/, $(PERL_SCRIPTS))

INST_INCLUDES = $(OBJDIR)/install_keywords.h

TEMPLATE_SCRIPTS_SRC = $(wildcard scripts/template-*)
TEMPLATE_SCRIPTS_DEST = $(subst scripts/,$(SCRIPTSDIR)/,$(TEMPLATE_SCRIPTS_SRC))

ifeq ($(USE_64), 1)
  FIX_SECMOD_DEP = $(BINDIR)/fix_secmod_db_64
endif

# gmake 3.74 will remove "intermediate" files if generated via a pattern match rule
# this is annoying for debugging since it tries to find the .o file
# if you're debugging and you want to make sure your file does not get removed
# by gmake, just uncomment the precious target and put your object files there
# or FIX IT! if you can figure out how . . .
#.PRECIOUS: $(OBJDEST)/ds_db2bak.o

all: $(BINDIR) $(OBJDEST) $(INST_INCLUDES) $(ALLOBJS) $(BINS) \
	installPerlFiles $(SCRIPTSDIR) $(TEMPLATE_SCRIPTS_DEST) \
	$(FIX_SECMOD_DEP)

$(SCRIPTSDIR):
	$(MKDIR) $@

.PHONY: installPerlFiles

clean:
	-@echo $(BINS)
	-$(RM) $(BINS)
	-$(RM) $(OBJDEST)/*.o

strip:
	$(STRIP) $(BINS)

# if $(DLLGLUEOBJ) isn't available, use $(ADMIN_DLLGLUEOBJ) as a substitute:
$(DLLGLUEOBJ): $(ADMIN_DLLGLUEOBJ)
	cp $(ADMIN_DLLGLUEOBJ) $(DLLGLUEOBJ)

# if $(SECGLUEOBJ) isn't available, use $(ADMIN_SECGLUEOBJ) as a substitute:
$(SECGLUEOBJ): $(ADMIN_SECGLUEOBJ)
	cp $(ADMIN_SECGLUEOBJ) $(SECGLUEOBJ)

# Special objects
$(BINDIR)/ds_newinst:	$(OBJDEST)/ds_newinst.o $(OBJDEST)/cfg_sspt.o \
    $(OBJDEST)/create_instance.o $(OBJDEST)/script-gen.o
	$(LINK_EXE_NOLIBSOBJS) $(SHARED) $(EXTRALDFLAGS) \
	$(OBJDEST)/ds_newinst.o $(OBJDEST)/cfg_sspt.o \
    $(OBJDEST)/create_instance.o $(OBJDEST)/script-gen.o $(EXTRA_LIBS)

$(BINDIR)/ds_newinst.exe:	$(OBJDEST)/ds_newinst.o $(OBJDEST)/cfg_sspt.o \
    $(OBJDEST)/create_instance.o $(OBJDEST)/script-gen.o
	$(LINK_EXE) $(NT_NOLIBS) $(OBJDEST)/ds_newinst.o $(OBJDEST)/cfg_sspt.o \
    $(OBJDEST)/create_instance.o $(OBJDEST)/script-gen.o \
		$(LIBNT) $(NSPRLINK) $(EXTRA_LIBS) $(DB_LIB)
# linking this file causes a .exp and a .lib file to be generated which don't seem
# to be required while running, so I get rid of them
	$(RM) $(subst .exe,.exp,$@) $(subst .exe,.lib,$@)

$(BINDIR)/ds_create:	$(OBJDEST)/instindex.o $(OBJDEST)/cfg_sspt.o \
    $(OBJDEST)/create_instance.o $(OBJDEST)/configure_instance.o \
	$(OBJDEST)/script-gen.o $(DEPLIBS)
	$(LINK_EXE_NOLIBSOBJS) $(SHARED) $(EXTRALDFLAGS) \
		$(OBJDEST)/instindex.o $(OBJDEST)/script-gen.o \
		$(OBJDEST)/create_instance.o $(OBJDEST)/cfg_sspt.o \
		$(OBJDEST)/configure_instance.o \
		$(GLUEOBJ) $(EXTRA_LIBS)

$(BINDIR)/ds_create.exe:	$(OBJDEST)/instindex.o $(OBJDEST)/cfg_sspt.o \
    $(OBJDEST)/create_instance.o $(OBJDEST)/configure_instance.o \
	$(OBJDEST)/script-gen.o $(LIBNT_DEP) $(DEPLIBS)
	$(LINK_EXE) $(NT_NOLIBS) $(OBJDEST)/instindex.o \
		$(OBJDEST)/create_instance.o $(OBJDEST)/cfg_sspt.o \
		$(OBJDEST)/configure_instance.o $(OBJDEST)/script-gen.o \
		$(SETUPUTIL_S_LINK) $(LDAP_SDK_LIBS) $(LIBNT) \
		$(NSPRLINK) $(EXTRA_LIBS) $(DB_LIB)
# linking this file causes a .exp and a .lib file to be generated which don't seem
# to be required while running, so I get rid of them
	$(RM) $(subst .exe,.exp,$@) $(subst .exe,.lib,$@)

$(BINDIR)/ds_remove:  $(OBJDEST)/ds_remove.o $(OBJDEST)/ds_remove_uninst.o $(DEPLIBS) $(EXTRA_LIBS_DEP)
	$(LINK_EXE_NOLIBSOBJS)  $(SHARED) $(EXTRALDFLAGS) \
		$(OBJDEST)/ds_remove.o $(OBJDEST)/ds_remove_uninst.o $(OBJDEST)/init_ds_env.o \
		$(SETUPUTIL_S_LINK) $(GLUEOBJ) $(EXTRA_LIBS)

$(BINDIR)/ds_remove.exe:  $(OBJDEST)/ds_remove.o $(OBJDEST)/ds_remove_uninst.o $(DEPLIBS) $(EXTRA_LIBS_DEP)
	$(LINK_EXE) $(OBJDEST)/ds_remove_uninst.o $(OBJDEST)/ds_remove.o $(OBJDEST)/init_ds_env.o $(LDAP_SDK_LIBS) $(NSPRLINK) $(SETUPUTIL_S_LINK) $(NT_NOLIBS)
# linking this file causes a .exp and a .lib file to be generated which don't seem
# to be required while running, so I get rid of them
	$(RM) $(subst .exe,.exp,$@) $(subst .exe,.lib,$@)

$(OBJDEST)/%.o: %.c
	$(CC) -c $(CFLAGS) $(MCC_INCLUDE) $< $(OFFLAG)$@

$(OBJDEST)/%.o: %.cpp
ifeq ($(ARCH), WINNT)
	$(CC) -c $(CFLAGS) $(MCC_INCLUDE) $(SETUPUTIL_INCLUDE) $< $(OFFLAG)$@
else
	$(CXX) $(EXCEPTIONS) -c $(CFLAGS) $(MCC_INCLUDE) $(SETUPUTIL_INCLUDE) $< $(OFFLAG)$@
endif

ifneq ($(ARCH), WINNT)
$(BINDIR)/%: $(OBJDEST)/%.o  $(DEPLIBS) $(EXTRA_LIBS_DEP) $(GLUEOBJ)
	$(LINK_EXE_NOLIBSOBJS) $< $(OBJDEST)/init_ds_env.o $(GLUEOBJ) $(EXTRA_LIBS)
else
$(BINDIR)/%.exe:  $(OBJDEST)/%.o $(DEPLIBS) $(EXTRA_LIBS_DEP) 
	$(LINK_EXE) $(OBJDEST)/$*.o $(OBJDEST)/init_ds_env.o $(NSPRLINK) 
endif

$(LDAP_SERVER_RELDIR)/namegen.exe:  $(OBJDEST)/namegen.o
	$(LINK_EXE_NOLIBSOBJS) $^

$(LDAP_SERVER_RELDIR)/latest_file.exe:  $(OBJDEST)/latest_file.o
	$(LINK_EXE_NOLIBSOBJS) $^

installPerlFiles:  $(BINDIR) $(BINDIR)/Install.pl

$(BINDIR)/Install.pl: CreateInstall.pl $(PERL_SCRIPTS_DEST)
	-@$(RM) $@
	$(CP) $< $@

$(BINDIR)/%: %
	-@$(RM) $@
	$(CP) $< $@

$(LDAP_SERVER_RELDIR)/%: % $(LDAP_SERVER_RELDIR)
	-@$(RM) $@
	$(CP) $< $@

$(INST_INCLUDES): install_keywords.h
	-@$(RM) $@
	$(CP) $< $@

$(SCRIPTSDIR)/template-%: scripts/template-% $(SCRIPTSDIR)
	-@$(RM) $@
	$(CP) $< $@

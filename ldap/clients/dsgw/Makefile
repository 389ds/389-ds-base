#
# PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
# license terms. Copyright © 2001 Sun Microsystems, Inc.
# Some preexisting portions Copyright © 2001 Netscape Communications Corp.
# All rights reserved.
# 

# Gmakefile for Directory Server Gateway
#
LDAP_SRC = ../..
MCOM_ROOT = ../../../..

include $(MCOM_ROOT)/ldapserver/nsdefs.mk

ifneq ($(ARCH), WINNT)
#OLD_MCOM_ROOT := $(MCOM_ROOT)
#MCOM_ROOT = $(shell cd $(OLD_MCOM_ROOT); pwd)
#OLD_LDAP_SRC := $(LDAP_SRC)
#LDAP_SRC = $(shell cd $(OLD_LDAP_SRC); pwd)
endif

NOSTDCLEAN=true # don't let nsconfig.mak define target clean
NOSTDSTRIP=true # don't let nsconfig.mak define target strip

include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk
ifndef LDAP_USE_OLD_DB
include $(MCOM_ROOT)/ldapserver/ns_usedb.mk
endif

include dsgw_include.mk

# the dsgw binaries should use the standard rpath which includes ../../lib,
# the clients lib location
DSGW_RPATHFLAG=


ifeq ($(LDAP_NO_LIBLCACHE),1)
CFLAGS+=-DNO_LIBLCACHE
endif

ifeq ($(ARCH), HPUX)
# Use C++ compiler when linking on HP to pick up
# exception-handling flag (+eh). We need this because
# we link with other objs compiled with +eh
ifeq ($(USE_64), 1)
ALDFLAGS += +DA2.0W +DS2.0 +Z -lpthread
endif

DSGW_LINK=$(CXX)
else
ifeq ($(ARCH), WINNT)
DSGW_LINK=$(LINK_EXE)
else
ifeq ($(ARCH), Linux)
DSGW_LINK=$(CXX)
else
ifeq ($(ARCH), OSF1)
DSGW_LINK=$(CXX)
else
DSGW_LINK=$(CC)
endif # Linux
endif # OSF1
endif # WINNT
endif # HPUX

ifeq ($(ARCH), SOLARIS)
ifeq ($(USE_64), 1)
ALDFLAGS += -xarch=v9
endif
# removed -lcx from following line
CCC_LINK = -lCstd -lCrun -lm -lw -lc
endif

ifneq ($(ARCH), WINNT)
ifdef USE_LD_RUN_PATH
# LD_RUN_PATH instead of -R RPATH
LD_RUN_PATH=$(RPATHFLAG):$(DSGW_RPATHFLAG)
export LD_RUN_PATH
else # USE_LD_RUN_PATH
DSGW_LINK += $(RPATHFLAG_PREFIX)$(RPATHFLAG):$(DSGW_RPATHFLAG)
endif # USE_LD_RUN_PATH
endif # ! WINNT


ifeq ($(ARCH), AIX)
DSGW_LINK=$(CC)
DSGW_LINK += -brtl -berok $(RPATHFLAG_PREFIX)$(RPATHFLAG):$(DSGW_RPATHFLAG)$(RPATHFLAG_EXTRAS)
endif


BINDIR=$(DSGW_BIN_RELDIR)
OBJDEST=$(OBJDIR)/clients/dsgw/obj

SRCDIR=$(MCOM_ROOT)/ldapserver/ldap/clients/dsgw

ifeq ($(ARCH), WINNT)
ALDFLAGS+=-force:multiple
SUBSYSTEM=console
endif

ifneq ($(ARCH), WINNT)
INSTALLOPTIONS= -m 755
endif

ifeq ($(ARCH), OSF1)
LINKOPTIONS=-lcxx
endif

ifeq ($(ARCH), WINNT)
NSECLIBS = $(NOSSLLIBS)
EXTRA_LIBS += $(LIBS) $(ADMINUTIL_LINK) $(ICULINK) $(NSPRLINK) $(NSHTTPD) 
EXTRA_LIBS_DEP += $(LIBS) $(NSHTTP_DEP)
else
LIBS := $(DISTLIBFLAG) $(LINKOPTIONS) $(LIBLDAPU) $(LDAPLINK) \
	$(ADMINUTIL_LINK) $(DYN_NSHTTPD) \
	$(LDAP_LIBLDBM) $(ICULINK) $(NSPRLINK) \
	$(LDAP_LIBLDIF) $(ALIBS) $(DBMLINK) $(SECURITYLINK) \
	$(THREADSLIB) $(NSPRLINK) $(LDAP_LIBLITEKEY)
LIBS_DEP = $(LIBLDAPU_DEP) $(LDAP_LIBLDBM_DEP) $(LDAP_LIBLDIF_DEP)
LIBS_DEP += $(LDAPSDK_DEP) $(ICU_DEP) $(NSPR_DEP) $(DB_LIB_DEP)
NSECLIBS = $(DISTLIBFLAG) $(SSLLIBFLAG) $(LINKOPTIONS) $(LIBLDAPU) \
	$(LDAPLINK) $(ADMINUTIL_LINK) \
	$(LDAP_LIBLDBM) $(ICULINK) $(NOSSLLIBS) \
	$(LDAP_LIBLDIF) $(DBMLINK) $(ALIBS) \
	$(THREADSLIB) $(NSPRLINK) $(LDAP_LIBLITEKEY)
endif

NSECLIBS_DEP=$(SECGLUEOBJS)
NSECLIBS_DEP += $(ICU_DEP)

#EXTRA_LIBS += -l$(LIBARES)

ifeq ($(ARCH), Linux)
LIBS += -lcrypt
NSECLIBS += -lcrypt
endif

# these are the programs we must build
PROGS= auth doauth edit domodify dnedit dosearch
BINS= $(addprefix $(BINDIR)/, $(PROGS))

# The rest of the CGI programs do not use LIBLDAP and SSL at all, so we
# link them with glue routines instead of the full libsec.  This is done
# only to reduce our footprint.
# these are also programs we need to build
NEED_SECGLUE = unauth search csearch newentry tutor lang  

ifneq ($(ARCH), WINNT)
NEED_SECGLUE_BINS = $(addprefix $(BINDIR)/, $(NEED_SECGLUE))
else
NEED_SECGLUE_BINS = $(addprefix $(BINDIR)/, $(addsuffix .exe, $(NEED_SECGLUE)))
endif

NOTSHIPPINGPROGS= browse browsesrch templateindex
NOTSHIPPINGBINS= $(addprefix $(BINDIR)/, $(NOTSHIPPINGPROGS))

CKUTILPROGS= ckput ckget ckdump ckpurge ckdel
CKUTILBINS= $(addprefix $(BINDIR)/, $(CKUTILPROGS))

CMNOBJS= htmlout.o htmlparse.o error.o cgiutil.o dsgwutil.o ldaputil.o \
	entrydisplay.o config.o	cookie.o emitauth.o emitf.o collate.o vcard.o \
	Versiongw.o utf8compare.o

# MLM
ifeq ($(ARCH), WINNT)
CMNOBJS += getopt.o
endif

COMMONOBJS= $(addprefix $(OBJDEST)/, $(CMNOBJS))
COMMONOBJSREL= $(addprefix ../obj/, $(CMNOBJS))

NOSSLCMNOBJS= htmlout.o htmlparse.o error.o cgiutil.o dsgwutil.o config.o \
	cookie.o emitauth.o emitf.o collate.o Versiongw.o utf8compare.o
NOSSLCOMMONOBJS=$(addprefix $(OBJDEST)/, $(NOSSLCMNOBJS))
NOSSLCOMMONOBJSREL=$(addprefix ../obj/, $(NOSSLCMNOBJS))

SCGLOBJS= secglue.o
SECGLUEOBJS=$(addprefix $(OBJDEST)/, $(SCGLOBJS))

ALLOBJS	= dosearch.o search.o csearch.o browse.o browsesrch.o templateindex.o \
	  auth.o doauth.o unauth.o ckput.o ckget.o ckdump.o ckpurge.o ckdel.o \
	  edit.o domodify.o newentry.o genscreen.o tutor.o dnedit.o \
	  dsconfig.o dsimpldif.o dsexpldif.o lang.o utf8compare.o \
	  $(COMMONOBJS) $(SECGLUEOBJS)

INCLUDES= -I$(SDKHDIR) -I$(NSCP_DISTDIR)/include $(ICU_INCLUDE) -I$(ADMINUTIL_INCPATH)

DEFINES	+= $(DEFS) $(SSL) $(XP_FLAG)

CFLAGS	+= $(INCLUDES) $(DEFINES) $(ACFLAGS)
# Uncomment the following for debug output to /tmp/dsgw/<CGI> and <CGI>.out
# if /tmp/dsgw exists and is a directory.
#CFLAGS  += -DDSGW_DEBUG -g
# Uncomment the following line to build without SSL support
#CFLAGS += -DDSGW_NO_SSL

ifeq ($(ARCH), WINNT)
PROGBINS = $(addsuffix .exe, $(BINS))
NOTSHIPPINGPROGBINS = $(addsuffix .exe, $(NOTSHIPPINGBINS))
CKUTILPROGBINS = $(addsuffix .exe, $(CKUTILBINS))
else
PROGBINS = $(BINS)
NOTSHIPPINGPROGBINS = $(NOTSHIPPINGBINS)
CKUTILPROGBINS = $(CKUTILBINS)
endif

ALLBINS= $(PROGBINS) $(NOTSHIPPINGPROGBINS) $(CKUTILPROGBINS)

## make sure there's prog.exe and prog for NT. Weak, but ES 
## only works with prog, and AS needs both.
#ifeq ($(ARCH), WINNT)
#all:	prerequisites $(PROGBINS) $(NEED_SECGLUE_BINS) nt_dsgw_bins all-html all-config
#
#nt_dsgw_bins:
#	+$(DO_COPY_DSGW_BINS)
#
#COPYBINS = $(PROGS) $(NEED_SECGLUE)
#
#DO_COPY_DSGW_BINS = \
#	for dsgw_bin in $(COPYBINS); do \
#		mv $(RELDIR)/clients/dsgw/bin/$$dsgw_bin.exe $(RELDIR)/clients/dsgw/bin/$$dsgw_bin; \
#	done
#else
all:	prerequisites $(PROGBINS) $(NEED_SECGLUE_BINS) all-html all-config
#endif

prerequisites:	$(BINDIR) $(OBJDEST)
#	-@echo $(NEED_SECGLUE_BINS)
#	-@echo $(PROGBINS)

# if $(SECGLUEOBJ) isn't available, use $(ADMIN_SECGLUEOBJ) as a substitute:
$(SECGLUEOBJ): $(ADMIN_SECGLUEOBJ)
	cp $(ADMIN_SECGLUEOBJ) $(SECGLUEOBJ)

all-html:
	cd html; $(MAKE) $(MFLAGS) all
	cd admhtml; $(MAKE) $(MFLAGS) all
	cd userhtml; $(MAKE) $(MFLAGS) all
	cd pbhtml; $(MAKE) $(MFLAGS) all

$(OBJDEST):
	$(MKDIR) -p $@

all-config:
	cd config; $(MAKE) $(MFLAGS) all
	cd pbconfig; $(MAKE) $(MFLAGS) all

ckutils:	$(CKUTILPROGBINS)

notshipping: $(NOTSHIPPINGPROGBINS)

$(LDAP_LIBLDBM_DEP):
	cd $(LDAP_SRC)/libraries; $(MAKE) $(MFLAGS) clientSDK

$(LDAP_SDK_LIBLCACHE_DLL_DEP): $(LDAP_LIBLDBM_DEP)

sort: $(BINDIR)/sort

.PHONY: sort

#
# The remainder are the CGI programs that make up the gateway itself.
# We ship all of these.
#
ifneq ($(ARCH), WINNT)
$(PROGBINS): $(BINDIR)/%: $(OBJDEST)/%.o $(COMMONOBJS) $(LIBS_DEP)
	$(DSGW_LINK) $(ALDFLAGS) -o $@ $< $(COMMONOBJS) $(LIBS) $(CCC_LINK)

$(NEED_SECGLUE_BINS): $(BINDIR)/%: $(OBJDEST)/%.o $(NOSSLCOMMONOBJS) $(NSECLIBS_DEP)
	$(DSGW_LINK) $(ALDFLAGS) -o $@ $< $(NOSSLCOMMONOBJS) \
	$(NSECLIBS) $(CCC_LINK)
else
$(PROGBINS): $(BINDIR)/%.exe: $(OBJDEST)/%.o $(COMMONOBJS)
	$(DSGW_LINK) $(ALDFLAGS) $< $(COMMONOBJS)  \
		$(LDAPLINK) $(SECURITYLINK) $(ICULINK) $(CCC_LINK)
# sometimes linking executables can produce unneeded .lib or .exp files
	-@$(RM) $(subst .exe,.lib,$@) $(subst .exe,.exp,$@)

$(NEED_SECGLUE_BINS): $(BINDIR)/%.exe: $(OBJDEST)/%.o $(NOSSLCOMMONOBJS) $(SECGLUEOBJS)
	$(DSGW_LINK) $(ALDFLAGS) $< $(NOSSLCOMMONOBJS) \
	$(LDAPLINK) $(SECGLUEOBJS) $(ICULINK) $(CCC_LINK)
# sometimes linking executables can produce unneeded .lib or .exp files
	-@$(RM) $(subst .exe,.lib,$@) $(subst .exe,.exp,$@)
endif

clean:	clean-html clean-config
	rm -rf $(BINDIR)
	rm -rf $(OBJDEST)

clean-html:
	cd html; $(MAKE) $(MFLAGS) clean
	cd pbhtml; $(MAKE) $(MFLAGS) clean

clean-config:
	cd config; $(MAKE) $(MFLAGS) clean
	cd pbconfig; $(MAKE) $(MFLAGS) clean

strip:
	$(STRIP) $(ALLBINS)

$(OBJDEST)/%.o: %.c
	$(CC) -c $(CFLAGS) $(MCC_INCLUDE) $< $(OFFLAG)$@

CFLAGS += -I../../include

#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# These are the default values 
#
# Individual modules may override them
#
# BUILD_BOMB=[-DPUMPKIN_HOUR=xxxxxxx or just leave it empty]
# BUILD_DEBUG=[full, optimize, purify, quantify]
# BUILD_MODULE=[HTTP_ADMIN, HTTP_PERSONAL, HTTP_ENTERPRISE, ...]
# BUILD_SECURITY=[none, export, domestic]

TMP_ARCH := $(shell uname -s)
RELEASE := $(shell uname -r)
ifeq ($(TMP_ARCH), WINNT)
BUILD_ARCH := $(shell uname -s)
else
BUILD_ARCH := $(shell $(BUILD_ROOT)/nsarch)
endif

USE_HCL=1

PUMPKIN_AGE := 120
#BUILD_BOMB=
BUILD_BOMB=-DPUMPKIN_HOUR=$(shell cat $(BUILD_ROOT)/pumpkin.dat)
BUILD_CHECKER=no
ifndef BUILD_DEBUG
BUILD_DEBUG=full
endif
BUILD_MODULE=DIRECTORY
BUILD_NSPR_THREADS=yes
ifndef BUILD_SECURITY
BUILD_SECURITY=domestic
endif
ifeq ($(USE_PTHREADS), 1)
  ifeq ($(USE_N32), 1)
    BUILD_PTHREADS=-n32-pth
  else
    BUILD_PTHREADS=-pth
  endif
else
  BUILD_PTHREADS=
endif

ifdef USE_64
  NS64TAG=_64
endif

# Check if we're on RHEL
ifeq ($(BUILD_ARCH), Linux)
  ARCH_TEST := $(shell cat /etc/redhat-release)
  ifeq ($(findstring Taroon, $(ARCH_TEST)),Taroon)
    BUILD_ARCH = RHEL3
  else
  ifeq ($(findstring Nahant,  $(ARCH_TEST)),Nahant)
    BUILD_ARCH = RHEL4
  endif
  endif
endif

# Should we build Java code on this platform?
ifndef BUILD_JAVA_CODE
ifeq ($(BUILD_ARCH),SOLARIS)
BUILD_JAVA_CODE=1
else
ifeq ($(BUILD_ARCH),WINNT)
BUILD_JAVA_CODE=1
else
BUILD_JAVA_CODE=0
endif # WINNT
endif # SOLARIS
endif # !BUILD_JAVA_CODE

NSPR_SUF=20
LDAP_SUF=50

# We can't have lite fortezza ( I don't think it makes sense ).
ifdef FORTEZZA
BUILD_FORTEZZA=fortezza
else
ifdef LITE
BUILD_FORTEZZA=lite
else
BUILD_FORTEZZA=normal
endif
endif

ifdef LITE
IS_DIR_LITE=true
else
IS_DIR_LITE=false
endif

# Foreign language support
WEBSERVER_LANGS = ja fr de

# default path where to look for shared libraries at runtime
ifeq ($(BUILD_ARCH), WINNT)
RPATHFLAG=..\bin\slapd\lib:..\lib:..\..\lib:..\..\..\lib:..\..\..\..\lib
else
ifeq ($(BUILD_ARCH), SOLARIS)
RPATHFLAG=\$$ORIGIN/../bin/slapd/lib:\$$ORIGIN:\$$ORIGIN/../lib:\$$ORIGIN/../../lib:\$$ORIGIN/../../../lib:\$$ORIGIN/../../../../lib
else
RPATHFLAG=../bin/slapd/lib:.:../lib:../../lib:../../../lib:../../../../lib
endif
endif

MAJOR_VERSION:="7"
MINOR_VERSION:="1"
MAJOR_VERSION_NOQUOTES:=7
MINOR_VERSION_NOQUOTES:=1

# the LINK version is the one used for -l linking e.g. -l$(LINK_HTTPDLL_NAME)
ifeq ($(BUILD_ARCH), WINNT)
LINK_HTTPDLL_NAME=ns-dshttpd$(MAJOR_VERSION_NOQUOTES)$(MINOR_VERSION_NOQUOTES)
BUILD_HTTPDLL_NAME=$(LINK_HTTPDLL_NAME)
else
LINK_HTTPDLL_NAME=ns-dshttpd$(MAJOR_VERSION)$(MINOR_VERSION)
BUILD_HTTPDLL_NAME=lib$(LINK_HTTPDLL_NAME)
endif # WINNT

ifeq ($(BUILD_ARCH), WINNT)
GUNZIP=gzip -d
BUILD_DLL_VERSION=yes
BUILD_ADMIN_NAME=ns-admin30
BUILD_IIOPLIB=yes
else
GUNZIP=gunzip -d
endif

define echo_build_parms
echo ==== Building with the following parameters ====
echo BUILD_ARCH=$(BUILD_ARCH) 
echo BUILD_MODULE=$(BUILD_MODULE)
echo BUILD_SECURITY=$(BUILD_SECURITY)
echo BUILD_DEBUG=$(BUILD_DEBUG)
echo BUILD_FORTEZZA=$(BUILD_FORTEZZA)
echo BUILD_NSPR_THREADS=$(BUILD_NSPR_THREADS)
echo BUILD_BOMB=$(BUILD_BOMB)
echo BUILD_DLL_VERSION=$(BUILD_DLL_VERSION)
echo BUILD_HTTPDLL_NAME=$(BUILD_HTTPDLL_NAME)
echo BUILD_ADMIN_NAME=$(BUILD_ADMIN_NAME)
echo BUILD_CHECKER=$(BUILD_CHECKER)
echo BUILD_IIOPLIB=$(BUILD_IIOPLIB)
endef

#
# Set up these names because most of the makefiles use them now.
#
ifeq ($(findstring RHEL, $(BUILD_ARCH)), RHEL)
  ARCH = Linux
else
  ARCH=$(BUILD_ARCH)
endif
SECURITY=$(BUILD_SECURITY)
DEBUG=$(BUILD_DEBUG)
B_FORTEZZA=$(BUILD_FORTEZZA)
BOMB=$(BUILD_BOMB)
NSPR_THREADS=$(BUILD_NSPR_THREADS)
BUILD_DLL=$(BUILD_DLL_VERSION)
ADMINDLL_NAME=$(BUILD_ADMIN_NAME)
HTTPDLL_NAME=$(BUILD_HTTPDLL_NAME)
BOUNDS_CHECKER=$(BUILD_CHECKER)
RESOLV_FLAG=
DO_SEARCH=
NODE_FLAG=-DNO_NODELOCK

# It would be best if all output dir definitions below used this, rather than
# repeating it
ifeq ($(ARCH), WINNT)
ifdef DEBUG_RUNTIME
ifeq ($(DEBUG), full)
RTSUFFIX=-d
endif
endif
endif
BASIC_OBJDIR=$(BUILD_ROOT)/built/$(ARCH)$(NS64TAG)-$(SECURITY)-$(DEBUG)$(RTSUFFIX)-$(B_FORTEZZA)

#
# -- Directory Server Section -----------------------------------------------
#

ifeq ($(BUILD_MODULE), DIRECTORY)
ifdef NSPR20
 NSPR_DIR=nspr20
else
 NSPR_DIR=nspr
endif
NSPR_BASENAME=libnspr21
PRODUCT="Brandx Directory Server"
PRODUCT_IS_DIRECTORY_SERVER=1
INSTANCE_NAME_PREFIX="Directory Server"
DIR=slapd
NS_PRODUCT=DIRECTORY_SERVER
ARCHPROCESSOR=$(ARCH)
ifdef INCLUDE_SSL
SSL_PREFIX=-ssl
endif
ifeq ($(findstring RHEL, $(BUILD_ARCH)), RHEL)
    NS_BUILD_FLAVOR = $(BUILD_ARCH)$(NS64TAG)-$(SECURITY)$(SSL_PREFIX)-$(DEBUG)$(RTSUFFIX)-$(BUILD_FORTEZZA)$(BUILD_PTHREADS)-$(DIR)
    ARCHPROCESSOR = $(BUILD_ARCH)
else
  NS_BUILD_FLAVOR = $(ARCH)$(NS64TAG)-$(SECURITY)$(SSL_PREFIX)-$(DEBUG)$(RTSUFFIX)-$(BUILD_FORTEZZA)$(BUILD_PTHREADS)-$(DIR)
endif
NC_BUILD_FLAVOR = $(NSCONFIG)$(NSOBJDIR_TAG).OBJ
ifeq ($(ARCH), WINNT)
ifeq ($(PROCESSOR), ALPHA)
ARCHPROCESSOR=$(ARCH)$(PROCESSOR)
endif
endif
COMMON_OBJDIR=$(BUILD_ROOT)/built/$(ARCHPROCESSOR)$(NS64TAG)-$(SECURITY)$(SSL_PREFIX)-$(DEBUG)$(RTSUFFIX)-$(BUILD_FORTEZZA)$(BUILD_PTHREADS)-$(DIR)
COMMON_OBJDIR_32=$(BUILD_ROOT)/built/$(ARCHPROCESSOR)-$(SECURITY)$(SSL_PREFIX)-$(DEBUG)$(RTSUFFIX)-$(BUILD_FORTEZZA)$(BUILD_PTHREADS)-$(DIR)
OBJDIR=$(COMMON_OBJDIR)
OBJDIR_32=$(COMMON_OBJDIR_32)
DO_SEARCH=no
DIR_VERSION:=7.1
DIR_NORM_VERSION:=7.1
# When you change DIRSDK_VERSION or DIRSDK_VERSION_DLL_SUFFIX, you must
# update all of the .exp and .def files by executing the following command:
#	cd ldap/libraries; gmake exportfiles
# Don't forget to commit the new files.  Eventually this step will be
# integrated into the build process.  -- Mark Smith <mcs@netscape.com>
DIRSDK_VERSION:=3.1
DIRSDK_VERSION_DLL_SUFFIX:=$(LDAP_SUF)
LDAP_NO_LIBLCACHE:=1

DIRVERDIR=$(COMMON_OBJDIR)/include
DIRVER_H=$(DIRVERDIR)/dirver.h
SDKVER_H=$(DIRVERDIR)/sdkver.h
endif

#
# -- Default Section --------------------------------------------------------
#
# Some of the _OBJDIR is maintained for backward compatibility until they
# are all cleaned up.  Most of them heavily dependent on value of $(DIR)
#

ifndef AMDSERV_OBJDIR
ADMSERV_OBJDIR=$(BASIC_OBJDIR)-admin
endif

ifndef COMMON_OBJDIR
COMMON_OBJDIR=$(BASIC_OBJDIR)-$(DIR)
endif

ifndef HTTPD_OBJDIR
HTTPD_OBJDIR=$(BASIC_OBJDIR)-$(DIR)
endif

ifndef MC_ICONS_OBJDIR
MC_ICONS_OBJDIR=$(BASIC_OBJDIR)-$(DIR)
endif

ifndef OBJDIR
OBJDIR=$(BASIC_OBJDIR)-$(DIR)
endif

ifndef PLUGINS_OBJDIR
PLUGINS_OBJDIR=$(BASIC_OBJDIR)-$(DIR)/plugins
endif

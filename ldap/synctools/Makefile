#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

# GNU Makefile for Directory Server and Ldap SDK
#
BUILD_ROOT = ../../
include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk

all: passsync

passsync:
ifeq ($(ARCH), WINNT)
	# Do not allow MAKEFLAGS or other environment variables to influence nmake.
	cd passwordsync; env -i PATH="${PATH}" LIB="${LIB}" INCLUDE="${INCLUDE}" BUILD_DEBUG=${BUILD_DEBUG} build.bat
endif

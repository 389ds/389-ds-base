#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server "Replication" plugin
#
#

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/replication-plugin
LIBDIR = $(LIB_RELDIR)

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./replication.def
endif

CFLAGS += $(SLCFLAGS) -DSLAPD_LOGGING

ifeq ($(ARCH), WINNT)
CFLAGS += /WX
endif

ifdef TEST_CL5
CFLAGS += -DTEST_CL5
endif

INCLUDES += -I$(LDAP_SRC)/servers/slapd -I$(DB_INCLUDE)

LOCAL_OBJS= \
	cl5_api.o \
	cl5_clcache.o \
	cl5_config.o \
	cl5_init.o \
	csnpl.o\
	legacy_consumer.o \
	llist.o\
	repl5_agmt.o \
	repl5_agmtlist.o \
	repl5_backoff.o \
	repl5_connection.o \
	repl5_inc_protocol.o \
	repl5_init.o\
	repl5_protocol.o \
	repl5_protocol_util.o \
	repl5_replica.o\
	repl5_replica_config.o\
	repl5_ruv.o\
	repl5_schedule.o \
	repl5_tot_protocol.o \
	repl5_total.o\
	repl5_mtnode_ext.o\
	repl5_plugins.o \
	repl_add.o \
	repl_bind.o \
	repl_compare.o \
	repl_connext.o \
	repl_controls.o \
	repl_delete.o \
	repl_entry.o \
	repl_ext.o \
	repl_extop.o \
	repl_globals.o \
	repl_init.o \
	repl_modify.o \
	repl_modrdn.o \
	repl_monitor.o \
	repl_objset.o \
	repl_opext.o \
	repl_ops.o \
	repl_rootdse.o \
	repl_search.o \
	replutil.o \
	urp.o \
	urp_glue.o \
	urp_tombstone.o \
	repl5_replica_hash.o\
	repl5_replica_dnhash.o\
	repl5_updatedn_list.o\
	windows_inc_protocol.o \
	windows_tot_protocol.o \
	windows_total.o \
	windows_protocol_util.o \
	windows_private.o \
	windows_connection.o

LIBREPLICATION_OBJS = $(addprefix $(OBJDEST)/, $(LOCAL_OBJS)) 

ifeq ($(ARCH), WINNT)
REPLICATION_DLL_OBJ = $(addprefix $(OBJDEST)/, dllmain.o)
endif

LIBREPLICATION=	$(addprefix $(LIBDIR)/, $(REPLICATION_DLL).$(DLL_SUFFIX))

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(DB_LIB_DEP) $(NSPR_DEP)
EXTRA_LIBS += $(LIBSLAPD) $(LDAPLINK) $(DB_LIB)
endif

ifeq ($(ARCH), AIX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(DB_LIB_DEP) $(NSPR_DEP)
EXTRA_LIBS += $(LIBSLAPD) $(LDAP_SDK_LIBLDAP_DLL) $(DB_LIB)
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(DYN_NSHTTPD) $(ADMINUTIL_LINK) $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
endif

ifeq ($(ARCH), WINNT)
DLL_LDFLAGS += -def:"./replication.def"
endif # WINNT

ifeq ($(ARCH), AIX)
EXTRA_LIBS += $(DLL_EXTRA_LIBS) 
LD=ld
endif

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(LIBREPLICATION)

$(LIBREPLICATION): $(LIBREPLICATION_OBJS) $(REPLICATION_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(LIBREPLICATION_OBJS) $(REPLICATION_DLL_OBJ) $(PLATFORMLIBS) $(EXTRA_LIBS) $(LDAP_LIBLDIF) $(NSPRLINK)

tests:	$(TEST_PROGS)

veryclean: clean

clean:
	$(RM) $(LIBREPLICATION_OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(REPLICATION_DLL_OBJ)
endif
	$(RM) $(LIBREPLICATION)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

#
# header file dependencies (incomplete)
#
$(LIBREPLICATION_OBJS):	

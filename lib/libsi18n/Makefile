#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# The libsi18n library support for getting strings from the
# string database.

MCOM_ROOT=../../..
MODULE=LibsI18N
include ../../nsdefs.mk

NSDEFS_PRODUCT = $(NS_PRODUCT)

OBJDEST=$(OBJDIR)/lib/libsi18n

L10NDIR = $(MCOM_ROOT)/ldapserver/l10n

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
ifeq ($(BUILD_MODULE), HTTP_ENTERPRISE)
StringDatabase = ns-httpd.db
L10N_SERVER = httpd
DBTheaders = \
        ../../httpd/src/dbthttpd.h \
        ../../include/base/dbtbase.h \
        ../../include/frame/dbtframe.h \
        ../../include/httpdaemon/dbthttpdaemon.h \
        ../../include/libaccess/dbtlibaccess.h \
        ../../include/libadmin/dbtlibadmin.h \
        ../../include/libir/dbtlibir.h \
        gshttpd.h
endif

ifeq ($(BUILD_MODULE), DIRECTORY)
 StringDatabase = ns-slapd.properties
 DBTheaders = \
    ../../httpd/src/dbthttpd.h \
    ../../include/base/dbtbase.h \
    ../../include/frame/dbtframe.h \
    ../../include/httpdaemon/dbthttpdaemon.h \
    ../../include/libaccess/dbtlibaccess.h \
    ../../include/libadmin/dbtlibadmin.h \
    ../../include/libir/dbtlibir.h \
    ../../ldap/clients/dsgw/dbtdsgw.h \
    gsslapd.h
 endif


ifeq ($(BUILD_MODULE), HTTP_PERSONAL)
StringDatabase = ns-httpd.db
L10N_SERVER = httpd
DBTheaders = \
        ../../httpd/src/dbthttpd.h \
        ../../include/base/dbtbase.h \
        ../../include/frame/dbtframe.h \
        ../../include/httpdaemon/dbthttpdaemon.h \
        ../../include/libaccess/dbtlibaccess.h \
        ../../include/libadmin/dbtlibadmin.h \
        gshttpd.h
endif

ifeq ($(BUILD_MODULE), HTTP_PROXY)
StringDatabase = ns-proxy.db
DBTheaders = \
        ../../httpd/src/dbthttpd.h \
        ../../include/base/dbtbase.h \
        ../../include/frame/dbtframe.h \
        ../../include/httpdaemon/dbthttpdaemon.h \
        ../../include/libaccess/dbtlibaccess.h \
        ../../include/libadmin/dbtlibadmin.h \
        gsproxy.h
endif

ifeq ($(BUILD_MODULE), HTTP_ADMIN)
StringDatabase = ns-admin.db
L10N_SERVER = admserv
DBTheaders = \
        ../../admserv/src/dbtadmserv.h \
        ../../admserv/cgi-src/dbtcgiadmin.h \
        ../../admserv/user-forms/src/dbtuserforms.h \
        ../../include/base/dbtbase.h \
        ../../include/frame/dbtframe.h \
        ../../include/httpdaemon/dbthttpdaemon.h \
        ../../include/libaccess/dbtlibaccess.h \
        ../../include/libadmin/dbtlibadmin.h \
        ../../include/libir/dbtlibir.h \
        gsadmserv.h
endif

include ../../nsconfig.mk

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

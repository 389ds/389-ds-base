#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# The admin libraries

BUILD_ROOT=../..

MODULE=LibAdmin
MODULE_CFLAGS=-DENCRYPT_PASSWORDS -DUSE_ADMSERV

include $(BUILD_ROOT)/nsdefs.mk

OBJDEST=$(OBJDIR)/lib/libadmin

ifeq ($(ARCH), WINNT)
LIBS=$(OBJDIR)/lib/libadmin.lib
else
LIBS=$(OBJDIR)/lib/libadmin.a
endif

OBJS=$(addprefix $(OBJDEST)/, error.o \
                              template.o util.o \
                              authdb.o $(OSOBJS))

# moved files [to libadminutil] : form_post.o distadm.o cron_conf.o 
# candidate for moing: admlog.o error.o admserv.o
# replaced files: error.o [by libadminutil/errRpt.c]
# removed for lack of ndbm support from binary release of libdbm.... userdb.o
#!! nsnews.o does not work anymore.

all: $(OBJDEST) $(LIBS)

#$(LIBS): $(addprefix $(BUILD_ROOT)/include/libadmin/, \
#                     hadm_msgs.i la_msgs.i)

include $(BUILD_ROOT)/nsconfig.mk

MCC_INCLUDE += $(ADMINUTIL_INCLUDE)

#ifeq ($(ARCH), HPUX)
#CC=$(CCC)
#endif

$(OBJDEST):
	mkdir -p $(OBJDEST)

$(LIBS): $(OBJS)
	rm -f $@
	$(AR) $(OBJS)
	$(RANLIB) $@

include $(INCLUDE_DEPENDS)

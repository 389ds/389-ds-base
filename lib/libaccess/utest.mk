#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#

#CFLAGS = -g -DDEBUG -I.
CFLAGS = -g -I. -I../../../include $(TESTFLAGS)
#LEX = flex
CC=gcc

HEAD = aclparse.h acltools.h lparse.h acl.h acleval.h lasdns.h lasip.h mthash.h stubs.h aclscan.h acl.tab.h
XSRC = aclparse.y aclscan.l 
CSRC = acleval.c aclutil.c lasdns.c lasip.c lastod.c mthash.c testmain.c acltools.c space.c acl.tab.c acl.yy.c
SRC  = $(HEAD) $(XSRC) $(CSRC)

XOBJ = acl.tab.o acl.yy.o testmain.o acltools.o 
COBJ = $(CSRC:%.c=%.o)
OBJ	 = $(XOBJ) $(COBJ)

always: $(OBJ) 

acleval.o:	stubs.h aclparse.h acl.h acleval.h mthash.h

aclutil.o: 	acl.h aclparse.h

lasdns.o:	acl.h aclparse.h lasdns.h mthash.h

lasip.o:	acl.h aclparse.h lasip.h

lastod.o:	acl.h aclparse.h

acltools.o:	aclparse.h aclscan.h lparse.h aclparse.y

testmain.o:	aclparse.h acltools.h

acl.yy.o: acl.yy.c acl.tab.h

acl.yy.o acl.tab.o acltools.o: aclparse.h acltools.h lparse.h

yacc: aclparse.y
	$(YACC) -dv aclparse.y
	mv y.tab.h acl.tab.h
	mv y.tab.c acl.tab.c
#sed -f yy-sed y.tab.h > acl.tab.h
#sed -f yy-sed y.tab.c > acl.tab.c

# Should only run this on an SGI, where flex() is present
flex: aclscan.l
	$(LEX) aclscan.l
	mv lex.yy.c acl.yy.c
#sed -f yy-sed lex.yy.c > acl.yy.c

clean:
	rm -f aclparse aclparse.pure y.output acl.tab.c acl.tab.h acl.yy.c lex.yy.c y.tab.c y.tab.h aclparse.c $(OBJ) 

#	Check it out from the RCS directory
$(SRC): RCS/$$@,v
	co $@

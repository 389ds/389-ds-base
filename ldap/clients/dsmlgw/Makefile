NOSTDCLEAN=true
NO_BUILD_NUM=true
COMPONENT_DEPS=true


MCOM_ROOT=../../../..

include ../../../nsconfig.mk
include ../../../ldap/javarules.mk

all: $(TOMCAT_DEP) $(ANT_DEP) $(LDAPJDK_DEP)
	$(ANT) 

clean: 	
	$(ANT) clean


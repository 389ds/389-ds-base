#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
1. Download Wix (http://sourceforge.net/projects/wix/) and unzip it into the Wix folder.
   (steps 2 and 3 can be skipped if ldapserver has been built)
2. Add the location of the Mozilla LDAP C SDK header files to your INCLUDE path.
   The LDAP C SDK is a component of the Directory Server and is "pulled"
   when the Directory Server is built; it can also be obtained individually from Mozilla.
   e.x. 
      set INCLUDE=%INCLUDE%;c:\source\dist\WINNT5.0_DBG.OBJ\ldapsdk\include
3. Add the location of the LDAP C SDK libraries to your LIB path.
   e.x. 
      set LIB=%LIB%;c:\source\dist\WINNT5.0_DBG.OBJ\ldapsdk\lib
4. Run build.bat.

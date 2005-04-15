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
# do so, delete this exception statement from your version. 
# 
# 
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# Microsoft Developer Studio Generated NMAKE File, Based on testplugin.dsp
!IF "$(CFG)" == ""
CFG=testplugin - Win32 Release
!MESSAGE No configuration specified. Defaulting to testplugin - Win32 Release.
!ENDIF 

!IF "$(CFG)" != "testplugin - Win32 Release" && "$(CFG)" !=\
 "testplugin - Win32 Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "testplugin.mak" CFG="testplugin - Win32 Release"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "testplugin - Win32 Release" (based on\
 "Win32 (x86) Dynamic-Link Library")
!MESSAGE "testplugin - Win32 Debug" (based on\
 "Win32 (x86) Dynamic-Link Library")
!MESSAGE 
!ERROR An invalid configuration is specified.
!ENDIF 

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE 
NULL=nul
!ENDIF 

CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "testplugin - Win32 Release"

OUTDIR=.\Release
INTDIR=.\Release
# Begin Custom Macros
OutDir=.\.\Release
# End Custom Macros

!IF "$(RECURSE)" == "0" 

ALL : "$(OUTDIR)\testplugin.dll"

!ELSE 

ALL : "$(OUTDIR)\testplugin.dll"

!ENDIF 

CLEAN :
	-@erase "$(INTDIR)\dllmain.obj"
	-@erase "$(INTDIR)\testbind.obj"
	-@erase "$(INTDIR)\testentry.obj"
	-@erase "$(INTDIR)\testextendedop.obj"
	-@erase "$(INTDIR)\testpostop.obj"
	-@erase "$(INTDIR)\testpreop.obj"
	-@erase "$(INTDIR)\testsaslbind.obj"
	-@erase "$(INTDIR)\vc50.idb"
	-@erase "$(OUTDIR)\testplugin.dll"
	-@erase "$(OUTDIR)\testplugin.exp"
	-@erase "$(OUTDIR)\testplugin.lib"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

CPP_PROJ=/nologo /MD /W3 /GX /O2 /I "..\include" /D "WIN32" /D "NDEBUG" /D\
 "_WINDOWS" /D "_WIN32" /Fp"$(INTDIR)\testplugin.pch" /YX /Fo"$(INTDIR)\\"\
 /Fd"$(INTDIR)\\" /FD /c 
CPP_OBJS=.\Release/
CPP_SBRS=.
MTL_PROJ=/nologo /D "NDEBUG" /mktyplib203 /win32 
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\testplugin.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib\
 advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib\
 odbccp32.lib wsock32.lib ..\lib\libslapd.lib /nologo /subsystem:windows /dll\
 /incremental:no /pdb:"$(OUTDIR)\testplugin.pdb" /machine:I386\
 /def:".\testplugin.def" /out:"$(OUTDIR)\testplugin.dll"\
 /implib:"$(OUTDIR)\testplugin.lib" 
DEF_FILE= \
	".\testplugin.def"
LINK32_OBJS= \
	"$(INTDIR)\dllmain.obj" \
	"$(INTDIR)\testbind.obj" \
	"$(INTDIR)\testentry.obj" \
	"$(INTDIR)\testextendedop.obj" \
	"$(INTDIR)\testpostop.obj" \
	"$(INTDIR)\testpreop.obj" \
	"$(INTDIR)\testsaslbind.obj"

"$(OUTDIR)\testplugin.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ELSEIF  "$(CFG)" == "testplugin - Win32 Debug"

OUTDIR=.\Debug
INTDIR=.\Debug
# Begin Custom Macros
OutDir=.\.\Debug
# End Custom Macros

!IF "$(RECURSE)" == "0" 

ALL : "$(OUTDIR)\testplugin.dll"

!ELSE 

ALL : "$(OUTDIR)\testplugin.dll"

!ENDIF 

CLEAN :
	-@erase "$(INTDIR)\dllmain.obj"
	-@erase "$(INTDIR)\testbind.obj"
	-@erase "$(INTDIR)\testentry.obj"
	-@erase "$(INTDIR)\testextendedop.obj"
	-@erase "$(INTDIR)\testpostop.obj"
	-@erase "$(INTDIR)\testpreop.obj"
	-@erase "$(INTDIR)\testsaslbind.obj"
	-@erase "$(INTDIR)\vc50.idb"
	-@erase "$(INTDIR)\vc50.pdb"
	-@erase "$(OUTDIR)\testplugin.dll"
	-@erase "$(OUTDIR)\testplugin.exp"
	-@erase "$(OUTDIR)\testplugin.ilk"
	-@erase "$(OUTDIR)\testplugin.lib"
	-@erase "$(OUTDIR)\testplugin.pdb"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

CPP_PROJ=/nologo /MD /W3 /Gm /GX /Zi /Od /I "..\include" /D "WIN32" /D "_DEBUG"\
 /D "_WINDOWS" /D "_WIN32" /Fp"$(INTDIR)\testplugin.pch" /YX /Fo"$(INTDIR)\\"\
 /Fd"$(INTDIR)\\" /FD /c 
CPP_OBJS=.\Debug/
CPP_SBRS=.
MTL_PROJ=/nologo /D "_DEBUG" /mktyplib203 /win32 
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\testplugin.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib\
 advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib\
 odbccp32.lib wsock32.lib ..\lib\libslapd.lib /nologo /subsystem:windows /dll\
 /incremental:yes /pdb:"$(OUTDIR)\testplugin.pdb" /debug /machine:I386\
 /def:".\testplugin.def" /out:"$(OUTDIR)\testplugin.dll"\
 /implib:"$(OUTDIR)\testplugin.lib" 
DEF_FILE= \
	".\testplugin.def"
LINK32_OBJS= \
	"$(INTDIR)\dllmain.obj" \
	"$(INTDIR)\testbind.obj" \
	"$(INTDIR)\testentry.obj" \
	"$(INTDIR)\testextendedop.obj" \
	"$(INTDIR)\testpostop.obj" \
	"$(INTDIR)\testpreop.obj" \
	"$(INTDIR)\testsaslbind.obj"

"$(OUTDIR)\testplugin.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ENDIF 

.c{$(CPP_OBJS)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(CPP_OBJS)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(CPP_OBJS)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.c{$(CPP_SBRS)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(CPP_SBRS)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(CPP_SBRS)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<


!IF "$(CFG)" == "testplugin - Win32 Release" || "$(CFG)" ==\
 "testplugin - Win32 Debug"
SOURCE=.\dllmain.c

!IF  "$(CFG)" == "testplugin - Win32 Release"

DEP_CPP_DLLMA=\
	{$(INCLUDE)}"lber.h"\
	{$(INCLUDE)}"ldap.h"\
	

"$(INTDIR)\dllmain.obj" : $(SOURCE) $(DEP_CPP_DLLMA) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "testplugin - Win32 Debug"

DEP_CPP_DLLMA=\
	{$(INCLUDE)}"lber.h"\
	{$(INCLUDE)}"ldap.h"\
	{$(INCLUDE)}"sys\types.h"\
	
NODEP_CPP_DLLMA=\
	"..\include\macsock.h"\
	"..\include\os2sock.h"\
	

"$(INTDIR)\dllmain.obj" : $(SOURCE) $(DEP_CPP_DLLMA) "$(INTDIR)"


!ENDIF 

SOURCE=.\testbind.c

!IF  "$(CFG)" == "testplugin - Win32 Release"

DEP_CPP_TESTB=\
	"..\include\slapi-plugin.h"\
	{$(INCLUDE)}"lber.h"\
	{$(INCLUDE)}"ldap.h"\
	

"$(INTDIR)\testbind.obj" : $(SOURCE) $(DEP_CPP_TESTB) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "testplugin - Win32 Debug"

DEP_CPP_TESTB=\
	"..\include\slapi-plugin.h"\
	{$(INCLUDE)}"lber.h"\
	{$(INCLUDE)}"ldap.h"\
	{$(INCLUDE)}"sys\types.h"\
	
NODEP_CPP_TESTB=\
	"..\include\macsock.h"\
	"..\include\os2sock.h"\
	

"$(INTDIR)\testbind.obj" : $(SOURCE) $(DEP_CPP_TESTB) "$(INTDIR)"


!ENDIF 

SOURCE=.\testentry.c

!IF  "$(CFG)" == "testplugin - Win32 Release"

DEP_CPP_TESTE=\
	"..\include\slapi-plugin.h"\
	{$(INCLUDE)}"lber.h"\
	{$(INCLUDE)}"ldap.h"\
	

"$(INTDIR)\testentry.obj" : $(SOURCE) $(DEP_CPP_TESTE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "testplugin - Win32 Debug"

DEP_CPP_TESTE=\
	"..\include\slapi-plugin.h"\
	{$(INCLUDE)}"lber.h"\
	{$(INCLUDE)}"ldap.h"\
	{$(INCLUDE)}"sys\types.h"\
	
NODEP_CPP_TESTE=\
	"..\include\macsock.h"\
	"..\include\os2sock.h"\
	

"$(INTDIR)\testentry.obj" : $(SOURCE) $(DEP_CPP_TESTE) "$(INTDIR)"


!ENDIF 

SOURCE=.\testextendedop.c

!IF  "$(CFG)" == "testplugin - Win32 Release"

DEP_CPP_TESTEX=\
	"..\include\slapi-plugin.h"\
	{$(INCLUDE)}"lber.h"\
	{$(INCLUDE)}"ldap.h"\
	

"$(INTDIR)\testextendedop.obj" : $(SOURCE) $(DEP_CPP_TESTEX) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "testplugin - Win32 Debug"

DEP_CPP_TESTEX=\
	"..\include\slapi-plugin.h"\
	{$(INCLUDE)}"lber.h"\
	{$(INCLUDE)}"ldap.h"\
	{$(INCLUDE)}"sys\types.h"\
	
NODEP_CPP_TESTEX=\
	"..\include\macsock.h"\
	"..\include\os2sock.h"\
	

"$(INTDIR)\testextendedop.obj" : $(SOURCE) $(DEP_CPP_TESTEX) "$(INTDIR)"


!ENDIF 

SOURCE=.\testpostop.c

!IF  "$(CFG)" == "testplugin - Win32 Release"

DEP_CPP_TESTP=\
	"..\include\slapi-plugin.h"\
	{$(INCLUDE)}"lber.h"\
	{$(INCLUDE)}"ldap.h"\
	

"$(INTDIR)\testpostop.obj" : $(SOURCE) $(DEP_CPP_TESTP) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "testplugin - Win32 Debug"

DEP_CPP_TESTP=\
	"..\include\slapi-plugin.h"\
	{$(INCLUDE)}"lber.h"\
	{$(INCLUDE)}"ldap.h"\
	{$(INCLUDE)}"sys\types.h"\
	
NODEP_CPP_TESTP=\
	"..\include\macsock.h"\
	"..\include\os2sock.h"\
	

"$(INTDIR)\testpostop.obj" : $(SOURCE) $(DEP_CPP_TESTP) "$(INTDIR)"


!ENDIF 

SOURCE=.\testpreop.c

!IF  "$(CFG)" == "testplugin - Win32 Release"

DEP_CPP_TESTPR=\
	"..\include\slapi-plugin.h"\
	{$(INCLUDE)}"lber.h"\
	{$(INCLUDE)}"ldap.h"\
	

"$(INTDIR)\testpreop.obj" : $(SOURCE) $(DEP_CPP_TESTPR) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "testplugin - Win32 Debug"

DEP_CPP_TESTPR=\
	"..\include\slapi-plugin.h"\
	{$(INCLUDE)}"lber.h"\
	{$(INCLUDE)}"ldap.h"\
	{$(INCLUDE)}"sys\types.h"\
	
NODEP_CPP_TESTPR=\
	"..\include\macsock.h"\
	"..\include\os2sock.h"\
	

"$(INTDIR)\testpreop.obj" : $(SOURCE) $(DEP_CPP_TESTPR) "$(INTDIR)"


!ENDIF 

SOURCE=.\testsaslbind.c

!IF  "$(CFG)" == "testplugin - Win32 Release"

DEP_CPP_TESTS=\
	"..\include\slapi-plugin.h"\
	{$(INCLUDE)}"lber.h"\
	{$(INCLUDE)}"ldap.h"\
	

"$(INTDIR)\testsaslbind.obj" : $(SOURCE) $(DEP_CPP_TESTS) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "testplugin - Win32 Debug"

DEP_CPP_TESTS=\
	"..\include\slapi-plugin.h"\
	{$(INCLUDE)}"lber.h"\
	{$(INCLUDE)}"ldap.h"\
	{$(INCLUDE)}"sys\types.h"\
	
NODEP_CPP_TESTS=\
	"..\include\macsock.h"\
	"..\include\os2sock.h"\
	

"$(INTDIR)\testsaslbind.obj" : $(SOURCE) $(DEP_CPP_TESTS) "$(INTDIR)"


!ENDIF 


!ENDIF 


#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# Microsoft Developer Studio Generated NMAKE File, Format Version 4.20
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

!IF "$(CFG)" == ""
CFG=Certmap - Win32 Debug
!MESSAGE No configuration specified.  Defaulting to Certmap - Win32 Debug.
!ENDIF 

!IF "$(CFG)" != "Certmap - Win32 Release" && "$(CFG)" !=\
 "Certmap - Win32 Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
!MESSAGE You can specify a configuration when running NMAKE on this makefile
!MESSAGE by defining the macro CFG on the command line.  For example:
!MESSAGE 
!MESSAGE NMAKE /f "Certmap.mak" CFG="Certmap - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "Certmap - Win32 Release" (based on\
 "Win32 (x86) Dynamic-Link Library")
!MESSAGE "Certmap - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 
!ERROR An invalid configuration is specified.
!ENDIF 

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE 
NULL=nul
!ENDIF 
################################################################################
# Begin Project
# PROP Target_Last_Scanned "Certmap - Win32 Debug"
CPP=cl.exe
RSC=rc.exe
MTL=mktyplib.exe

!IF  "$(CFG)" == "Certmap - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Target_Dir ""
OUTDIR=.\Release
INTDIR=.\Release

ALL : "$(OUTDIR)\Certmap.dll"

CLEAN : 
	-@erase "$(INTDIR)\init.obj"
	-@erase "$(INTDIR)\plugin.obj"
	-@erase "$(OUTDIR)\Certmap.dll"
	-@erase "$(OUTDIR)\Certmap.exp"
	-@erase "$(OUTDIR)\Certmap.lib"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /YX /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I "c:\netscape\suitespot\include" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /YX /c
CPP_PROJ=/nologo /MT /W3 /GX /O2 /I "c:\netscape\suitespot\include" /D "WIN32"\
 /D "NDEBUG" /D "_WINDOWS" /Fp"$(INTDIR)/Certmap.pch" /YX /Fo"$(INTDIR)/" /c 
CPP_OBJS=.\Release/
CPP_SBRS=.\.
# ADD BASE MTL /nologo /D "NDEBUG" /win32
# ADD MTL /nologo /D "NDEBUG" /win32
MTL_PROJ=/nologo /D "NDEBUG" /win32 
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
BSC32_FLAGS=/nologo /o"$(OUTDIR)/Certmap.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /dll /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /dll /machine:I386
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib\
 advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib\
 odbccp32.lib /nologo /subsystem:windows /dll /incremental:no\
 /pdb:"$(OUTDIR)/Certmap.pdb" /machine:I386 /out:"$(OUTDIR)/Certmap.dll"\
 /implib:"$(OUTDIR)/Certmap.lib" 
LINK32_OBJS= \
	"$(INTDIR)\init.obj" \
	"$(INTDIR)\plugin.obj" \
	"C:\Netscape\SuiteSpot\lib\nsldap32v10.lib"

"$(OUTDIR)\Certmap.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ELSEIF  "$(CFG)" == "Certmap - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Target_Dir ""
OUTDIR=.\Debug
INTDIR=.\Debug

ALL : "$(OUTDIR)\Certmap.dll"

CLEAN : 
	-@erase "$(INTDIR)\init.obj"
	-@erase "$(INTDIR)\plugin.obj"
	-@erase "$(INTDIR)\vc40.idb"
	-@erase "$(INTDIR)\vc40.pdb"
	-@erase "$(OUTDIR)\Certmap.dll"
	-@erase "$(OUTDIR)\Certmap.exp"
	-@erase "$(OUTDIR)\Certmap.ilk"
	-@erase "$(OUTDIR)\Certmap.lib"
	-@erase "$(OUTDIR)\Certmap.pdb"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /Zi /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /YX /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /Zi /Od /I "c:\netscape\suitespot\include" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /YX /c
CPP_PROJ=/nologo /MTd /W3 /Gm /GX /Zi /Od /I "c:\netscape\suitespot\include" /D\
 "WIN32" /D "_DEBUG" /D "_WINDOWS" /Fp"$(INTDIR)/Certmap.pch" /YX\
 /Fo"$(INTDIR)/" /Fd"$(INTDIR)/" /c 
CPP_OBJS=.\Debug/
CPP_SBRS=.\.
# ADD BASE MTL /nologo /D "_DEBUG" /win32
# ADD MTL /nologo /D "_DEBUG" /win32
MTL_PROJ=/nologo /D "_DEBUG" /win32 
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
BSC32_FLAGS=/nologo /o"$(OUTDIR)/Certmap.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /dll /debug /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /dll /debug /machine:I386
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib\
 advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib\
 odbccp32.lib /nologo /subsystem:windows /dll /incremental:yes\
 /pdb:"$(OUTDIR)/Certmap.pdb" /debug /machine:I386 /out:"$(OUTDIR)/Certmap.dll"\
 /implib:"$(OUTDIR)/Certmap.lib" 
LINK32_OBJS= \
	"$(INTDIR)\init.obj" \
	"$(INTDIR)\plugin.obj" \
	"C:\Netscape\SuiteSpot\lib\nsldap32v10.lib"

"$(OUTDIR)\Certmap.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ENDIF 

.c{$(CPP_OBJS)}.obj:
   $(CPP) $(CPP_PROJ) $<  

.cpp{$(CPP_OBJS)}.obj:
   $(CPP) $(CPP_PROJ) $<  

.cxx{$(CPP_OBJS)}.obj:
   $(CPP) $(CPP_PROJ) $<  

.c{$(CPP_SBRS)}.sbr:
   $(CPP) $(CPP_PROJ) $<  

.cpp{$(CPP_SBRS)}.sbr:
   $(CPP) $(CPP_PROJ) $<  

.cxx{$(CPP_SBRS)}.sbr:
   $(CPP) $(CPP_PROJ) $<  

################################################################################
# Begin Target

# Name "Certmap - Win32 Release"
# Name "Certmap - Win32 Debug"

!IF  "$(CFG)" == "Certmap - Win32 Release"

!ELSEIF  "$(CFG)" == "Certmap - Win32 Debug"

!ENDIF 

################################################################################
# Begin Source File

SOURCE=.\plugin.c
DEP_CPP_PLUGI=\
	".\plugin.h"\
	"c:\netscape\suitespot\include\certmap.h"\
	"c:\netscape\suitespot\include\lber.h"\
	"c:\netscape\suitespot\include\ldap.h"\
	{$(INCLUDE)}"\sys\types.h"\
	

"$(INTDIR)\plugin.obj" : $(SOURCE) $(DEP_CPP_PLUGI) "$(INTDIR)"


# End Source File
################################################################################
# Begin Source File

SOURCE=.\init.c
DEP_CPP_INIT_=\
	".\plugin.h"\
	"c:\netscape\suitespot\include\certmap.h"\
	"c:\netscape\suitespot\include\lber.h"\
	"c:\netscape\suitespot\include\ldap.h"\
	{$(INCLUDE)}"\sys\types.h"\
	

"$(INTDIR)\init.obj" : $(SOURCE) $(DEP_CPP_INIT_) "$(INTDIR)"


# End Source File
################################################################################
# Begin Source File

SOURCE=C:\Netscape\SuiteSpot\lib\nsldap32v10.lib

!IF  "$(CFG)" == "Certmap - Win32 Release"

!ELSEIF  "$(CFG)" == "Certmap - Win32 Debug"

!ENDIF 

# End Source File
# End Target
# End Project
################################################################################

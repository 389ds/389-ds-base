#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# Microsoft Developer Studio Generated NMAKE File, Based on passhook.dsp
!IF "$(CFG)" == ""
CFG=passhook - Win32 Debug
!MESSAGE No configuration specified. Defaulting to passhook - Win32 Debug.
!ENDIF 

!IF "$(CFG)" != "passhook - Win32 Release" && "$(CFG)" != "passhook - Win32 Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "passhook.mak" CFG="passhook - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "passhook - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "passhook - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 
!ERROR An invalid configuration is specified.
!ENDIF 

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE 
NULL=nul
!ENDIF 

!IF  "$(CFG)" == "passhook - Win32 Release"

OUTDIR=.\Release
INTDIR=.\Release
# Begin Custom Macros
OutDir=.\Release
# End Custom Macros

ALL : "$(OUTDIR)\passhook.dll"


CLEAN :
	-@erase "$(INTDIR)\passhand.obj"
	-@erase "$(INTDIR)\passhook.obj"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(OUTDIR)\passhook.dll"
	-@erase "$(OUTDIR)\passhook.exp"
	-@erase "$(OUTDIR)\passhook.lib"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

CPP=cl.exe
CPP_PROJ=/nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "PASSHOOK_EXPORTS" /Fp"$(INTDIR)\passhook.pch" /YX /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /c 

.c{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.c{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

MTL=midl.exe
MTL_PROJ=/nologo /D "NDEBUG" /mktyplib203 /win32 
RSC=rc.exe
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\passhook.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /incremental:no /pdb:"$(OUTDIR)\passhook.pdb" /machine:I386 /def:".\passhook.def" /out:"$(OUTDIR)\passhook.dll" /implib:"$(OUTDIR)\passhook.lib" 
DEF_FILE= \
	".\passhook.def"
LINK32_OBJS= \
	"$(INTDIR)\passhand.obj" \
	"$(INTDIR)\passhook.obj"

"$(OUTDIR)\passhook.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ELSEIF  "$(CFG)" == "passhook - Win32 Debug"

OUTDIR=.\Debug
INTDIR=.\Debug
# Begin Custom Macros
OutDir=.\Debug
# End Custom Macros

ALL : "$(OUTDIR)\passhook.dll"


CLEAN :
	-@erase "$(INTDIR)\passhand.obj"
	-@erase "$(INTDIR)\passhook.obj"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(INTDIR)\vc60.pdb"
	-@erase "$(OUTDIR)\passhook.dll"
	-@erase "$(OUTDIR)\passhook.exp"
	-@erase "$(OUTDIR)\passhook.ilk"
	-@erase "$(OUTDIR)\passhook.lib"
	-@erase "$(OUTDIR)\passhook.pdb"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

CPP=cl.exe
CPP_PROJ=/nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "PASSHOOK_EXPORTS" /Fp"$(INTDIR)\passhook.pch" /YX /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /GZ /c 

.c{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.c{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

MTL=midl.exe
MTL_PROJ=/nologo /D "_DEBUG" /mktyplib203 /win32 
RSC=rc.exe
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\passhook.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
LINK32_FLAGS=nss3.lib libnspr4.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /incremental:yes /pdb:"$(OUTDIR)\passhook.pdb" /debug /machine:I386 /def:".\passhook.def" /out:"$(OUTDIR)\passhook.dll" /implib:"$(OUTDIR)\passhook.lib" /pdbtype:sept 
DEF_FILE= \
	".\passhook.def"
LINK32_OBJS= \
	"$(INTDIR)\passhand.obj" \
	"$(INTDIR)\passhook.obj"

"$(OUTDIR)\passhook.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ENDIF 


!IF "$(NO_EXTERNAL_DEPS)" != "1"
!IF EXISTS("passhook.dep")
!INCLUDE "passhook.dep"
!ELSE 
!MESSAGE Warning: cannot find "passhook.dep"
!ENDIF 
!ENDIF 


!IF "$(CFG)" == "passhook - Win32 Release" || "$(CFG)" == "passhook - Win32 Debug"
SOURCE=..\passhand.cpp

"$(INTDIR)\passhand.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\passhook.cpp

"$(INTDIR)\passhook.obj" : $(SOURCE) "$(INTDIR)"



!ENDIF 


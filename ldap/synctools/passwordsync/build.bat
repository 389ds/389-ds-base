@rem //
@rem // BEGIN COPYRIGHT BLOCK
@rem // This Program is free software; you can redistribute it and/or modify it under
@rem // the terms of the GNU General Public License as published by the Free Software
@rem // Foundation; version 2 of the License.
@rem // 
@rem // This Program is distributed in the hope that it will be useful, but WITHOUT
@rem // ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
@rem // FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
@rem // 
@rem // You should have received a copy of the GNU General Public License along with
@rem // this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
@rem // Place, Suite 330, Boston, MA 02111-1307 USA.
@rem // 
@rem // In addition, as a special exception, Red Hat, Inc. gives You the additional
@rem // right to link the code of this Program with code not covered under the GNU
@rem // General Public License ("Non-GPL Code") and to distribute linked combinations
@rem // including the two, subject to the limitations in this paragraph. Non-GPL Code
@rem // permitted under this exception must only link to the code of this Program
@rem // through those well defined interfaces identified in the file named EXCEPTION
@rem // found in the source code files (the "Approved Interfaces"). The files of
@rem // Non-GPL Code may instantiate templates or use macros or inline functions from
@rem // the Approved Interfaces without causing the resulting work to be covered by
@rem // the GNU General Public License. Only Red Hat, Inc. may make changes or
@rem // additions to the list of Approved Interfaces. You must obey the GNU General
@rem // Public License in all respects for all of the Program code and other code used
@rem // in conjunction with the Program except the Non-GPL Code covered by this
@rem // exception. If you modify this file, you may extend this exception to your
@rem // version of the file, but you are not obligated to do so. If you do not wish to
@rem // provide this exception without modification, you must delete this exception
@rem // statement from your version and license this file solely under the GPL without
@rem // exception. 
@rem // 
@rem // 
@rem // Copyright (C) 2005 Red Hat, Inc.
@rem // All rights reserved.
@rem // END COPYRIGHT BLOCK
@rem //

@echo off

pushd

if [%BUILD_DEBUG%] == [optimize] (
    set LIBROOT=..\..\..\..\dist\WINNT5.0_OPT.OBJ
) else (
    set LIBROOT=..\..\..\..\dist\WINNT5.0_DBG.OBJ
)

echo %LIBROOT%

set INCLUDE=%INCLUDE%;%CD%\%LIBROOT%\ldapsdk\include;%CD%\%LIBROOT%\nspr\include;%CD%\%LIBROOT%\nss\include
set LIB=%LIB%;%CD%\%LIBROOT%\ldapsdk\lib;%CD%\%LIBROOT%\nspr\lib;%CD%\%LIBROOT%\nss\lib
set PATH=%PATH%;%CD%\%LIBROOT%\wix

set OK=0

cd passsync

:BUILD
nmake passsync.mak
set /a OK=%OK% + %ERRORLEVEL%

copy /Y Debug\passsync.exe ..\Wix
set /a OK=%OK% + %ERRORLEVEL%

cd ..\passhook

nmake passhook.mak
set /a OK=%OK% + %ERRORLEVEL%

copy /Y Debug\passhook.dll ..\Wix
set /a OK=%OK% + %ERRORLEVEL%

:PKG
if NOT EXIST ..\Wix (
    echo ERROR: Cannot find Wix folder.
    set OK=1 
    goto :END )

cd ..\Wix

if EXIST ..\%LIBROOT%\ldapsdk\lib\nsldap32v50.dll (
    copy /Y ..\%LIBROOT%\ldapsdk\lib\nsldap32v50.dll
)
if EXIST ..\%LIBROOT%\ldapsdk\lib\nsldapssl32v50.dll (
    copy /Y ..\%LIBROOT%\ldapsdk\lib\nsldapssl32v50.dll
)
if EXIST ..\%LIBROOT%\ldapsdk\lib\nsldappr32v50.dll (
    copy /Y ..\%LIBROOT%\ldapsdk\lib\nsldappr32v50.dll
)
if EXIST ..\%LIBROOT%\nspr\lib\libnspr4.dll (
    copy /Y ..\%LIBROOT%\nspr\lib\libnspr4.dll
)
if EXIST ..\%LIBROOT%\nspr\lib\libplds4.dll (
    copy /Y ..\%LIBROOT%\nspr\lib\libplds4.dll
)
if EXIST ..\%LIBROOT%\nspr\lib\libplc4.dll (
    copy /Y ..\%LIBROOT%\nspr\lib\libplc4.dll
)
if EXIST ..\%LIBROOT%\nss\lib\nss3.dll (
    copy /Y ..\%LIBROOT%\nss\lib\nss3.dll
)
if EXIST ..\%LIBROOT%\nss\lib\ssl3.dll (
    copy /Y ..\%LIBROOT%\nss\lib\ssl3.dll
)
if EXIST ..\%LIBROOT%\nss\lib\softokn3.dll (
    copy /Y ..\%LIBROOT%\nss\lib\softokn3.dll
)

candle PassSync.wxs
set /a OK=%OK% + %ERRORLEVEL%

light PassSync.wixobj
set /a OK=%OK% + %ERRORLEVEL%

if NOT [%BUILD_DEBUG%] == [] (
    if EXIST PassSync.msi (move /Y PassSync.msi PassSync-%BUILD_DEBUG%.msi)
)

:END
popd
if %OK% GTR 1 (set OK=1)
exit %OK%

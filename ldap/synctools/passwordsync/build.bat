@rem //
@rem // BEGIN COPYRIGHT BLOCK
@rem // Copyright (C) 2005 Red Hat, Inc.
@rem // All rights reserved.
@rem // END COPYRIGHT BLOCK
@rem //

@echo off

pushd

if NOT [%BUILD_DEBUG%] == [] (
    if [%BUILD_DEBUG%] == [optimize] (
        set LIBROOT=..\..\..\..\dist\WINNT5.0_OPT.OBJ
    ) else (
        set LIBROOT=..\..\..\..\dist\WINNT5.0_DBG.OBJ
    )
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

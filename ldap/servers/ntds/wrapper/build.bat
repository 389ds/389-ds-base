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
        set LIBROOT=..\..\..\..\..\dist\WINNT5.0_OPT.OBJ
    ) else (
        set LIBROOT=..\..\..\..\..\dist\WINNT5.0_DBG.OBJ
    )
)

set PATH=%PATH%;%CD%\%LIBROOT%\wix

set OK=0
cd wix

candle ntds.wxs
set /a OK=%OK% + %ERRORLEVEL%

light ntds.wixobj
set /a OK=%OK% + %ERRORLEVEL%

if NOT [%BUILD_DEBUG%] == [] (
    if EXIST ntds.msi (move /Y ntds.msi ntds-%BUILD_DEBUG%.msi)
)

:END
popd
if %OK% GTR 1 (set OK=1)
exit %OK%

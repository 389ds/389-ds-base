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

set WXSLOC=%CD%\wix
echo %WXSLOC%

cd %OBJDEST%

set OK=0

candle %WXSLOC%\ntds.wxs
set /a OK=%OK% + %ERRORLEVEL%

light ntds.wixobj
set /a OK=%OK% + %ERRORLEVEL%

:END
popd
if %OK% GTR 1 (set OK=1)
exit %OK%

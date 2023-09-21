#!/bin/bash

LANG_CODE="en"

find src/ ! -name 'test-*' -name '*.js' -o -name '*.jsx' | xargs xgettext --default-domain=cockpit --output=${LANG_CODE}.po --language=C --keyword= --keyword=_:1,1t --keyword=_:1c,2,2t --keyword=C_:1c,2 --keyword=N_ --keyword=NC_:1c,2 --keyword=gettext:1,1t --keyword=gettext:1c,2,2t --keyword=ngettext:1,2,3t --keyword=ngettext:1c,2,3,4t --keyword=gettextCatalog.getString:1,3c --keyword=gettextCatalog.getPlural:2,3,4c --from-code=UTF-8

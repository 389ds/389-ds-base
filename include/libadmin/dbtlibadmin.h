/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#define LIBRARY_NAME "libadmin"

static char dbtlibadminid[] = "$DBT: libadmin referenced v1 $";

#include "i18n.h"

BEGIN_STR(libadmin)
	ResDef( DBT_LibraryID_, -1, dbtlibadminid )/* extracted from dbtlibadmin.h*/
	ResDef( DBT_help_, 1, "  Help  " )/*extracted from template.c*/
	ResDef( DBT_ok_, 2, "   OK   " )/*extracted from template.c*/
	ResDef( DBT_reset_, 3, " Reset " )/*extracted from template.c*/
	ResDef( DBT_done_, 4, "  Done  " )/*extracted from template.c*/
	ResDef( DBT_cancel_, 5, " Cancel " )/*extracted from template.c*/
END_STR(libadmin)

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/***********************************************************************
**
**
** NAME
**  configure_instance.h
**
** DESCRIPTION
**
**
** AUTHOR
**  Rich Megginson <richm@netscape.com>
**
***********************************************************************/

#ifndef _CONFIGURE_INSTANCE_H_
#define _CONFIGURE_INSTANCE_H_

#include "create_instance.h"

#ifdef __cplusplus
extern "C" {
#endif

int
create_config_from_inf(
	server_config_s *cf,
	int argc,
	char *argv[]
);

int
configure_instance_with_config(
	server_config_s *cf,
	int verbose, /* if false, silent; if true, verbose */
	const char *lfile /* log file */
);

int
configure_instance();

int
reconfigure_instance(int argc, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif /* _CONFIGURE_INSTANCE_H_ */

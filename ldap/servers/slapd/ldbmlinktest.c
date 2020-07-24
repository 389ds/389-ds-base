/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

main()
{
    ldbm_back_bind();
    ldbm_back_unbind();
    ldbm_back_search();
    ldbm_back_compare();
    ldbm_back_modify();
    ldbm_back_modrdn();
    ldbm_back_add();
    ldbm_back_delete();
    ldbm_back_abandon();
    ldbm_back_config();
    ldbm_back_init();
    ldbm_back_close();
}

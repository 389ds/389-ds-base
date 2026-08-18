/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "../../test_slapd.h"

/* To access filter optimise */
#include <slapi-private.h>

void
test_libslapd_filter_optimise(void **state __attribute__((unused)))
{
    char *test_filters[] = {
        "(&(uid=uid1)(sn=last1)(givenname=first1))",
        "(&(uid=uid1)(&(sn=last1)(givenname=first1)))",
        "(&(uid=uid1)(&(&(sn=last1))(&(givenname=first1))))",
        "(&(uid=*)(sn=last3)(givenname=*))",
        "(&(uid=*)(&(sn=last3)(givenname=*)))",
        "(&(uid=uid5)(&(&(sn=*))(&(givenname=*))))",
        "(&(objectclass=*)(uid=*)(sn=last*))",
        "(&(objectclass=*)(uid=*)(sn=last1))",

        "(|(uid=uid1)(sn=last1)(givenname=first1))",
        "(|(uid=uid1)(|(sn=last1)(givenname=first1)))",
        "(|(uid=uid1)(|(|(sn=last1))(|(givenname=first1))))",
        "(|(objectclass=*)(sn=last1)(|(givenname=first1)))",
        "(|(&(objectclass=*)(sn=last1))(|(givenname=first1)))",
        "(|(&(objectclass=*)(sn=last))(|(givenname=first1)))",

        "(&(uid=uid1)(!(cn=NULL)))",
        "(&(!(cn=NULL))(uid=uid1))",
        "(&(uid=*)(&(!(uid=1))(!(givenname=first1))))",

        "(&(|(uid=uid1)(uid=NULL))(sn=last1))",
        "(&(|(uid=uid1)(uid=NULL))(!(sn=NULL)))",
        "(&(|(uid=uid1)(sn=last2))(givenname=first1))",
        "(|(&(uid=uid1)(!(uid=NULL)))(sn=last2))",
        "(|(&(uid=uid1)(uid=NULL))(sn=last2))",
        "(&(uid=uid5)(sn=*)(cn=*)(givenname=*)(uid=u*)(sn=la*)(cn=full*)(givenname=f*)(uid>=u)(!(givenname=NULL)))",
        "(|(&(objectclass=*)(sn=last))(&(givenname=first1)))",

        "(&(uid=uid1)(sn=last1)(givenname=NULL))",
        "(&(uid=uid1)(&(sn=last1)(givenname=NULL)))",
        "(&(uid=uid1)(&(&(sn=last1))(&(givenname=NULL))))",
        "(&(uid=uid1)(&(&(sn=last1))(&(givenname=NULL)(sn=*)))(|(sn=NULL)))",
        "(&(uid=uid1)(&(&(sn=last*))(&(givenname=first*)))(&(sn=NULL)))",

        "(|(uid=NULL)(sn=NULL)(givenname=NULL))",
        "(|(uid=NULL)(|(sn=NULL)(givenname=NULL)))",
        "(|(uid=NULL)(|(|(sn=NULL))(|(givenname=NULL))))",

        "(uid>=uid3)",
        "(&(uid=*)(uid>=uid3))",
        "(|(uid>=uid3)(uid<=uid5))",
        "(&(uid>=uid3)(uid<=uid5))",
        "(|(&(uid>=uid3)(uid<=uid5))(uid=*))",

        "(|(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)"
        "(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)"
        "(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)"
        "(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)"
        "(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)"
        "(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)"
        "(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)"
        "(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)"
        "(uid=*))",
        NULL
    };

    for (size_t i = 0; test_filters[i] != NULL; i++) {
        char *filter_str = slapi_ch_strdup(test_filters[i]);

        struct slapi_filter *filter = slapi_str2filter(filter_str);
        slapi_filter_optimise(filter);
        slapi_filter_free(filter, 1);
        slapi_ch_free_string(&filter_str);
    }
}


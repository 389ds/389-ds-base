/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2015  Red Hat
 * see files 'COPYING' and 'COPYING.openssl' for use and warranty
 * information
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * Additional permission under GPLv3 section 7:
 * 
 * If you modify this Program, or any covered work, by linking or
 * combining it with OpenSSL, or a modified version of OpenSSL licensed
 * under the OpenSSL license
 * (https://www.openssl.org/source/license.html), the licensors of this
 * Program grant you additional permission to convey the resulting
 * work. Corresponding Source for a non-source form of such a
 * combination shall include the source code for the parts that are
 * licensed under the OpenSSL license as well as that of the covered
 * work.
 * --- END COPYRIGHT BLOCK ---
 */
#ifndef NS_TLS_H
#define NS_TLS_H

#include "nspr.h"
#include "prerror.h"
#include "prio.h"

/* should be in sec_ctx.h */
struct ns_sec_ctx_t;
/* adds the sec layer _in place_ to fd - if error returned, the sec
   layer is unstable and the fd should be closed and discarded */
PRErrorCode ns_add_sec_layer(PRFileDesc *fd, struct ns_sec_ctx_t *ctx);

/* tls specific functions */
void ns_tls_done(struct ns_sec_ctx_t *ctx);
struct ns_sec_ctx_t *ns_tls_init(const char *dir, const char *prefix, const char *certname, PRBool isClient);

#endif /* NS_TLS_H */

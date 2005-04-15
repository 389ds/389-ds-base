/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* fileio.c - layer to adjust EOL to use DOS format via PR_Read/Write on NT */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#ifndef _WIN32
#include <sys/param.h>
#include <unistd.h>
#include <pwd.h>
#endif
#include "slap.h"
#include "pw.h"
#include <prio.h>

#if defined( XP_WIN32 )

#include <prinit.h> /* PR_CallOnce */
#include <string.h> /* memmove, memcpy */

#define g_EOF (-1)

static PRInt32 PR_CALLBACK readText(PRFileDesc *f, void *buf, PRInt32 amount)
{
	auto PRInt32 size = *(signed char*)&(f->secret);
	auto char* readAhead = ((char*)&(f->secret)) + 1;
	if ( size == g_EOF ) {
		f->secret = NULL;
		return 0;
	}
	if ( size > amount ) {
		return 0;
	}
	if ( size > 0 ) {
		memcpy( buf, readAhead, size );
	}
	f->secret = NULL;
	while (1) {
		auto PRInt32 len = amount - size;
		auto char* head;
		auto PRInt32 rval;
		if (len > 0) {
			head = (char*)buf + size;
		} else if (size > 0 && '\r' == ((char*)buf)[size-1]) {
			head = readAhead;
			len = 1;
		} else {
			break;		
		}
		rval = PR_Read( f->lower, head, len );
		if ( rval < 0 ) { /* error */
			return rval;
		}
		if ( rval == 0 ) { /* EOF */
			if ( size ) {
				*(signed char*)&(f->secret) = g_EOF;
			}
			return size;
		}
		if (head == readAhead) {
			if ( '\n' == *readAhead ) {
				((char*)buf)[size-1] = '\n';
			} else {
				*(signed char*)&(f->secret) = rval;
			}
			break;
		} else {
			auto char* tail = head + rval;
			auto char* dest = NULL;
			auto char* p;
			for ( p = head; p < tail; p++ ) {
				if ( *p == '\n' && p > (char*)buf && *(p - 1) == '\r' )
				{
					if ( dest == NULL ) {	/* first CRLF */
						dest = p - 1;
					} else {
						auto size_t len = (p - 1) - head;
						memmove( dest, head, len );
						dest += len;
					}
					head = p;  /* '\n' */
					--rval;	/* ignore '\r' */
				}
			}
			if ( dest != NULL ) {
				auto size_t len = tail - head;
				memmove( dest, head, len );
			}
			size += rval;
		}
	}
	return size;
}

static PRInt32 PR_CALLBACK seekText(PRFileDesc *f, PRInt32 offset, PRSeekWhence how)
{
	f->secret = NULL;
	return PR_Seek(f->lower, offset, how);
}

static PRInt64 PR_CALLBACK seek64Text(PRFileDesc *f, PRInt64 offset, PRSeekWhence how)
{
	f->secret = NULL;
	return PR_Seek64(f->lower, offset, how);
}

static PRInt32 PR_CALLBACK writeText(PRFileDesc *f, const void *buf, PRInt32 amount)
{
	/* note: buf might not be null-terminated */
	auto PRInt32 size = 0;
	auto char* head = (char*)buf;
	auto char* tail = head + amount;
	auto char* p;
	for ( p = head; p <= tail; ++p ) {
		if ( p == tail || *p == '\n' ) {
			auto PRInt32 len = p - head;
			auto PRInt32 rval;
			if ( len > 0 ) {
				rval = PR_Write( f->lower, head, len );
				if ( rval < 0 ) {
					return rval;
				}
				size += rval;
				if ( rval < len ) {
					break;
				}
			}
			if ( p == tail ) {
				break;
			}
			rval = PR_Write( f->lower, "\r", 1 );
			if ( rval < 0 ) {
				return rval;
			}
			if ( rval < 1 ) {
				break;
			}
			head = p;
		}
	}
	return size;
}

static PRInt32 PR_CALLBACK writevText(PRFileDesc *fd, const PRIOVec *iov, PRInt32 size, PRIntervalTime timeout)
{
    auto PRInt32 i;
    auto size_t total = 0;
    for (i = 0; i < size; ++i) {
		register PRInt32 rval = PR_Write(fd, iov[i].iov_base, iov[i].iov_len);
		if (rval < 0) return rval;
		total += rval;
		if (rval < iov[i].iov_len) break;
    }
    return total;
}

/* ONREPL - this is bad because it allows only one thread to use this functionality.
   Noriko said she would fix this before 5.0 ships.
 */

static const char* const g_LayerName = "MdsTextIO";
static PRDescIdentity    g_LayerID;
static PRIOMethods       g_IoMethods;

static PRStatus PR_CALLBACK closeLayer(PRFileDesc* stack)
{
	auto PRFileDesc* layer = PR_PopIOLayer(stack, g_LayerID);
	if (!layer)
		return PR_FAILURE;
	if (layer->dtor) {
		layer->secret = NULL;
		layer->dtor(layer);
	}
	return PR_Close(stack);
}

static PRStatus PR_CALLBACK initialize(void)
{
	g_LayerID = PR_GetUniqueIdentity(g_LayerName);
	if (PR_INVALID_IO_LAYER == g_LayerID) {
		return PR_FAILURE;
	} else {
		auto const PRIOMethods* defaults = PR_GetDefaultIOMethods();
		if (!defaults) {
			return PR_FAILURE;
		} else {
			memcpy (&g_IoMethods, defaults, sizeof(g_IoMethods));
		}
	}
	/* Customize methods: */
	g_IoMethods.read = readText;
	g_IoMethods.seek = seekText;
	g_IoMethods.seek64 = seek64Text;
	g_IoMethods.write = writeText;
	g_IoMethods.writev = writevText; /* ??? Is this necessary? */
	g_IoMethods.close = closeLayer; /* ??? Is this necessary? */
	return PR_SUCCESS;
}

static PRCallOnceType g_callOnce = {0,0};

/* Push a layer that converts from "\n" to the local filesystem's
 * end-of-line sequence on output, and vice-versa on input.
 * The layer pops itself (if necessary) when the file is closed.
 *
 * This layer does not affect the behavior of PR_Seek or PR_Seek64;
 * their parameters still measure bytes in the lower-level file,
 * and consequently will not add up with the results of PR_Read
 * or PR_Write.  For example, if you add up PR_Read return values,
 * and seek backward in the file that many bytes, the cursor will
 * *not* be restored to its original position (unless the data you
 * read didn't require conversion; that is, they didn't contain
 * any newlines, or you're running on Unix).
 * 
 * Likewise, the results of PR_Read or PR_Write won't add up to
 * the 'size' field in the result of PRFileInfo or PRFileInfo64.
 */
static PRStatus pushTextIOLayer(PRFileDesc* stack)
{
	auto PRStatus rv = PR_CallOnce(&g_callOnce, initialize);
	if (PR_SUCCESS == rv) {
		auto PRFileDesc* layer = PR_CreateIOLayerStub(g_LayerID, &g_IoMethods);
		layer->secret = NULL;
		rv = PR_PushIOLayer(stack, PR_TOP_IO_LAYER, layer);
	}
	return rv;
}

static PRFileDesc *popTextIOLayer(PRFileDesc* stack)
{
	PRFileDesc *layer;
	layer = PR_PopIOLayer(stack, g_LayerID);
	if (layer && layer->dtor) {
		layer->secret = NULL;
		layer->dtor(layer);
	}
	return layer;
}

#endif /* XP_WIN32 */

PRInt32
slapi_read_buffer( PRFileDesc *fd, void *buf, PRInt32 amount )
{
	PRInt32 rval = 0;
#if defined( XP_WIN32 )
	PRStatus rv;

	rv = pushTextIOLayer( fd );
	if ( PR_SUCCESS != rv ) {
		return -1;
	}
#endif

	rval = PR_Read( fd, buf, amount );

#if defined( XP_WIN32 )
	popTextIOLayer( fd );
#endif

    return rval;
}

/*
 * slapi_write_buffer -- same as PR_Write
 *                       except '\r' is added before '\n'.
 * Return value: written bytes not including '\r' characters.
 */
PRInt32
slapi_write_buffer( PRFileDesc *fd, void *buf, PRInt32 amount )
{
	PRInt32 rval = 0;
#if defined( XP_WIN32 )
	PRStatus rv;

	rv = pushTextIOLayer( fd );
	if ( PR_SUCCESS != rv ) {
		return -1;
	}
#endif

    rval = PR_Write( fd, buf, amount );

#if defined( XP_WIN32 )
	popTextIOLayer( fd );
#endif

    return rval;
}

/*
 * This function renames a file to a new name.  Unlike PR_Rename or NT rename, this
 * function can be used if the destfilename exists, and it will overwrite the dest
 * file name
 */
int
slapi_destructive_rename(const char *srcfilename, const char *destfilename)
{
	int rv = 0;
#if defined( XP_WIN32 )
	if (!MoveFileEx(srcfilename, destfilename, MOVEFILE_REPLACE_EXISTING)) {
		rv = GetLastError();
	}
#else
	if ( rename(srcfilename, destfilename) < 0 ) {
		rv = errno;
	}
#endif
	return rv;
}

/*
 * This function copies the source into the dest
 */
int
slapi_copy(const char *srcfilename, const char *destfilename)
{
	int rv = 0;
#if defined( XP_WIN32 )
	if (!CopyFile(srcfilename, destfilename, FALSE)) {
		rv = GetLastError();
	}
#else
	unlink(destfilename);
	if ( link(srcfilename, destfilename) < 0 ) {
		rv = errno;
	}
#endif
	return rv;
}

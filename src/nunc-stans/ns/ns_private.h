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
/*! \file ns_private.h
    \brief Nunc Stans private API

    This is the private API for Nunc Stans
*/
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "nspr.h"
#include "prmon.h"
#include <nunc-stans.h>

#include <inttypes.h>

/**
 * Forward declaration of the thread struct - internal
 *
 * The actual struct is opaque to applications.  The forward declaration is here
 * for the typedef.
 */
struct ns_thread_t;
/**
 * This is the thread type - internal
 *
 * The actual thread type is opaque to applications.
 */
typedef struct ns_thread_t ns_thread_t;

/**
 * Log an error to logging system. - internal
 * \param priority The log level
 * \param fmt The log message string format
 */
void ns_log(int priority, const char *fmt, ...);

/**
 * Log an error to logging system. - internal
 * \param priority The log level
 * \param fmt The log message string format
 * \param varg The va_list arg
 */
void ns_log_valist(int priority, const char *fmt, va_list varg);

/**
 * \param size - Memory allocation size - internal
 * \return - allocated memory
 */
void *ns_malloc(size_t size);

/**
 * \param size - Memory allocation size - internal
 * \param alignment - The allignment of the memory. Must be a power of two.
 * \return - allocated memory
 */
void *ns_memalign(size_t size, size_t alignment);

/**
 * \param count - number of items - internal
 * \param size - Memory allocation size
 * \return - allocated memory
 */
void *ns_calloc(size_t count, size_t size);

/**
 * \param ptr - pointer to the memory block - internal
 * \param size - size of allocation
 * \return - allocated memory
 */
void *ns_realloc(void *ptr, size_t size);

/**
 * \param ptr - pointer to memory to be freed - internal
 */
void ns_free(void *ptr);

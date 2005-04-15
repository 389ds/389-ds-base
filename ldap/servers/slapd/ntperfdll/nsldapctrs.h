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
/*

      nsctrs.h

 */

#ifndef _NSCTRS_H_
#define _NSCTRS_H_

#pragma pack (4)

#define NS_NUM_PERF_OBJECT_TYPES 1
#define NUM_CONN_RATE_OFFSET	    		sizeof(DWORD)
#define NUM_THROUGHPUT_OFFSET	    		NUM_CONN_RATE_OFFSET + sizeof(DWORD)
#define NUM_TOTAL_BYTES_WRITTEN_OFFSET		NUM_THROUGHPUT_OFFSET + sizeof(DWORD)
#define NUM_TOTAL_BYTES_READ_OFFSET		NUM_TOTAL_BYTES_WRITTEN_OFFSET + sizeof(DWORD)
#define NUM_OP_RATE_OFFSET			NUM_TOTAL_BYTES_READ_OFFSET + sizeof(DWORD)
#define NUM_TOTAL_ERRORS_OFFSET			NUM_OP_RATE_OFFSET + sizeof(DWORD)
#define NUM_SEARCH_RATE_OFFSET			NUM_TOTAL_ERRORS_OFFSET + sizeof(DWORD)
#define ADD_RATE_OFFSET				NUM_SEARCH_RATE_OFFSET + sizeof(DWORD)
#define DELETE_RATE_OFFSET			ADD_RATE_OFFSET + sizeof(DWORD)
#define MODIFY_RATE_OFFSET			DELETE_RATE_OFFSET + sizeof(DWORD)
#define COMPARE_RATE_OFFSET			MODIFY_RATE_OFFSET + sizeof(DWORD)
#define MODDN_RATE_OFFSET			COMPARE_RATE_OFFSET + sizeof(DWORD)
#define CONNECTIONS_OFFSET			MODDN_RATE_OFFSET + sizeof(DWORD)
#define BIND_RATE_OFFSET			CONNECTIONS_OFFSET + sizeof(DWORD)
#define ENTRIES_RETURNED_OFFSET		BIND_RATE_OFFSET + sizeof(DWORD)
#define ENTRIES_RETURNED_RATE_OFFSET		ENTRIES_RETURNED_OFFSET + sizeof(DWORD)
#define REFERRALS_RETURNED_OFFSET		ENTRIES_RETURNED_RATE_OFFSET + sizeof(DWORD)
#define REFERRALS_RETURNED_RATE_OFFSET		REFERRALS_RETURNED_OFFSET + sizeof(DWORD)
#define BYTES_READ_RATE_OFFSET		REFERRALS_RETURNED_RATE_OFFSET + sizeof(DWORD)
#define BYTES_WRITTEN_RATE_OFFSET	BYTES_READ_RATE_OFFSET + sizeof(DWORD)
#define SIZE_OF_NS_PERFORMANCE_DATA     	BYTES_WRITTEN_RATE_OFFSET + sizeof(DWORD)

typedef struct _NS_DATA_DEFINITION {
    	PERF_OBJECT_TYPE	NS_ObjectType;
    	PERF_COUNTER_DEFINITION	connection_rate;
    	PERF_COUNTER_DEFINITION	throughput;
	PERF_COUNTER_DEFINITION total_bytes_written;
	PERF_COUNTER_DEFINITION total_bytes_read;
	PERF_COUNTER_DEFINITION	operation_rate;
	PERF_COUNTER_DEFINITION total_errors;
	PERF_COUNTER_DEFINITION search_rate;
	PERF_COUNTER_DEFINITION add_rate;
	PERF_COUNTER_DEFINITION delete_rate;
	PERF_COUNTER_DEFINITION modify_rate;
	PERF_COUNTER_DEFINITION compare_rate;
	PERF_COUNTER_DEFINITION moddn_rate;
	PERF_COUNTER_DEFINITION connections;
	PERF_COUNTER_DEFINITION bind_rate;
	PERF_COUNTER_DEFINITION entries_returned;
	PERF_COUNTER_DEFINITION entries_returned_rate;
	PERF_COUNTER_DEFINITION referrals_returned;
	PERF_COUNTER_DEFINITION referrals_returned_rate;
	PERF_COUNTER_DEFINITION bytes_read_rate;
	PERF_COUNTER_DEFINITION bytes_written_rate;
} NS_DATA_DEFINITION;

#pragma pack ()

#endif /* _NSCTRS_H_ */
  

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
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* uuid.c */

/*
** Copyright (c) 1990- 1993, 1996 Open Software Foundation, Inc.
** Copyright (c) 1989 by Hewlett-Packard Company, Palo Alto, Ca. &
** Digital Equipment Corporation, Maynard, Mass.
** Copyright (c) 1998 Microsoft.
** To anyone who acknowledges that this file is provided "AS IS"
** without any express or implied warranty: permission to use, copy,
** modify, and distribute this file for any purpose is hereby
** granted without fee, provided that the above copyright notices and
** this notice appears in all source code copies, and that none of
** the names of Open Software Foundation, Inc., Hewlett-Packard
** Company, or Digital Equipment Corporation be used in advertising
** or publicity pertaining to distribution of the software without
** specific, written prior permission.  Neither Open Software
** Foundation, Inc., Hewlett-Packard Company, Microsoft, nor Digital Equipment
* Corporation makes any representations about the suitability of
** this software for any purpose.
*/


#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pk11func.h>

#ifdef _WIN32
#include <sys/stat.h> /* for S_IREAD and S_IWRITE */
#else
#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>     /* gethostname() */
#endif


#include "slap.h"
#include "uuid.h"
#include "sechash.h"

#define SEQ_PER_SEC			10000000	/* number of 100ns intervals in a sec */
#define STATE_FILE			"state"		/* file that contains generator's state */
#define STATE_ATTR			"nsState"	/* attribute that stores state info */
#define MODULE				"uuid"		/* for logging */
#define UPDATE_INTERVAL		60000		/* 1 minute */
#define NEED_TIME_UPDATE    -1
#define SEED_LENGTH			16

/* generates uuid in singlethreaded environment */
static int uuid_create_st(guid_t *uuid);
/* generates uuid in multithreaded environment */
static int uuid_create_mt(guid_t *uuid);
/* periodically called to update generator's state - mt case only */
static void uuid_update_state (time_t when, void *arg);
/* creates uuid in v1 format using current state info */
static void format_uuid_v1(guid_t *uuid, uuid_time_t timestamp, unsigned16 clock_seq);
/* generates uuid in v3 format */
static void format_uuid_v3(guid_t *uuid, unsigned char hash[16]);  
/* reads state from a file or DIT entry */
static int read_state (const char *configDir, const Slapi_DN *configDN, PRBool *newState);
/* reads state from a file */ 
static int read_state_from_file (const char *configDir);
/* read state information from DIT */
static int read_state_from_entry (const Slapi_DN *configDN);
/* writes state to persistent store: file or dit */
static int write_state(PRBool newState);
/* writes state to a file */
static int write_state_to_file();
/* writes state to dit */
static int write_state_to_entry(PRBool newState);
/* add state entry to the dit */
static int add_state_entry ();
/* modify state entry in the dit */
static int modify_state_entry ();
/* generates timestamp for the next uuid - single threaded */
static uuid_time_t update_time();
/* generates timestamp for the next uuid - multithreaded threaded */  
static int update_time_mt(uuid_time_t *timestamp, unsigned16 *clock_seq);
/* retrieves or generates nodeid */
static int get_node_identifier(uuid_node_t *node);
/* returns current time in the UTC format */
static void get_system_time(uuid_time_t *uuid_time);
/* generates random value - used to set clock sequence */
static unsigned16 true_random(void);
/* generate random info buffer to generate nodeid */
static void get_random_info(unsigned char seed[], size_t arraylen);

/* UUID generator state stored persistently */
typedef struct 
{
	uuid_time_t timestamp;	/* saved timestamp                       */
	uuid_node_t node;		/* saved node ID                         */
	unsigned16  clockseq;	/* saved clock sequence                  */
	unsigned8	last_update;/* flags the update made during server sutdown */
} uuid_gen_state;

/* generator state plus data to support it */
typedef struct 
{
    uuid_gen_state genstate;	/* generator state						*/
    int         time_seq;		/* sequence number to account for clock 
                                   granularity; not written to disk     */
	PRBool		initialized;	/* uniqueid successfully initialized    */
	PRBool		mtGen;			/* multithreaded generation 			*/
    PRLock      *lock;			/* lock to protect state				*/
    PRFileDesc  *fd;			/* fd for the state file				*/
	Slapi_DN    *configDN;		/* db entry that contains state info	*/
} uuid_state;

static unsigned int uuid_seed = 0;		/* seed for the random generator */

  uuid_state _state; /* generator's state */

/* uuid_init -- initializes uuid layer */
int uuid_init (const char *configDir, const Slapi_DN *configDN, PRBool mtGen)
{
	int rt;
	PRBool newState = PR_FALSE;

	if (_state.initialized)
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, 
						 "uuid_init: generator is already initialized\n");
		return UUID_SUCCESS;
	}

	memset (&_state, 0, sizeof (_state));

    /* get saved state */
    rt = read_state(configDir, configDN, &newState);
	if (rt != UUID_SUCCESS)
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, 
						 "uuid_init: failed to get generator's state\n");
		uuid_cleanup ();
		return rt;
	}

	_state.mtGen = mtGen;

	/* this is multithreaded generation - create lock */
	if (_state.mtGen)
	{
		_state.lock = PR_NewLock();
		if (!_state.lock) 
		{
			PRErrorCode prerr = PR_GetError();
			slapi_log_error (SLAPI_LOG_FATAL, MODULE, "uuid_init: "
						 "failed to create state lock; " SLAPI_COMPONENT_NAME_NSPR " error %d (%s).\n",
						 prerr, slapd_pr_strerror(prerr));
			uuid_cleanup ();
			return UUID_LOCK_ERROR;
		}
    }

    /* save the state */
    rt = write_state(newState);
    /* can't proceede if the state can't be written */
	if (rt != UUID_SUCCESS)
	{
		if (slapi_config_get_readonly() &&
			(rt == UUID_LDAP_ERROR)) {
			/*
			 * If server is readonly and error is UUID_LDAP_ERROR
			 * we can continue. 
			 */
			slapi_log_error (SLAPI_LOG_FATAL, MODULE, "Warning: "
							 "The server is in read-only mode, therefore the unique ID generator cannot be used. "
							 "Do not use this server in any replication agreement\n");
		}
		else {
			slapi_log_error (SLAPI_LOG_FATAL, MODULE, "uuid_init: "
						 "failed to save generator's state.\n");
			uuid_cleanup ();
			return rt;
		}
    }

	/* schedule update task for multithreaded generation */
	if (_state.mtGen)
		slapi_eq_repeat(uuid_update_state, NULL, (time_t)0, UPDATE_INTERVAL);

	_state.initialized = PR_TRUE;
    return UUID_SUCCESS;
}

/* uuid_cleanup -- saves state, destroys generator data */
void uuid_cleanup ()
{	
	if (_state.initialized)
	{
		_state.genstate.last_update = 1;
		write_state (PR_FALSE);
	}
	
    if (_state.lock)
        PR_DestroyLock (_state.lock);

    if (_state.fd)
        PR_Close (_state.fd);
	
	if (_state.configDN)
		slapi_sdn_free(&_state.configDN);

	memset (&_state, 0, sizeof (_state));
}

/* uuid_create - main generating function */
int uuid_create(guid_t *uuid) 
{
	if (_state.mtGen)
		return uuid_create_mt(uuid);
	else
		return uuid_create_st(uuid);	
}

/* uuid_compare -- Compare two UUID's "lexically" and return
				   -1   u1 is lexically before u2
					0   u1 is equal to u2
					1   u1 is lexically after u2

  Note:   lexical ordering is not temporal ordering!
*/
#define CHECK(f1, f2) if (f1 != f2) return f1 < f2 ? -1 : 1;
int uuid_compare(const guid_t *u1, const guid_t *u2)
{
	int i;

	CHECK(u1->time_low, u2->time_low);
	CHECK(u1->time_mid, u2->time_mid);
	CHECK(u1->time_hi_and_version, u2->time_hi_and_version);
	CHECK(u1->clock_seq_hi_and_reserved, u2->clock_seq_hi_and_reserved);
	CHECK(u1->clock_seq_low, u2->clock_seq_low)
	for (i = 0; i < 6; i++) 
	{
		if (u1->node[i] < u2->node[i])
			return -1;
		if (u1->node[i] > u2->node[i])
		return 1;
	}
	return 0;
}

/* uuid_format -- converts UUID to its string representation 
                  buffer should be at list 40 bytes long
*/

void uuid_format(const guid_t *u, char *buff) 
{
    sprintf(buff, "%8.8x-%4.4x-%4.4x-%2.2x%2.2x-%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x", 
            (unsigned int)u->time_low, u->time_mid, u->time_hi_and_version,
            u->clock_seq_hi_and_reserved, u->clock_seq_low, u->node[0], 
            u->node[1], u->node[2], u->node[3], u->node[4], u->node[5]);
}

/* uuid_create_from_name -- create a UUID using a "name" from a "name space" 
 */
void uuid_create_from_name(guid_t * uuid,      /* resulting UUID */
						   const guid_t nsid,  /* UUID to serve as context, so identical
												  names from different name spaces generate
												  different UUIDs */
						   const void * name,  /* the name from which to generate a UUID */
						   int namelen)        /* the length of the name */
{
	PK11Context *c = NULL;

	unsigned char hash[16];
	unsigned int hashLen;
	guid_t net_nsid;      /* context UUID in network byte order */

	memset(hash, 0, 16);

	/* put name space ID in network byte order so it hashes the same
		no matter what endian machine we're on */

	memset(&net_nsid, 0, sizeof(guid_t));
	net_nsid.time_low = PR_htonl(nsid.time_low);
	net_nsid.time_mid = PR_htons(nsid.time_mid);
	net_nsid.time_hi_and_version = PR_htons(nsid.time_hi_and_version);
	net_nsid.clock_seq_hi_and_reserved=nsid.clock_seq_hi_and_reserved;
	net_nsid.clock_seq_low=nsid.clock_seq_low;
	strncpy((char *)net_nsid.node, (char *)nsid.node, 6);
	
	c = PK11_CreateDigestContext(SEC_OID_MD5);
	if (c != NULL) {
		PK11_DigestBegin(c);
		PK11_DigestOp(c, (unsigned char *)&net_nsid, sizeof(net_nsid));
		PK11_DigestOp(c, (unsigned char *)name, namelen);
		PK11_DigestFinal(c, hash, &hashLen, 16);

		/* the hash is in network byte order at this point */
		format_uuid_v3(uuid, hash);

		PK11_DestroyContext(c, PR_TRUE);
	}
	else { /* Probably desesperate but at least deterministic... */
		memset(uuid, 0, sizeof(*uuid));
	}
}  

/* Helper Functions */

/* uuid_create_st -- singlethreaded generation */
static int uuid_create_st(guid_t *uuid)
{
	uuid_time_t timestamp;

    /* generate new time and save it in the state */
    timestamp = update_time ();

    /* stuff fields into the UUID */
    format_uuid_v1(uuid, timestamp, _state.genstate.clockseq);

	return UUID_SUCCESS;
}

/* uuid_create -- multithreaded generation */
static int uuid_create_mt(guid_t *uuid) 
{
    uuid_time_t timestamp = 0;
	unsigned16 clock_seq = 0;

	/* just bumps time sequence number. the actual
	 * time calls are made by a uuid_update_state */
	if (update_time_mt(&timestamp, &clock_seq) == UUID_TIME_ERROR)
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "uuid_create_mt: generator ran "
						 "out of sequence numbers.\n");
		return UUID_TIME_ERROR;	
	}

    /* stuff fields into UUID */
    format_uuid_v1(uuid, timestamp, clock_seq);

	return UUID_SUCCESS;
}

/* uuid_update_state -- called periodically to update generator's state
					    (multithreaded case only) 
 */
static void uuid_update_state (time_t when, void *arg)
{
	uuid_time_t timestamp;

	get_system_time (&timestamp);

	/* time has not changed since last call - return */
	if (timestamp == _state.genstate.timestamp)
		return;

	PR_Lock (_state.lock);
	/* clock was set backward - insure uuid uniquness by incrementing clock sequence */
	if (timestamp < _state.genstate.timestamp)
		_state.genstate.clockseq ++;

	_state.genstate.timestamp = timestamp;
	_state.time_seq = 0;

	PR_Unlock (_state.lock);      
}

/* read_state -- read UUID generator state from non-volatile store.
*/
static int read_state(const char *configDir, const Slapi_DN *configDN, PRBool *newState)
{
	uuid_time_t timestamp;
	int  rt;

	if (configDN)
		rt = read_state_from_entry (configDN);
	else
		rt = read_state_from_file (configDir);

	if (rt == UUID_NOTFOUND)
		*newState = PR_TRUE;
	else
		*newState = PR_FALSE;		

	if (rt != UUID_SUCCESS && rt != UUID_NOTFOUND) /* fatal error - bail out */
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, 
						 "read_state: failed to get generator's state\n");
		return rt;		 
	}

	/* get current time and nodeid */
	get_system_time(&timestamp);
	if (*newState) /* state info is missing - generate */
	{
		get_node_identifier (&_state.genstate.node);
		_state.genstate.clockseq = true_random();
	} 
	else if(_state.genstate.last_update != 1)
	{
		/* clock sequence should be randomized and not just incremented
		   because server's clock could have been set back before the
           server crashed in which case clock sequence was incremented */
		_state.genstate.clockseq = true_random();
	}
	else if (timestamp <= _state.genstate.timestamp)
	{
		_state.genstate.clockseq ++;
	}

	_state.genstate.timestamp = timestamp;
	_state.time_seq = 0;
	/* need to clear this field so that we know if the state information
	 is written during shutdown (in which case this flag is set to 1 */
	_state.genstate.last_update = 0;

	return UUID_SUCCESS;
}

/* read_state_from_file -- read generator state from file.
*/
static int read_state_from_file (const char *configDir)
{
	char *path;
	int rt;

	if (configDir == NULL || configDir[0] == '\0')
	{ /* this directory */
		path = (char*)slapi_ch_malloc(strlen (STATE_FILE) + 1);
		if (path == NULL)
		{
			slapi_log_error (SLAPI_LOG_FATAL, MODULE, "read_state: "
							"memory allocation failed.\n");
			return (UUID_MEMORY_ERROR);
		}

		strcpy (path, STATE_FILE);    
	}
	else 
	{
		path = slapi_ch_smprintf("%s/%s", configDir, STATE_FILE);    
		if (path == NULL)
		{
			slapi_log_error (SLAPI_LOG_FATAL, MODULE, "read_state: "
							"memory allocation failed.\n");
			return (UUID_MEMORY_ERROR);
		}
	}

	/* open or create state file for read/write and keep it in sync */
	_state.fd = PR_Open(path, PR_RDWR | PR_CREATE_FILE | PR_SYNC,
					    SLAPD_DEFAULT_FILE_MODE);
	slapi_ch_free ((void**)&path);
	if (!_state.fd)
	{
		PRErrorCode prerr = PR_GetError();
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "read_state: "
						 "failed to open state file - %s; " SLAPI_COMPONENT_NAME_NSPR " error %d (%s).\n",
						 path, prerr, slapd_pr_strerror(prerr));
		return (UUID_IO_ERROR);
	}

	rt = PR_Read (_state.fd, &_state.genstate, sizeof(uuid_gen_state));
	if (rt == 0) /* new state */
	{
		return UUID_NOTFOUND;	
	}

	if (rt == -1)
	{
		PRErrorCode prerr = PR_GetError();
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "read_state: "
						"failed to read state information; " SLAPI_COMPONENT_NAME_NSPR " error %d (%s).\n",
						prerr, slapd_pr_strerror(prerr));
		return (UUID_IO_ERROR);
	}  

	return(UUID_SUCCESS);
}	 

/* read_state_from_entry -- read generator state from DIT.
*/
static int read_state_from_entry (const Slapi_DN *configDN)
{
	Slapi_PBlock *pb;
	int res, rt;
	Slapi_Entry **entries;
	Slapi_Attr *attr;
	Slapi_Value *value;
	const struct berval *bv;

	_state.configDN = slapi_sdn_dup (configDN);
	pb = slapi_search_internal(slapi_sdn_get_ndn (configDN), LDAP_SCOPE_BASE, 
							   "objectclass=*", NULL, NULL, 0);

	if (pb == NULL)
	{		
		/* the only time NULL pb is returned is when memory allocation fails */
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "read_state_from_entry: "
						 "NULL pblock returned from search\n");
		return UUID_MEMORY_ERROR;
	}

	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &res);

	if (res == LDAP_NO_SUCH_OBJECT)
	{
		rt = UUID_NOTFOUND;
		goto done;
	}

	if (res != LDAP_SUCCESS)
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "read_state_from_entry: "
						 "search operation failed; LDAP error - %d\n", res);
		rt = UUID_LDAP_ERROR;
		goto done;
	}

	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
	if (entries == NULL || entries[0] == NULL)
	{	
		rt = UUID_UNKNOWN_ERROR;
		goto done;
	}

	/* get state info */
	rt = slapi_entry_attr_find (entries[0], STATE_ATTR, &attr);
	if (rt != LDAP_SUCCESS)
	{
		rt = UUID_FORMAT_ERROR;	
		goto done;
	}

	slapi_attr_first_value(attr,&value);
	if (value == NULL)
	{
		rt = UUID_FORMAT_ERROR;	
		goto done;
	}

	bv = slapi_value_get_berval(value);
	if (bv == NULL || bv->bv_val == NULL || bv->bv_len != sizeof (_state.genstate))
	{
		rt = UUID_FORMAT_ERROR;	
		goto done;
	}

	memcpy (&(_state.genstate), bv->bv_val, bv->bv_len);

done:;
	if (pb)
	{
          slapi_free_search_results_internal(pb);
	  slapi_pblock_destroy(pb); 
	}

	return rt;
}

/* write_state -- save UUID generator state back to non-volatile
                storage. Writes immediately to the disk
*/

static int write_state (PRBool newState)
{
   if (_state.configDN)	/* write to DIT */
	   return write_state_to_entry (newState);
   else /* write to a file */
	   return write_state_to_file ();	
}

/* write_state_to_file -- stores state to state file
*/
static int write_state_to_file() 
{
	int rt;

	rt = PR_Seek (_state.fd, 0, PR_SEEK_SET);
	if (rt == -1)
	{
		PRErrorCode prerr = PR_GetError();
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "write_state: "
						 "failed to rewind state file; " SLAPI_COMPONENT_NAME_NSPR " error %d (%s).\n",
						 prerr, slapd_pr_strerror(prerr));
		return UUID_IO_ERROR;
	}

	rt = PR_Write (_state.fd, &_state.genstate, sizeof (uuid_gen_state));
	if (rt == -1)
	{
		PRErrorCode prerr = PR_GetError();
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "write_state: "
						 "failed to update state file; " SLAPI_COMPONENT_NAME_NSPR " error %d (%s).\n",
						 prerr, slapd_pr_strerror(prerr));

		return (UUID_IO_ERROR);
	}

	return (UUID_SUCCESS);
}

/* write_state_to_entry -- stores state to state file
*/
static int write_state_to_entry(PRBool newState) {
  if (newState)
	return add_state_entry ();
  else
	return modify_state_entry ();
}

/* add_state_entry -- add state entry to the dit */
static int add_state_entry ()
{
	struct berval	*vals[2];
	struct berval	val;
	Slapi_Entry		*e;
	Slapi_PBlock	*pb = NULL;
	int rt;

	vals[0] = &val;
	vals[1] = NULL;

	e = slapi_entry_alloc();
	slapi_entry_set_sdn(e, _state.configDN);

	/* Set the objectclass attribute */
	val.bv_val = "top";
	val.bv_len = strlen (val.bv_val);
	slapi_entry_add_values(e, "objectclass", vals);

	val.bv_val = "extensibleObject";
	val.bv_len = strlen (val.bv_val);
	slapi_entry_add_values(e, "objectclass", vals);

	/* Set state attribute */
	val.bv_val = (char*)&(_state.genstate);
	val.bv_len = sizeof (_state.genstate);
	slapi_entry_add_values(e, STATE_ATTR, vals);

	/* this operation frees the entry */
	pb = slapi_add_entry_internal(e, 0, 0 /* log_change */);		
	if (pb == NULL) 
	{
		/* the only time NULL pb is returned is when memory allocation fails */
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "add_state_entry: "
						 "NULL pblock returned from search\n");
		return UUID_MEMORY_ERROR;
	} 
	else 
	{
		slapi_pblock_get( pb, SLAPI_PLUGIN_INTOP_RESULT, &rt);
		slapi_ch_free((void **) &pb);
	}

	if (rt != LDAP_SUCCESS) 
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "add_state_entry: "
						 "add operation failed; LDAP error - %d.\n", rt);
		return UUID_LDAP_ERROR;
	} 
	
	slapi_log_error (SLAPI_LOG_HOUSE, MODULE, "add_state_entry: "
					 "successfully added generator's state entry");

	return UUID_SUCCESS;
}

/* modify_state_entry -- modify state entry in the dit */
static int modify_state_entry ()
{
	int res;
	Slapi_Mods mods;
	struct berval *vals[2];
	struct berval val;
	Slapi_PBlock *pb;

	val.bv_val = (char*)&(_state.genstate);
	val.bv_len = sizeof (_state.genstate);
	vals[0] = &val;
	vals[1] = NULL;

	slapi_mods_init (&mods, 1);
	slapi_mods_add_modbvps(&mods, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES, STATE_ATTR, vals);
	pb = slapi_modify_internal(slapi_sdn_get_ndn (_state.configDN), 
							   slapi_mods_get_ldapmods_byref(&mods), NULL, 0);
	slapi_mods_done(&mods);	

	if (pb == NULL)
	{
		/* the only time NULL pb is returned is when memory allocation fails */
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "modify_state_entry: "
						 "NULL pblock returned from search\n");
		return UUID_MEMORY_ERROR;
	}

	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &res);
	slapi_pblock_destroy(pb);
	if (res != LDAP_SUCCESS) 
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "modify_state_entry: "
						 "update operation failed; LDAP error - %d.\n", res);

		return UUID_LDAP_ERROR;
	}

	slapi_log_error (SLAPI_LOG_HOUSE, MODULE, "modify_state_entry: "
					 "successfully updated generator's state entry");
	return UUID_SUCCESS;
}

/* update_time -- updates time portion of the generators state
				  for singlethreaded generation
*/
static uuid_time_t update_time() 
{
	uuid_time_t         time_now;

	get_system_time(&time_now);

	/* time was turned back - need to change clocksequence */
	if (time_now < _state.genstate.timestamp)
	{
		_state.genstate.clockseq ++;
		_state.genstate.timestamp = time_now;
		_state.time_seq = 0;
		return _state.genstate.timestamp;
	}

	/* go into loop if the time has not changed since last call */
	while (time_now == _state.genstate.timestamp)
	{
		/* if we still have sequence numbers to give to the
		 timestamp, use it and get out of the loop        */
		if (_state.time_seq < SEQ_PER_SEC - 1)
		{
			_state.time_seq ++;
			break;
		}

		/* this should never happen because we don't generate more that  10 mln ids/sec */
		DS_Sleep (PR_MillisecondsToInterval(500));
		get_system_time(&time_now);
	}

	/* system time has changed - clear sequence number and
	update last time                                    */
	if (time_now > _state.genstate.timestamp)
	{
		_state.time_seq = 0;
		_state.genstate.timestamp = time_now;
	}

	return _state.genstate.timestamp + _state.time_seq;      
}

/* update_time_mt -- this function updates time sequence part of generators state.
					 This function should be used in the multithreaded environment
				     only.
*/
static int update_time_mt (uuid_time_t *timestamp, unsigned16 *clock_seq) 
{

	PR_Lock (_state.lock);

	/* we ran out time sequence numbers because 
       uuid_update_state function is not called
	   frequently enough */
    if (_state.time_seq >= SEQ_PER_SEC - 1)
	{
        _state.time_seq = NEED_TIME_UPDATE;
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "update_time_mt: "
						 "ran out of time sequence numbers; "
						 "uuid_update_state must be called\n");
 
		PR_Unlock (_state.lock);
        return (UUID_TIME_ERROR);
    }

    _state.time_seq++;

	*timestamp = _state.genstate.timestamp + _state.time_seq; 
	*clock_seq = _state.genstate.clockseq;

	PR_Unlock (_state.lock);

    return UUID_SUCCESS;
}

/* format_uuid_v1 -- make a UUID from the timestamp, clockseq,
                     and node ID 
*/
static void format_uuid_v1(guid_t * uuid, uuid_time_t timestamp, unsigned16 clock_seq) 
{
	/* Construct a version 1 uuid with the information we've gathered
     * plus a few constants. */

    uuid->time_low = (unsigned32)(timestamp & 0xFFFFFFFF);
    uuid->time_mid = (unsigned16)((timestamp >> 32) & 0xFFFF);
    uuid->time_hi_and_version = (unsigned16)
                                  ((timestamp >> 48) & 0x0FFF);                                                   
    uuid->time_hi_and_version |= (1 << 12);
    uuid->clock_seq_low = clock_seq & 0xFF;
    uuid->clock_seq_hi_and_reserved = (unsigned8)((clock_seq & 0x3F00) >> 8);
    uuid->clock_seq_hi_and_reserved |= 0x80;
    memcpy(&uuid->node, &_state.genstate.node, sizeof (uuid->node));
}

/* when converting broken values, we may need to swap the bytes */
#define BSWAP16(x) ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8))
#define BSWAP32(x) ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) | \
                    (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))

/* format_uuid_v3 -- make a UUID from a (pseudo)random 128 bit number
*/
static void format_uuid_v3(guid_t * uuid, unsigned char hash[16]) 
{
	char *use_broken_uuid = getenv("USE_BROKEN_UUID");
	/* Construct a version 3 uuid with the (pseudo-)random number
	* plus a few constants. */

	memcpy(uuid, hash, sizeof(guid_t));

	/* when migrating, we skip the ntohl in order to read in old, 
	   incorrectly formatted uuids */
	if (!use_broken_uuid || (*use_broken_uuid == '0')) {
		/* convert UUID to local byte order */
		uuid->time_low = PR_ntohl(uuid->time_low);
		uuid->time_mid = PR_ntohs(uuid->time_mid);
		uuid->time_hi_and_version = PR_ntohs(uuid->time_hi_and_version);
	} else {
#if defined(IS_BIG_ENDIAN)
		/* convert UUID to b0rken byte order */
		uuid->time_low = BSWAP32(uuid->time_low);
		uuid->time_mid = BSWAP16(uuid->time_mid);
		uuid->time_hi_and_version = BSWAP16(uuid->time_hi_and_version);
#endif
	}

	/* put in the variant and version bits */
	uuid->time_hi_and_version &= 0x0FFF;
	uuid->time_hi_and_version |= (3 << 12);
	uuid->clock_seq_hi_and_reserved &= 0x3F;
	uuid->clock_seq_hi_and_reserved |= 0x80;
}

/* system dependent call to get IEEE node ID.
 This sample implementation generates a random node ID
 Assumes that configDir was tested for validity by
 the higher layer
*/
static int get_node_identifier (uuid_node_t *node) 
{
	unsigned char seed[SEED_LENGTH]= {0};

#ifdef USE_NIC
	/* ONREPL - code to use NIC address would go here; Currently, we use 
                cryptographic random number to avoid state sharing among
				servers running on the same host. See UniqueID Generator 
                docs for more info.
	 */				          
#endif
	get_random_info(seed, SEED_LENGTH);
	seed[0] |= 0x80;
	memcpy (node, seed, sizeof (uuid_node_t));
	
	return UUID_SUCCESS;
}

/* call to get the current system time. Returned as 100ns ticks 
 since Oct 15, 1582, but resolution may be less than 100ns.
*/

static void get_system_time(uuid_time_t *uuid_time)
{
	time_t cur_time;

	cur_time = current_time ();

	/* Offset between UUID formatted times and time() formatted times.
       UUID UTC base time is October 15, 1582. time() base time is January 1, 1970.*/
	*uuid_time = cur_time * SEQ_PER_SEC + I64(0x01B21DD213814000);
}

/* ONREPL */

/* true_random -- generate a crypto-quality random number. 
*/
static unsigned16 true_random(void)
{
	static int inited = 0;

	if (!inited) 
	{
		uuid_seed = slapi_rand();
		inited = 1;
	}

	return (slapi_rand_r(&uuid_seed));
}

static void get_random_info(unsigned char seed[], size_t arraylen) 
{
    slapi_rand_array(seed, arraylen);
}

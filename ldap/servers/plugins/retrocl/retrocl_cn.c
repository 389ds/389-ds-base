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

#include "retrocl.h"

static changeNumber retrocl_internal_cn = 0;
static changeNumber retrocl_first_cn = 0;
PRLock *retrocl_internal_lock = NULL;

/*
 * Function: a2changeNumber
 *
 * Returns: changeNumber (long)
 * 
 * Arguments: string
 *
 * Description: parses the string to a changenumber.  changenumbers are 
 * positive integers.
 *
 */

static changeNumber a2changeNumber (const char *p)
{
  changeNumber c;

  c = strntoul((char *)p,strlen(p),10);
  return c;
}

/*
 * Function: handle_cnum_entry
 * Arguments: op - pointer to Operation struct for this operation
 *            e - pointer to returned entry.
 * Returns: nothing
 * Description: Search result handler for retrocl_getchangenum().  Sets the
 *              op->o_handler_data to point to a structure which contains
 *              the changenumber retrieved and an error code.
 */
static int
handle_cnum_entry( Slapi_Entry *e, void *callback_data )
{
    cnumRet *cr = (cnumRet *)callback_data;
    Slapi_Value *sval=NULL;
    const struct berval *value;

    cr->cr_cnum = 0UL;
    cr->cr_time = NULL;

    if ( NULL != e ) {
	Slapi_Attr *chattr = NULL;
	sval = NULL;
	value = NULL;
	if ( slapi_entry_attr_find( e, attr_changenumber, &chattr ) == 0 ) {
	    slapi_attr_first_value( chattr,&sval );
	    if ( NULL != sval ) {
		value = slapi_value_get_berval ( sval );
		if( NULL != value && NULL != value->bv_val &&
		    '\0' != value->bv_val[0]) {
		cr->cr_cnum = a2changeNumber( value->bv_val );
		}
	    }
	}
	chattr = NULL;
	sval = NULL;
	value = NULL;

	chattr = NULL;
	sval = NULL;
	value = NULL;
	if ( slapi_entry_attr_find( e, attr_changetime, &chattr ) == 0 ) {
	    slapi_attr_first_value( chattr,&sval );
	    if ( NULL != sval) {
		value = slapi_value_get_berval ( sval );
		if (NULL != value && NULL != value->bv_val &&
		    '\0' != value->bv_val[0]) {
		cr->cr_time = slapi_ch_strdup( value->bv_val );
		}
	    }
	}
    }
    return 0;
}


/*
 * Function: handle_cnum_result
 * Arguments: err - error code returned from search
 *            callback_data - private data for callback
 * Returns: nothing
 * Description: result handler for retrocl_getchangenum().  Sets the cr_lderr
 *              field of the cnumRet struct to the error returned
 *              from the backend.
 */
static void
handle_cnum_result( int err, void *callback_data )
{
    cnumRet *cr = (cnumRet *)callback_data;
    cr->cr_lderr = err;
}

/*
 * Function: retrocl_get_changenumbers
 *
 * Returns: 0/-1
 * 
 * Arguments: none
 *
 * Description: reads the first and last entry in the changelog to obtain
 * the starting and ending change numbers.
 *
 */

int retrocl_get_changenumbers(void) 
{ 
    cnumRet cr;

    if (retrocl_internal_lock == NULL) {
      retrocl_internal_lock = PR_NewLock();
      
      if (retrocl_internal_lock == NULL) return -1;
    }
    
    if (retrocl_be_changelog == NULL) return -1;
    
    cr.cr_cnum = 0;
    cr.cr_time = 0;

    slapi_seq_callback(RETROCL_CHANGELOG_DN,SLAPI_SEQ_FIRST,
		       (char *)attr_changenumber, /* cast away const */
		       NULL,NULL,0,&cr,NULL,handle_cnum_result,
		       handle_cnum_entry, NULL);

    retrocl_first_cn = cr.cr_cnum;

    slapi_ch_free(( void **) &cr.cr_time );

    slapi_seq_callback(RETROCL_CHANGELOG_DN,SLAPI_SEQ_LAST,
		       (char *)attr_changenumber, /* cast away const */
		       NULL,NULL,0,&cr,NULL,handle_cnum_result,
		       handle_cnum_entry, NULL);

    retrocl_internal_cn = cr.cr_cnum;
    
    slapi_log_error(SLAPI_LOG_PLUGIN,"retrocl","Got changenumbers %d and %d\n",
		    retrocl_first_cn,
		    retrocl_internal_cn);

    slapi_ch_free(( void **) &cr.cr_time );

    return 0;
}

/*
 * Function: retrocl_getchangetime
 * Arguments: type - one of SLAPI_SEQ_FIRST, SLAPI_SEQ_LAST
 * Returns: The time of the requested change record.  If the return value is 
 *          NO_TIME, the changelog could not be read.
 *          If err is non-NULL, the memory it points to is set the the
 *          error code returned from the backend.  If "type" is not valid,
 *          *err is set to -1.
 * Description: Get the first or last changenumber stored in the changelog,
 *              depending on the value of argument "type".
 */
time_t retrocl_getchangetime( int type, int *err )
{
    cnumRet cr;
    time_t ret;

    if ( type != SLAPI_SEQ_FIRST && type != SLAPI_SEQ_LAST ) {
	if ( err != NULL ) {
	    *err = -1;
	}
	return NO_TIME;
    }
    memset( &cr, '\0', sizeof( cnumRet ));
    slapi_seq_callback( RETROCL_CHANGELOG_DN, type, 
			(char *)attr_changenumber, /* cast away const */
			NULL,
			NULL, 0, &cr, NULL, 
			handle_cnum_result, handle_cnum_entry, NULL ); 
    
    if ( err != NULL ) {
	*err = cr.cr_lderr;
    }
    
    if ( NULL == cr.cr_time ) {
	ret = NO_TIME;
    } else {
	ret = parse_localTime( cr.cr_time );
    }
    slapi_ch_free(( void **) &cr.cr_time );
    return ret;
}

/*
 * Function: retrocl_forget_changenumbers
 *
 * Returns: none
 * 
 * Arguments: none
 *
 * Description: used only when the server is shutting down
 *
 */

void retrocl_forget_changenumbers(void) 
{ 
    if (retrocl_internal_lock == NULL) return;

    PR_Lock(retrocl_internal_lock);
    retrocl_first_cn = 0;
    retrocl_internal_cn = 0;
    PR_Unlock(retrocl_internal_lock);
}

/*
 * Function: retrocl_get_first_changenumber 
 *
 * Returns: changeNumber
 * 
 * Arguments: none
 *
 * Description: used in root DSE
 *
 */

changeNumber retrocl_get_first_changenumber(void) 
{ 
    changeNumber cn;
    PR_Lock(retrocl_internal_lock);
    cn = retrocl_first_cn;
    PR_Unlock(retrocl_internal_lock);
    return cn;
}

/*
 * Function: retrocl_set_first_changenumber
 *
 * Returns: none
 * 
 * Arguments: changenumber
 *
 * Description: used in changelog trimming
 *
 */

void retrocl_set_first_changenumber(changeNumber cn) 
{ 
    PR_Lock(retrocl_internal_lock);
    retrocl_first_cn = cn;
    PR_Unlock(retrocl_internal_lock);
}


/*
 * Function: retrocl_get_last_changenumber
 *
 * Returns:
 * 
 * Arguments:
 *
 * Description: used in root DSE
 *
 */

changeNumber retrocl_get_last_changenumber(void) 
{ 
    changeNumber cn;
    PR_Lock(retrocl_internal_lock);
    cn = retrocl_internal_cn;
    PR_Unlock(retrocl_internal_lock);
    return cn;
}

/*
 * Function: retrocl_commit_changenumber
 *
 * Returns: none
 * 
 * Arguments: none, lock must be held
 *
 * Description: NOTE! MUST BE PRECEEDED BY retrocl_assign_changenumber
 *
 */

void retrocl_commit_changenumber(void) 
{ 
    if ( retrocl_first_cn == 0) {
        retrocl_first_cn = retrocl_internal_cn;
    }
}

/*
 * Function: retrocl_release_changenumber
 *
 * Returns: none
 * 
 * Arguments: none, lock must be held
 *
 * Description: NOTE! MUST BE PRECEEDED BY retrocl_assign_changenumber
 *
 */

void retrocl_release_changenumber(void) 
{ 
    retrocl_internal_cn--;
}

/*
 * Function: retrocl_update_lastchangenumber
 *
 * Returns: 0/-1
 *
 * Arguments: none
 *
 * Description: reads the last entry in the changelog to obtain
 * the last change number.
 *
 */

int retrocl_update_lastchangenumber(void)
{
    cnumRet cr;

    if (retrocl_internal_lock == NULL) {
      retrocl_internal_lock = PR_NewLock();

      if (retrocl_internal_lock == NULL) return -1;
    }

    if (retrocl_be_changelog == NULL) return -1;

    cr.cr_cnum = 0;
    cr.cr_time = 0;
    slapi_seq_callback(RETROCL_CHANGELOG_DN,SLAPI_SEQ_LAST,
               (char *)attr_changenumber, /* cast away const */
               NULL,NULL,0,&cr,NULL,handle_cnum_result,
               handle_cnum_entry, NULL);


    retrocl_internal_cn = cr.cr_cnum;
    slapi_log_error(SLAPI_LOG_PLUGIN,"retrocl","Refetched last changenumber =  %d \n",
            retrocl_internal_cn);

    slapi_ch_free(( void **) &cr.cr_time );

    return 0;
}



/*
 * Function: retrocl_assign_changenumber
 *
 * Returns: change number, 0 on error
 * 
 * Arguments: none.  Lock must be held.
 *
 * Description: NOTE! MUST BE FOLLOWED BY retrocl_commit_changenumber or 
 * retrocl_release_changenumber
 *
 */

changeNumber retrocl_assign_changenumber(void)
{
    changeNumber cn;
 
    if (retrocl_internal_lock == NULL) return 0;

    /* Before we assign the changenumber; we should check for the
     * validity of the internal assignment of retrocl_internal_cn 
     * we had from the startup */  

    if(retrocl_internal_cn <= retrocl_first_cn){ 
        /* the numbers have become out of sync - retrocl_get_changenumbers
         * gets called only once during startup and it may have had a problem 
         * getting the last changenumber.         
         * If there was any problem then update the lastchangenumber from the changelog db.
         * This function is being called by only the thread that is actually writing
         * to the changelog.
         */                                                         
        retrocl_update_lastchangenumber();                                   
    }                             

    retrocl_internal_cn++;
    cn = retrocl_internal_cn;
    return cn;
}

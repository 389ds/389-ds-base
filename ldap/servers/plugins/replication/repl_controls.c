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


#include "slapi-plugin.h"
#include "repl5.h"

/*
 * repl_controls.c - convenience functions for creating and
 * decoding controls that implement 5.0-style replication
 * protocol operations.
 *
 * TODO: Send modrdn mods with modrdn operation
 *       Fix ber_printf() and ber_scanf() format strings - some are
 *       the wrong types.
 */

/*
 * Return a pointer to a NSDS50ReplUpdateInfoControl.
 * The control looks like this:
 *
 * NSDS50ReplUpdateInfoControl ::= SEQUENCE {
 *     uuid OCTET STRING,
 *     csn OCTET STRING,
 *     OPTIONAL [new]superior-uuid OCTET STRING
 *     OPTIONAL modrdn_mods XXXggood WHAT TYPE???
 * }
 */
int
create_NSDS50ReplUpdateInfoControl(const char *uuid,
                                   const char *superior_uuid,
                                   const CSN *csn,
                                   LDAPMod **modrdn_mods,
                                   LDAPControl **ctrlp)
{
    int retval;
    BerElement *tmp_bere = NULL;
    char csn_str[CSN_STRSIZE];

    if (NULL == ctrlp) {
        retval = LDAP_PARAM_ERROR;
        goto loser;
    } else {
        if ((tmp_bere = ber_alloc()) == NULL) {
            retval = LDAP_NO_MEMORY;
            goto loser;
        } else {
            /* Stuff uuid and csn into BerElement */
            if (ber_printf(tmp_bere, "{") == -1) {
                retval = LDAP_ENCODING_ERROR;
                goto loser;
            }

            /* Stuff uuid of this entry into BerElement */
            if (ber_printf(tmp_bere, "s", uuid) == -1) {
                retval = LDAP_ENCODING_ERROR;
                goto loser;
            }

            /* Stuff csn of this change into BerElement */
            csn_as_string(csn, PR_FALSE, csn_str);
            if (ber_printf(tmp_bere, "s", csn_str) == -1) {
                retval = LDAP_ENCODING_ERROR;
                goto loser;
            }

            /* If present, stuff uuid of parent entry into BerElement */
            if (NULL != superior_uuid) {
                if (ber_printf(tmp_bere, "s", superior_uuid) == -1) {
                    retval = LDAP_ENCODING_ERROR;
                    goto loser;
                }
            }

            /* If present, add the modrdn mods */
            if (NULL != modrdn_mods) {
                int i;
                if (ber_printf(tmp_bere, "{") == -1) {
                    retval = LDAP_ENCODING_ERROR;
                    goto loser;
                }
                /* for each modification to be performed... */
                for (i = 0; NULL != modrdn_mods[i]; i++) {
                    if (ber_printf(tmp_bere, "{e{s[V]}}",
                                   modrdn_mods[i]->mod_op & ~LDAP_MOD_BVALUES,
                                   modrdn_mods[i]->mod_type, modrdn_mods[i]->mod_bvalues) == -1) {
                        retval = LDAP_ENCODING_ERROR;
                        goto loser;
                    }
                }
                if (ber_printf(tmp_bere, "}") == -1) {
                    retval = LDAP_ENCODING_ERROR;
                    goto loser;
                }
            }

            /* Close the sequence */
            if (ber_printf(tmp_bere, "}") == -1) {
                retval = LDAP_ENCODING_ERROR;
                goto loser;
            }

            retval = slapi_build_control(REPL_NSDS50_UPDATE_INFO_CONTROL_OID,
                                         tmp_bere, 1 /* is critical */, ctrlp);
        }
    }
loser:
    if (NULL != tmp_bere) {
        ber_free(tmp_bere, 1);
        tmp_bere = NULL;
    }
    return retval;
}


/*
 * Destroy a ReplUpdateInfoControl and set the pointer to NULL.
 */
void
destroy_NSDS50ReplUpdateInfoControl(LDAPControl **ctrlp)
{
    if (NULL != ctrlp && NULL != *ctrlp) {
        ldap_control_free(*ctrlp);
        *ctrlp = NULL;
    }
}


/*
 * Look through the array of controls. If an NSDS50ReplUpdateInfoControl
 * is present, decode it and return pointers to the broken-out
 * components. The caller is responsible for freeing pointers to
 * the returned objects. The caller may indicate that it is not
 * interested in any of the output parameters by passing NULL
 * for that parameter.
 *
 * Returns 0 if the control is not present, 1 if it is present, and
 * -1 if an error occurs.
 */
int
decode_NSDS50ReplUpdateInfoControl(LDAPControl **controlsp,
                                   char **uuid,
                                   char **superior_uuid,
                                   CSN **csn,
                                   LDAPMod ***modrdn_mods)
{
    struct berval *ctl_value = NULL;
    int iscritical = 0;
    int rc = -1;
    struct berval uuid_val = {0};
    struct berval superior_uuid_val = {0};
    struct berval csn_val = {0};
    BerElement *tmp_bere = NULL;
    Slapi_Mods modrdn_smods;
    PRBool got_modrdn_mods = PR_FALSE;
    ber_len_t len;

    slapi_mods_init(&modrdn_smods, 4);
    if (slapi_control_present(controlsp, REPL_NSDS50_UPDATE_INFO_CONTROL_OID,
                              &ctl_value, &iscritical)) {
        if (!BV_HAS_DATA(ctl_value) || (tmp_bere = ber_init(ctl_value)) == NULL) {
            rc = -1;
            goto loser;
        }
        if (ber_scanf(tmp_bere, "{oo", &uuid_val, &csn_val) == LBER_ERROR) {
            rc = -1;
            goto loser;
        }
        if (ber_peek_tag(tmp_bere, &len) == LBER_OCTETSTRING) {
            /* The optional superior_uuid is present */
            if (ber_scanf(tmp_bere, "o", &superior_uuid_val) == LBER_DEFAULT) {
                rc = -1;
                goto loser;
            }
        }
        if (ber_peek_tag(tmp_bere, &len) == LBER_SEQUENCE) {
            ber_tag_t emtag;
            ber_len_t emlen;
            char *emlast;

            for (emtag = ber_first_element(tmp_bere, &emlen, &emlast);
                 emtag != LBER_ERROR && emtag != LBER_END_OF_SEQORSET;
                 emtag = ber_next_element(tmp_bere, &emlen, emlast)) {
                struct berval **embvals;
                ber_int_t op;
                char *type;
                if (ber_scanf(tmp_bere, "{i{a[V]}}", &op, &type, &embvals) == LBER_ERROR) {
                    rc = -1;
                    goto loser;
                }
                slapi_mods_add_modbvps(&modrdn_smods, op, type, embvals);
                slapi_ch_free_string(&type);
                ber_bvecfree(embvals);
            }
            got_modrdn_mods = PR_TRUE;
        }
        if (ber_scanf(tmp_bere, "}") == LBER_ERROR) {
            rc = -1;
            goto loser;
        }

        if (NULL != uuid) {
            *uuid = slapi_ch_malloc(uuid_val.bv_len + 1);
            strncpy(*uuid, uuid_val.bv_val, uuid_val.bv_len);
            (*uuid)[uuid_val.bv_len] = '\0';
        }

        if (NULL != csn) {
            char *csnstr = slapi_ch_malloc(csn_val.bv_len + 1);
            strncpy(csnstr, csn_val.bv_val, csn_val.bv_len);
            csnstr[csn_val.bv_len] = '\0';
            *csn = csn_new_by_string(csnstr);
            slapi_ch_free((void **)&csnstr);
        }

        if (NULL != superior_uuid && NULL != superior_uuid_val.bv_val) {
            *superior_uuid = slapi_ch_malloc(superior_uuid_val.bv_len + 1);
            strncpy(*superior_uuid, superior_uuid_val.bv_val,
                    superior_uuid_val.bv_len);
            (*superior_uuid)[superior_uuid_val.bv_len] = '\0';
        }

        if (NULL != modrdn_mods && got_modrdn_mods) {
            *modrdn_mods = slapi_mods_get_ldapmods_passout(&modrdn_smods);
        }
        slapi_mods_done(&modrdn_smods);

        rc = 1;
    } else {
        rc = 0;
    }
loser:
    /* XXXggood free CSN here if allocated */

    if (NULL != tmp_bere) {
        ber_free(tmp_bere, 1);
        tmp_bere = NULL;
    }
    if (NULL != uuid_val.bv_val) {
        ldap_memfree(uuid_val.bv_val);
        uuid_val.bv_val = NULL;
    }
    if (NULL != superior_uuid_val.bv_val) {
        ldap_memfree(superior_uuid_val.bv_val);
        superior_uuid_val.bv_val = NULL;
    }
    if (NULL != csn_val.bv_val) {
        ldap_memfree(csn_val.bv_val);
        csn_val.bv_val = NULL;
    }
    return rc;
}


void
add_repl_control_mods(Slapi_PBlock *pb, Slapi_Mods *smods)
{
    struct berval *embvp;
    LDAPControl **controls = NULL;

    slapi_pblock_get(pb, SLAPI_REQCONTROLS, &controls);
    if (slapi_control_present(controls,
                              LDAP_CONTROL_REPL_MODRDN_EXTRAMODS,
                              &embvp, NULL)) {
        if (embvp != NULL && embvp->bv_len > 0 && embvp->bv_val != NULL) {
            /* Parse the extramods stuff */
            ber_int_t op;
            char *type;
            ber_len_t emlen;
            ber_tag_t emtag;
            char *emlast;
            BerElement *ember = ber_init(embvp);
            if (ember != NULL) {
                for (emtag = ber_first_element(ember, &emlen, &emlast);
                     emtag != LBER_ERROR && emtag != LBER_END_OF_SEQORSET;
                     emtag = ber_next_element(ember, &emlen, emlast)) {
                    struct berval **embvals = NULL;
                    type = NULL;
                    if (ber_scanf(ember, "{i{a[V]}}", &op, &type, &embvals) != LBER_ERROR) {
                        slapi_mods_add_modbvps(smods, op, type, embvals);
                        /* GGOODREPL I suspect this will cause two sets of lastmods attr values
                        to end up in the entry. We need to remove the old ones.
                    */
                    }
                    slapi_ch_free_string(&type);
                    ber_bvecfree(embvals);
                }
            }
            ber_free(ember, 1);
        }
    }
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "back-ldbm.h"

/*
 * In filterindex, rather than calling idl_union multiple times over
 * this idl_set provides better apis to do efficent set manipulations
 * for idl results.
 *
 * previously or results were calculated as:
 * |(a)(b)(c)(d)
 *
 * t1 = union(a, b)
 * t2 = union(t1, c)
 * t3 = union(t2, d)
 * As you can see, this results in n-1 union operations. Depending on
 * the size of the IDL, this could signifigantly affect performance.
 * It actually let to a situation where re-arranging a query would be
 * faster.
 *
 * This idl_set code allows us to perform a k-way intersection and union.
 * this means given some arbitrary number of IDLists (k), we intersect
 * or union all k of them at the same time, rather than generating
 * intermediates.
 *
 * k-way union
 * -----------
 *
 * First, if we have allids, return.
 * Imagine we have these sets:
 *
 * (1,2,3,4) (1,5,6) (1), ()
 *
 * We start with a pointer at the head of each:
 *
 * (1,2,3,4) (1,5,6) (1), ()
 *  ^         ^       ^   ^
 *
 * if the idl is empty, we prune it:
 *
 * (1,2,3,4) (1,5,6) (1)
 *  ^         ^       ^
 *
 * Now we check the min id for all sets. In this case the
 * sets agree it's 1, so we add 1 to the result set.
 *
 * now we move the pointers along and prune the empty set
 *
 * (1,2,3,4) (1,5,6)
 *    ^         ^
 *
 * Now the next min is 2 - when we examine the second list
 * because 5 > 2, we ignore this for now. Finally we then
 * insert 2. We advance the pointer.
 *
 * (1,2,3,4) (1,5,6)
 *      ^       ^
 * We continue this until we exhaust the first list, or the
 * second.
 *
 * k-way intersection
 * ------------------
 *
 * k-way intersection intersects multiple idls at the same time.
 * it works by continually iterating over our lists to build a
 * quorum, where all idls agree on the current minimal element.
 *
 * given:
 *
 * (1,2,3,4,5,6) (1,2,5,6) (3,5,6)
 *
 * we start our pointers at the start as in k-way union
 *
 * (1,2,3,4,5,6) (1,2,5,6) (3,5,6)
 *  ^             ^         ^
 *  | min=0, q=0
 *
 * since min=0, we set this to the min of the first list. Because
 * the first list "agrees" that 1 is the min, we set quorum to 1.
 *
 * (1,2,3,4,5,6) (1,2,5,6) (3,5,6)
 *  ^             ^         ^
 *  | min=1, q=1
 *
 * Now we move to the next list. It also has 1 as the min, so we
 * increase the quorum.
 *
 * (1,2,3,4,5,6) (1,2,5,6) (3,5,6)
 *  ^             ^         ^
 *                | min=1, q=2
 *
 * When we advance to the third list we see 3 is the min. So we reset
 * min to 3, and q to 1 (since only this list thinks 3 is min so far)
 *
 * (1,2,3,4,5,6) (1,2,5,6) (3,5,6)
 *  ^             ^         ^
 *                          | min=3, q=1
 *
 * We now go back to the first list. Since 1 < min, we advance our
 * pointer til either we exceed min or equal it. In this case we
 * equal it and add to quorum.
 *
 * (1,2,3,4,5,6) (1,2,5,6) (3,5,6)
 *      ^         ^         ^
 *      | min=3, q=2
 *
 * When we get to the next list, we repeat this. We advance to 5.
 *
 * (1,2,3,4,5,6) (1,2,5,6) (3,5,6)
 *      ^             ^     ^
 *                    | min=5, q=1
 *
 * Next when we get to list 3 we have 5, and finall list 1 we have 5
 *
 * (1,2,3,4,5,6) (1,2,5,6) (3,5,6)
 *      ^             ^       ^
 *                            | min=5, q=2
 *
 * (1,2,3,4,5,6) (1,2,5,6) (3,5,6)
 *          ^         ^       ^
 *          | min=5, q=3
 *
 * We finally have quorum! Now we insert 5 to the result_list, and
 * advance all our idl by 1.
 *
 *
 */

IDListSet *
idl_set_create()
{
    IDListSet *idl_set = (IDListSet *)slapi_ch_calloc(1, sizeof(IDListSet));
    /* all other fields are 0 thanks to calloc */
    return idl_set;
}

static void
idl_set_free_idls(IDListSet *idl_set)
{
    /* Free idlists */
    IDList *idl = idl_set->head;
    IDList *next_idl = NULL;
    while (idl != NULL) {
        next_idl = idl->next;
        idl_free(&idl);
        idl = next_idl;
    }

    /* Free complements if any */
    idl = idl_set->complement_head;
    while (idl != NULL) {
        next_idl = idl->next;
        idl_free(&idl);
        idl = next_idl;
    }
}

void
idl_set_destroy(IDListSet *idl_set)
{
    slapi_ch_free((void **)&(idl_set));
}

void
idl_set_insert_idl(IDListSet *idl_set, IDList *idl)
{
    PR_ASSERT(idl);

    /*
     * prune incoming allids - for union, we just return
     * allids, for intersection, if we only have
     * allids we return, else we just ignore it.
     */
    if (idl_is_allids(idl)) {
        idl_set->allids = 1;
        idl_free(&idl);
        return;
    }

    /*
     * Track the current min set to make intersect alloc small
     */
    if (idl_set->minimum == NULL || idl->b_nids < idl_set->minimum->b_nids) {
        idl_set->minimum = idl;
    }
    /*
     * Track this for max possible union size of these sets.
     */
    idl_set->total_size += idl->b_nids;

    idl->next = idl_set->head;
    idl_set->head = idl;
    idl_set->count += 1;

    return;
}

/*
 * The difference between this and insert is that complement implies
 * a future intersection, and that we plan to use a "not" query.
 *
 * As a result, the order of operations is to:
 * * intersect all sets
 * * apply complements.
 */
void
idl_set_insert_complement_idl(IDListSet *idl_set, IDList *idl)
{
    PR_ASSERT(idl);
    /*
     * If we complement to ALLIDS, the result is empty set.
     * so we can put empty set into the main list. This will cause
     * -- no change to  union (but this should never be called during OR / union)
     * -- shortcut of the AND to trigger immediately
     */
    idl->next = idl_set->complement_head;
    idl_set->complement_head = idl;
}

int64_t
idl_set_union_shortcut(IDListSet *idl_set)
{
    /* Are we able to shortcut the union process? */
    /* This generally indicates the presence of allids */
    return idl_set->allids;
}

int64_t
idl_set_intersection_shortcut(IDListSet *idl_set)
{
    /* If we have a 0 length idl, we can never create an intersection. */
    if (idl_set->minimum != NULL && idl_set->minimum->b_nids <= FILTER_TEST_THRESHOLD) {
        return 1;
    }
    return 0;
}

IDList *
idl_set_union(IDListSet *idl_set, backend *be)
{
    /*
     * Check allids first, because allids = 1, may not
     * have set count > 0.
     */
    if (idl_set->allids != 0) {
        idl_set_free_idls(idl_set);
        return idl_allids(be);
    } else if (idl_set->count == 0) {
        return idl_alloc(0);
    } else if (idl_set->count == 1) {
        return idl_set->head;
    } else if (idl_set->count == 2) {
        IDList *result_list = idl_union(be, idl_set->head, idl_set->head->next);
        idl_free(&(idl_set->head->next));
        idl_free(&(idl_set->head));
        return result_list;
    }

    /*
     * Allocate a new set based on the size of our sets.
     */
    IDList *result_list = idl_alloc(idl_set->total_size);
    IDList *idl = NULL;
    IDList *idl_del = NULL;
    IDList *prev_idl = NULL;
    NIDS last_min = 0;
    NIDS next_min = 0;

    /*
     * Now we iterate over our sets - if the current itr is
     * cur_min and ! in the new set, add it. While we do this
     * we are finding the next_min to repeat.
     *
     * we continue until we exhaust every set we have.
     *
     * check for idl_set->head->next NULL?
     */
    while (idl_set->head != NULL) {
        prev_idl = NULL;
        idl = idl_set->head;
        /* now find the next smallest */
        while (idl) {
            /*
             * Did our head value get inserted last round?
             */
            if (idl->b_ids[idl->itr] == last_min && last_min != 0) {
                /*
                 * Our value was previously inserted - advance itr.
                 */
                idl->itr += 1;
            }
            /*
             * Nothing left to search in this idl
             * itr should never be >, but lets be safe.
             */
            if (idl->itr >= idl->b_nids) {
                if (prev_idl) {
                    prev_idl->next = idl->next;
                } else {
                    /*
                     * This is our base case: when we strike the last
                     * idl, and prev is null,  and next is null, we are done!
                     */
                    PR_ASSERT(idl == idl_set->head);
                    idl_set->head = idl->next;
                }
                idl_del = idl;
                idl = idl_del->next;
                idl_free(&idl_del);
                /* No need to touch prev_idl. */
            } else {
                /*
                 * Now check our value and see if it's this iterations
                 * smallest candidate.
                 */
                if (idl->b_ids[idl->itr] < next_min || next_min == 0) {
                    next_min = idl->b_ids[idl->itr];
                }
                /* Go to the next idl. */
                prev_idl = idl;
                idl = idl->next;
            }
        }
        /* Insert the current smallest value we have */
        /* next_min can be 0 because we freed all idls remaining */
        if (next_min > 0) {
            idl_append(result_list, next_min);
        }

        last_min = next_min;
        next_min = 0;
    }

    return result_list;
}

IDList *
idl_set_intersect(IDListSet *idl_set, backend *be)
{
    IDList *result_list = NULL;

    if (idl_set->allids) {
        /* if any component was allids we have to apply the filtertest */
        slapi_be_set_flag(be, SLAPI_BE_FLAG_DONT_BYPASS_FILTERTEST);
    }

    if (idl_set->allids != 0 && idl_set->count == 0) {
        /*
         * We only have allids, so must be allids.
         */
        result_list = idl_allids(be);
    } else if (idl_set->count == 0) {
        /*
         * No allids, but we do have ... nothing?
         */
        result_list = idl_alloc(0);
    } else if (idl_set->count == 1) {
        /*
         * If allids, when we intersect head, we get head, so just skip that.
         */
        result_list = idl_set->head;
    } else if (idl_set->minimum->b_nids <= FILTER_TEST_THRESHOLD) {
        result_list = idl_set->minimum;
        slapi_be_set_flag(be, SLAPI_BE_FLAG_DONT_BYPASS_FILTERTEST);

        /* Free the other IDLs which are not the minimum. */
        IDList *next = NULL;
        IDList *idl = idl_set->head;
        while (idl != NULL) {
            next = idl->next;
            if (idl != idl_set->minimum) {
                idl_free(&idl);
            }
            idl = next;
        }
    } else if (idl_set->count == 2) {
        /*
         * If we have two items only, just intersect them.
         */
        result_list = idl_intersection(be, idl_set->head, idl_set->head->next);
        idl_free(&(idl_set->head->next));
        idl_free(&(idl_set->head));
    } else {
        /*
         * Must have at least 2 idls or more, so do a k-way intersection.
         * result_list is allocated to size of min, because intersection can not
         * exceed the size of the smallest set we have.
         *
         * we don't care if we have allids here, because we'll ignore it anyway.
         */
        result_list = idl_alloc(idl_set->minimum->b_nids);
        IDList *idl = NULL;

        /* The previous value we inserted. */
        NIDS last_min = 0;
        /* The next minimum we have found */
        NIDS next_min = 0;

        uint64_t finished = 0;
        uint64_t quorum = 0;

        while (idl_set->head != NULL) {
            idl = idl_set->head;
            /* now find the next smallest */
            while (idl && finished == 0) {
                /*
                 * Did our head value get inserted last round?
                 */
                if (idl->b_ids[idl->itr] == last_min && last_min != 0) {
                    /*
                     * Our value was previously inserted - advance itr.
                     */
                    idl->itr += 1;
                }
                /*
                 * Nothing left to search in this idl
                 * itr should never be >, but lets be safe.
                 */
                if (idl->itr >= idl->b_nids) {
                    /*
                     * This is our base case: when we have emptied *one*
                     * idl, intersections of the remaining values can never be
                     * valid.
                     */
                    finished = 1;
                    /*
                     * ensure we don't insert, we're done.
                     */
                    quorum = 0;
                } else {
                    /*
                     * Still data in IDLs
                     */
                    if (next_min == 0) {
                        /* Must be head: so put our value as next_min */
                        next_min = idl->b_ids[idl->itr];
                    } else if (next_min < idl->b_ids[idl->itr]) {
                        /*
                         * Must be a diff id. mark it as skip, nothing can be
                         * inserted this iteration.
                         */
                        quorum = 1;
                        /*
                         * Because this is our minimum value of this IDL, set the next_min
                         * to it.
                         */
                        next_min = idl->b_ids[idl->itr];
                    } else if (next_min > idl->b_ids[idl->itr]) {
                        /*
                         * We must be behind the next_min. We need to advance til we are
                         * eq or greater.
                         *
                         * I tried optimising this to lookahead and jump by blocks of 256
                         * or 64, and it made it slower. Probably not worth it :(
                         */
                        while (idl->itr < idl->b_nids && next_min > idl->b_ids[idl->itr]) {
                            idl->itr += 1;
                        }
                        /*
                         * Right, made it here. Are we out of ids?
                         */
                        if (idl->itr >= idl->b_nids) {
                            finished = 1;
                            quorum = 0;
                        } else if (next_min < idl->b_ids[idl->itr]) {
                            /* okay, we don't have next_min. Update and reset */
                            next_min = idl->b_ids[idl->itr];
                            quorum = 1;
                        } else {
                            /* Great! we match! */
                            quorum += 1;
                        }
                    } else {
                        /*
                         * Must be in agreeance - this head is the next_min
                         */
                        quorum += 1;
                    }
                    /*
                     * check if all the idls agree this is the smallest value
                     * so we insert it!
                     */
                    if (next_min > 0 && quorum == idl_set->count) {
                        idl_append(result_list, next_min);
                        last_min = next_min;
                        next_min = 0;
                        quorum = 0;
                    }
                    /* Go to the next idl. */
                    idl = idl->next;
                }
            }

            if (finished) {
                /*
                 * We emptied an IDL - drain them all.
                 */
                IDList *idl_del = NULL;
                idl = idl_set->head;
                while (idl) {
                    idl_del = idl;
                    idl = idl_del->next;
                    idl_free(&idl_del);
                }
                idl_set->head = NULL;
            }
        }
    }

    /* Now, that we have the "smallest" intersection possible, we need to subtract
     * elements as required.
     *
     * NOTE: This is still not optimised yet!
     */
    if (idl_set->complement_head != NULL && result_list->b_nids > 0) {
        IDList *new_result_list = NULL;
        IDList *next_idl = NULL;
        IDList *idl = idl_set->complement_head;
        while (idl != NULL) {
            next_idl = idl->next;
            if (idl_notin(be, result_list, idl, &new_result_list)) {
                /*
                 * idl_notin returns 1 on new alloc, so free result_list and idl
                 */
                idl_free(&idl);
                idl_free(&result_list);
                result_list = new_result_list;
            } else {
                /*
                 * idl_notin returns 0 when it "does nothing", so just free idl.
                 */
                idl_free(&idl);
            }
            idl = next_idl;
        }
    }

    return result_list;
}

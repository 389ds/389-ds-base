/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef RESHASH_H
#define RESHASH_H
/**********************************************************************
    Hash  --> Tree --> ValueList      

    ValueList: language per item, each list associated with one key
    Tree:  contains multiple keys
    Hash:  Based on hash to decide withc tree to use for lookup

***********************************************************************/

/* 
  Valuelist, each item contains 
    language:  ISO two or four letters 
    value:     UTF-8 encoding strings
 */
typedef struct ValueNode {
	char *language;
	char *value;
    struct ValueNode *next;
} ValueNode;


/*  
  Current: BINARY TREE
  Future: balanced tree for high search performance
 */
typedef struct TreeNodeStruct {
	ValueNode *vlist;
    char *key;
	char *value;
    struct TreeNodeStruct *left;
    struct TreeNodeStruct *right;
} TreeNode;


typedef struct ResHash {
    char *name;  /* name of hash table */
    TreeNode *treelist;
} ResHash;


ResHash * ResHashCreate(char * name);
int ResHashAdd(ResHash *res, char *key, char *value, char *language);
const char *ResHashSearch(ResHash *res, char *key, char *language);
void ResHashDestroy(ResHash *res);

#endif


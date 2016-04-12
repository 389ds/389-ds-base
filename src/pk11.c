/*
 * Copyright (C) 1998 Netscape Communications Corporation.
 * All Rights Reserved.
 *
 * Copyright 2016 Red Hat, Inc. and/or its affiliates.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * pk11.c - SVRCORE module for securely storing PIN using PK11
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <svrcore.h>

#include <string.h>
#include <secitem.h>
#include <pk11func.h>

/* ------------------------------------------------------------ */
/*
 * Mechanisms for doing the PIN encryption.  Each of these lists
 * an encryption mechanism, with setup, encode and decode routines that
 * use that mechanism.  The PK11PinStore looks for a mechanism
 * that the token supports, and then uses it.  If none is found,
 * it will fail.
 */
typedef struct mech_item mech_item;
struct mech_item
{
  CK_MECHANISM_TYPE type;
  const char *mechName;
};

/* ------------------------------------------------------------ */
/*
 * The table listing all mechanism to try
 */
#define MECH_TABLE_SIZE 4
static const mech_item table[MECH_TABLE_SIZE] = {
  { CKM_SKIPJACK_CBC64, "Skipjack CBC-64 encryption" },
  { CKM_DES3_CBC,       "Triple-DES CBC encryption" },
  { CKM_CAST128_CBC,    "CAST-128 CBC encryption" },
  { CKM_DES_CBC,        "DES CBC encryption" }
};
static mech_item dflt_mech = { CKM_DES3_CBC, "Triple-DES CBC (default)" };


/* ------------------------------------------------------------ */
/*
 * Implementation
 */
struct SVRCOREPk11PinStore
{
  PK11SlotInfo *slot;

  const mech_item *mech;

  PK11SymKey *key;
  SECItem *params;

  int length;
  unsigned char *crypt;
};


/* ------------------------------------------------------------ */
/*
 * SVRCORE_CreatePk11PinStore
 */
SVRCOREError
SVRCORE_CreatePk11PinStore(
  SVRCOREPk11PinStore **out,
  const char *tokenName, const char *pin)
{
  SVRCOREError err;
  SVRCOREPk11PinStore *store;

  do {
    err = SVRCORE_Success;

    store = (SVRCOREPk11PinStore*)malloc(sizeof *store);
    if (store == 0) { err = SVRCORE_NoMemory_Error; break; }

    /* Low-level init */
    store->slot = 0;
    store->key = 0;
    store->params = 0;
    store->crypt = 0;

    /* Use the tokenName to find a PKCS11 slot */
    store->slot = PK11_FindSlotByName((char *)tokenName);
    if (store->slot == 0) { err = SVRCORE_NoSuchToken_Error; break; }

    /* Check the password/PIN.  This allows access to the token */
    {
      SECStatus rv = PK11_CheckUserPassword(store->slot, (char *)pin);

      if (rv == SECSuccess)
        ;
      else if (rv == SECWouldBlock)
      {
        err = SVRCORE_IncorrectPassword_Error;
        break;
      }
      else
      {
        err = SVRCORE_System_Error;
        break;
      }
    }

    /* Find the mechanism that this token can do */
    {
      const mech_item *tp;

      store->mech = 0;
      for(tp = table;tp < &table[MECH_TABLE_SIZE];tp++)
      {
        if (PK11_DoesMechanism(store->slot, tp->type))
        {
          store->mech = tp;
          break;
        }
      }
      /* Default to a mechanism (probably on the internal token */
      if (store->mech == 0)
        store->mech = &dflt_mech;
    }

    /* Generate a key and parameters to do the encryption */
    store->key = PK11_TokenKeyGenWithFlags(store->slot, store->mech->type,
                 0, 0, 0, CKF_ENCRYPT|CKF_DECRYPT,
                 0, 0);
    if (store->key == 0)
    {
      /* PR_SetError(xxx); */
      err = SVRCORE_System_Error;
      break;
    }

    store->params = PK11_GenerateNewParam(store->mech->type, store->key);
    if (store->params == 0)
    {
      err = SVRCORE_System_Error;
      break;
    }

    /* Compute the size of the encrypted data including necessary padding */
    {
      int blocksize = PK11_GetBlockSize(store->mech->type, 0);

      store->length = strlen(pin)+1;

      /* Compute padded size - 0 means stream cipher */
      if (blocksize != 0)
      {
        store->length += blocksize - (store->length % blocksize);
      }

      store->crypt = (unsigned char *)malloc(store->length);
      if (!store->crypt) { err = SVRCORE_NoMemory_Error; break; }
    }

    /* Encrypt */
    {
      unsigned char *plain;
      PK11Context *ctx;
      SECStatus rv;
      int outLen;

      plain = (unsigned char *)malloc(store->length);
      if (!plain) { err = SVRCORE_NoMemory_Error; break; }

      /* Pad with 0 bytes */
      memset(plain, 0, store->length);
      strcpy((char *)plain, pin);

      ctx = PK11_CreateContextBySymKey(store->mech->type, CKA_ENCRYPT,
              store->key, store->params);
      if (!ctx) {
        err = SVRCORE_System_Error;
        free(plain);
        break;
      }

      do {
        rv = PK11_CipherOp(ctx, store->crypt, &outLen, store->length,
               plain, store->length);
        if (rv) break;

        rv = PK11_Finalize(ctx);
      } while(0);

      PK11_DestroyContext(ctx, PR_TRUE);
      memset(plain, 0, store->length);
      free(plain);

      if (rv) err = SVRCORE_System_Error;
    }
  } while(0);

  if (err)
  {
    SVRCORE_DestroyPk11PinStore(store);
    store = 0;
  }

  *out = store;
  return err;
}

/*
 * SVRCORE_DestroyPk11PinStore
 */
void
SVRCORE_DestroyPk11PinStore(SVRCOREPk11PinStore *store)
{
  if (store == 0) return;

  if (store->slot)
  {
    PK11_FreeSlot(store->slot);
  }

  if (store->params)
  {
    SECITEM_ZfreeItem(store->params, PR_TRUE);
  }

  if (store->key)
  {
    PK11_FreeSymKey(store->key);
  }

  if (store->crypt)
  {
    memset(store->crypt, 0, store->length);
    free(store->crypt);
  }

  free(store);
}

SVRCOREError
SVRCORE_Pk11StoreGetPin(char **out, SVRCOREPk11PinStore *store)
{
  SVRCOREError err = SVRCORE_Success;
  unsigned char *plain;
  SECStatus rv = 0;
  PK11Context *ctx = 0;
  int outLen;

  do {
    plain = (unsigned char *)malloc(store->length);
    if (!plain) { err = SVRCORE_NoMemory_Error; break; }

    ctx = PK11_CreateContextBySymKey(store->mech->type, CKA_DECRYPT,
              store->key, store->params);
    if (!ctx) { err = SVRCORE_System_Error; break; }

    rv = PK11_CipherOp(ctx, plain, &outLen, store->length,
           store->crypt, store->length);
    if (rv) break;

    rv = PK11_Finalize(ctx);
    if (rv) break;
  } while(0);

  if (ctx) PK11_DestroyContext(ctx, PR_TRUE);

  if (rv)
  {
    err = SVRCORE_System_Error;
    if (plain) {
      memset(plain, 0, store->length);
      free(plain);
      plain = 0;
    }
  }

  *out = (char *)plain;
  return err;
}

const char *
SVRCORE_Pk11StoreGetMechName(const SVRCOREPk11PinStore *store)
{
  return store->mech->mechName;
}

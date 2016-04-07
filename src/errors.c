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
 * errors.c - SVRCORE Error strings
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <svrcore.h>

const char * const SVRCORE_Errors[] = {
  "Operation completed successfully",
  "Not enough memory to complete operation",
  "Unspecified error",
  "Token missing or unavailable",
  "Incorrect password or PIN provided"
};

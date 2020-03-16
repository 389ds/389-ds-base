// BEGIN COPYRIGHT BLOCK
// Copyright (c) 2017, Red Hat, Inc
// All rights reserved.
//
// License: GPL (version 3 or any later version).
// See LICENSE for details.
// END COPYRIGHT BLOCK

#![warn(missing_docs)]

//! sds is a collection of datastructures used in the slapi api. This contains
//! a thread safe queue and others implemented in C.

/// Implementation of a thread safe queue.
pub mod tqueue;

#[repr(C)]
/// Slapi Data Structure Result types. Indicates the status of the operation
/// for C compatability (instead of Result<T>
pub enum sds_result {
    /// The operation was a success
    Success = 0,
    /// An unknown error occured. This indicates a fault in the API.
    UnknownError = 1,
    /// A null pointer was provided as an argument to a function. This is
    /// invalid.
    NullPointer = 2,
    /// The list is exhausted, no more elements can be returned.
    ListExhausted = 16,
}

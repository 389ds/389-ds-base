
# Slapi R(ust) Plugin Bindings

If you are here, you are probably interested in the Rust bindings that allow plugins to be written
in Rust for the 389 Directory Server project. If you are, you should use `cargo doc --workspace --no-deps`
in `src`, as this contains the material you want for implementing safe plugins.

This readme is intended for developers of the bindings that enable those plugins to work.

As such it likely requires that you have an understanding both of C and
the [Rust Nomicon](https://doc.rust-lang.org/nomicon/index.html)

> **WARNING** This place is not a place of honor ... no highly esteemed deed is commemorated here
> ... nothing valued is here. What is here is dangerous and repulsive to us. This message is a
> warning about danger.

This document will not detail the specifics of unsafe or the invariants you must adhere to for rust
to work with C.

If you still want to see more about the plugin bindings, go on ...

## The Challenge

Rust is a memory safe language - that means you may not dereference pointers or alter or interact
with uninitialised memory. There are whole classes of problems that this resolves, but it means
that Rust is opiniated about how it interacts with memory.

C is an unsafe language - there are undefined behaviours all through out the specification, memory
can be interacted with without bounds which leads to many kinds of issues ranging from crashes,
silent data corruption, to code execution and explotation.

While it would be nice to rewrite everything from C to Rust, this is a large task - instead we need
a way to allow Rust and C to interact.

## The Goal

To be able to define, a pure Rust, 100% safe (in rust terms) plugin for 389 Directory Server that
can perform useful tasks.

## The 389 Directory Server Plugin API

The 389-ds plugin system works by reading an ldap entry from cn=config, that directs to a shared
library. That shared library path is dlopened and an init symbol read and activated. At that
point the plugin is able to call-back into 389-ds to provide registration of function handlers for
various tasks that the plugin may wish to perform at defined points in a operations execution.

During the execution of a plugin callback, the context of the environment is passed through a
parameter block (pblock). This pblock has a set of apis for accessing it's content, which may
or may not be defined based on the execution state of the server.

Common plugin tasks involve the transformation of entries during write operation paths to provide
extra attributes to the entry or generation of other entries. Values in entries are represented by
internal structures that may or may not have sorting of content.

Already at this point it can be seen there is a lot of surface area to access. For clarity in
our trivial example here we have required:

* Pblock
* Entry
* ValueSet
* Value
* Sdn
* Result Codes

We need to be able to interact with all of these - and more - to make useful plugins.

## Structure of the Rust Plugin bindings.

As a result, there are a number of items we must be able to implement:

* Creation of the plugin function callback points
* Transformation of C pointer types into Rust structures that can be interacted with.
* Ability to have Rust interact with structures to achieve side effects in the C server
* Mapping of errors that C can understand
* Make all of it safe.

In order to design this, it's useful to see what a plugin from Rust should look like - by designing
what the plugin should look like, we make the bindings that are preferable and ergonomic to rust
rather than compromising on quality and developer experience.

Here is a minimal example of a plugin - it may not compile or be complete, it serves as an
example.

```
#[macro_use]
extern crate slapi_r_plugin;
use slapi_r_plugin::prelude::*;

struct NewPlugin;

slapi_r_plugin_hooks!(plugin_name, NewPlugin);

impl SlapiPlugin3 for NewPlugin {
    fn start(_pb: &mut PblockRef) -> Result<(), PluginError> {
        log_error!(ErrorLevel::Trace, "plugin start");
        Ok(())
    }

    fn close(_pb: &mut PblockRef) -> Result<(), PluginError> {
        log_error!(ErrorLevel::Trace, "plugin close");
        Ok(())
    }

    fn has_betxn_pre_add() -> bool {
        true
    }

    fn betxn_pre_add(pb: &mut PblockRef) -> Result<(), PluginError> {
        let mut e = pb.get_op_add_entryref().map_err(|_| PluginError::Pblock)?;
        let sdn = e.get_sdnref();

        log_error!(ErrorLevel::Trace, "betxn_pre_add -> {:?}", sdn);
        Ok(())
    }
}
```

Important details - there is no unsafe, we use rust native error handling and functions, there
is no indication of memory management, we are defined by a trait, error logging uses native
formatting. There are probably other details too - I'll leave it as an exercise for the reader
to play Where's Wally and find them all.

With the end goal in mind, we can begin to look at the construction of the plugin system, and
the design choices that were made.

## The Plugin Trait

A significant choice was the use of a trait to define the possible plugin function operations
for rust implementors. This allows the compiler to guarantee that a plugin *will* have all
associated functions.

> Traits are synonomous with java interfaces, defining methods you "promise" to implement, unlike
> object orientation with a class hierarchy.

Now, you may notice that not all members of the trait are implemented. This is due to a feature
of rust known as default trait impls. This allows the trait origin (src/plugin.rs) to provide
template versions of these functions. If you "overwrite" them, your implementation is used. Unlike
OO, you may not inherit or call the default function. 

If a default is not provided you *must* implement that function to be considered valid. Today (20200422)
this only applies to `start` and `close`.

The default implementations all return "false" to the presence of callbacks, and if they are used,
they will always return an error.

## Interface generation

While it is nice to have this Rust interface for plugins, C is unable to call it (Rust uses a different
stack calling syntax to C, as well as symbol mangaling). To expose these, we must provide `extern C`
functions, where any function that requires a static symbol must be marked as no_mangle.

Rather than ask all plugin authors to do this, we can use the rust macro system to generate these
interfaces at compile time. This is the reason for this line:

```
slapi_r_plugin_hooks!(plugin_name, NewPlugin);
```

This macro is defined in src/macros.rs, and is "the bridge" from C to Rust. Given a plugin name
and a struct of the trait SlapiPlugin3, this macro is able to generate all needed C compatible
functions. Based on the calls to `has_<op_type>`, the generated functions are registered to the pblock
that is provided.

When a call back triggers, the function landing point is called. This then wraps all the pointer
types from C into Rust structs, and then dispatches to the struct instance.

When the struct function returns, the result is unpacked and turned into C compatible result codes -
in some cases, the result codes are sanitised due to quirks in the C ds api - `[<$mod_ident _plugin_mr_filter_ava>]`
is an excellent example of this, where Rust returns are `true`/`false`, which would normally
be FFI safe to convert to 1/0 respectively, but 389-ds expects the inverse in this case, where
0 is true and all other values are false. To present a sane api to rust, the macro layer does this
(mind bending) transformation for us.

## C Ptr Wrapper types

This is likely the major, and important detail of the plugin api. By wrapping these C ptrs with
Rust types, we can create types that perform as rust expects, and adheres to the invariants required,
while providing safe - and useful interfaces to users.

It's important to understand how Rust manages memory both on the stack and the heap - Please see
[the Rust Book](https://doc.rust-lang.org/book/ch04-00-understanding-ownership.html) for more.

As a result, this means that we must express in code, assertions about the proper ownership of memory
and who is responsible for it (unlike C, where it can be hard to determine who or what is responsible
for freeing some value.) Failure to handle this correctly, can and will lead to crashes, leaks or
*hand waving* magical failures that are eXtReMeLy FuN to debug.

### Reference Types

There are a number of types, such as `SdnRef`, which have a suffix of `*Ref`. These types represent
values whos content is owned by the C server - that is, it is the responsibility of 389-ds to free
the content of the Pointer once it has been used. A majority of values that are provided to the
function callback points fall into this class.

### Owned Types

These types contain a pointer from the C server, but it is the responsibility of the Rust library
to indicate when that pointer and it's content should be disposed of. This is generally handled
by the `drop` trait, which is executed ... well, when an item is dropped.

### Dispatch from the wrapper to C

When a rust function against a wrapper is called, the type internally accesses it Ref type and
uses the ptr to dispatch into the C server. Any required invariants are upheld, and results are
mapped as required to match what rust callers expect.

As a result, this involves horrendous amounts of unsafe, and a detailed analysis of both the DS C
api, what it expects, and the Rust nomicon to ensure you maintain all the invariants.

## Conclusion

Providing a bridge between C and Rust is challenging - but achievable - the result is plugins that
are clean, safe, efficent.




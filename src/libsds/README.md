Libsds {#mainpage}
======


Libsds is the Slapi Data Structures Library. This encapsulates a queue, b+tree
and set, a copy-on-write b+tree for parallelism.

In the future it will contain a copy-on-write LRU cache as well.

This abstraction exists so that we can reuse this library with Nunc-Stans. The
queue detects if we are on an x86_64 capable CPU and will build a lock free
variant if possible.

This library has thorough and complete tests of various complex states to
assure correct behaviour. It has been tested against NSPR Hashmaps and the
slapi AVL tree for performance.

Building
--------

    autoreconf -fiv
    ./configure [--enable-tests]
    make
    [make test]
    make install

### Author

William Brown <william@blackhats.net.au>


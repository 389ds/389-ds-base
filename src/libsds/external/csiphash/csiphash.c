/* <MIT License>
 Copyright (c) 2013  Marek Majkowski <marek@popcount.org>

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 </MIT License>

 Original location:
    https://github.com/majek/csiphash/

 Solution inspired by code from:
    Samuel Neves (supercop/crypto_auth/siphash24/little)
    djb (supercop/crypto_auth/siphash24/little2)
    Jean-Philippe Aumasson (https://131002.net/siphash/siphash24.c)
*/

#include <inttypes.h>
#include <stddef.h> /* for size_t */

#include <stdlib.h> /* calloc,free */
#include <string.h> /* memcpy */

#include <config.h>

#if defined(HAVE_SYS_ENDIAN_H)
#include <sys/endian.h>
#elif defined(HAVE_ENDIAN_H)
#include <endian.h>
#else
#error platform header for endian detection not found.
#endif

/* See: http://sourceforge.net/p/predef/wiki/Endianness/ */
#if defined(__BYTE_ORDER) && defined(__LITTLE_ENDIAN) && __BYTE_ORDER == __LITTLE_ENDIAN
#define _le64toh(x) ((uint64_t)(x))
#else
#define _le64toh(x) le64toh(x)
#endif


#define ROTATE(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define HALF_ROUND(a, b, c, d, s, t) \
    a += b;                          \
    c += d;                          \
    b = ROTATE(b, s) ^ a;            \
    d = ROTATE(d, t) ^ c;            \
    a = ROTATE(a, 32);

#define ROUND(v0, v1, v2, v3)           \
    HALF_ROUND(v0, v1, v2, v3, 13, 16); \
    HALF_ROUND(v2, v1, v0, v3, 17, 21)

#define cROUND(v0, v1, v2, v3) \
    ROUND(v0, v1, v2, v3)

#define dROUND(v0, v1, v2, v3) \
    ROUND(v0, v1, v2, v3);     \
    ROUND(v0, v1, v2, v3);     \
    ROUND(v0, v1, v2, v3)


uint64_t
sds_siphash13(const void *src, size_t src_sz, const char key[16])
{
    uint64_t _key[2] = {0};
    memcpy(_key, key, 16);
    uint64_t k0 = _le64toh(_key[0]);
    uint64_t k1 = _le64toh(_key[1]);
    uint64_t b = (uint64_t)src_sz << 56;

    size_t input_sz = (src_sz / sizeof(uint64_t)) + 1;

    /* Account for non-uint64_t alligned input */
    /* Could make this stack allocation */
    uint64_t *in = calloc(1, input_sz * sizeof(uint64_t));
    if (in == NULL) {
        return 0;
    }
    /*
     * Because all crypto code sucks, they modify *in
     * during operation, so we stash a copy of the ptr here.
     * alternately, we could use stack allocated array, but gcc
     * will complain about the vla being unbounded.
     */
    uint64_t *in_ptr = memcpy(in, src, src_sz);

    uint64_t v0 = k0 ^ 0x736f6d6570736575ULL;
    uint64_t v1 = k1 ^ 0x646f72616e646f6dULL;
    uint64_t v2 = k0 ^ 0x6c7967656e657261ULL;
    uint64_t v3 = k1 ^ 0x7465646279746573ULL;

    while (src_sz >= 8) {
        uint64_t mi = _le64toh(*in);
        in += 1;
        src_sz -= 8;
        v3 ^= mi;
        // cround
        cROUND(v0, v1, v2, v3);
        v0 ^= mi;
    }

    /*
     * Because we allocate in as size + 1, we can over-read 0
     * for this buffer to be padded correctly. in here is a pointer to the
     * excess data because the while loop above increments the in pointer
     * to point to the excess once src_sz drops < 8.
     */
    uint64_t t = 0;
    memcpy(&t, in, sizeof(uint64_t));

    b |= _le64toh(t);

    v3 ^= b;
    // cround
    cROUND(v0, v1, v2, v3);
    v0 ^= b;
    v2 ^= 0xff;
    // dround
    dROUND(v0, v1, v2, v3);

    free(in_ptr);

    return (v0 ^ v1) ^ (v2 ^ v3);
}


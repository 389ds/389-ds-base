/***** defines *****/
#define LFDS710_PRNG_MAX  ( (lfds710_pal_uint_t) -1 )

/* TRD : the seed is from an on-line hardware RNG, using atmospheric noise
         the URL below will generate another 16 random hex digits (e.g. a 64-bit number) and is
         the RNG used to generate the number above (0x0a34655d34c092fe)

         http://www.random.org/integers/?num=16&min=0&max=15&col=1&base=16&format=plain&rnd=new

         the 32 bit seed is the upper half of the 64 bit seed

         the "SplitMix" PRNG is from from Sebastiano vigna's site, CC0 license, http://xorshift.di.unimi.it/splitmix64.c
         the 64-bit constants come directly from the source, the 32-bt constants are in fact the 32-bit murmurhash3 constants
*/

#if( LFDS710_PAL_ALIGN_SINGLE_POINTER == 4 )
  #define LFDS710_PRNG_SEED                            0x0a34655dUL
  #define LFDS710_PRNG_SPLITMIX_MAGIC_RATIO            0x9E3779B9UL
  #define LFDS710_PRNG_SPLITMIX_SHIFT_CONSTANT_ONE     16
  #define LFDS710_PRNG_SPLITMIX_SHIFT_CONSTANT_TWO     13
  #define LFDS710_PRNG_SPLITMIX_SHIFT_CONSTANT_THREE   16
  #define LFDS710_PRNG_SPLITMIX_MULTIPLY_CONSTANT_ONE  0x85ebca6bUL
  #define LFDS710_PRNG_SPLITMIX_MULTIPLY_CONSTANT_TWO  0xc2b2ae35UL
#endif

#if( LFDS710_PAL_ALIGN_SINGLE_POINTER == 8 )
  #define LFDS710_PRNG_SEED                            0x0a34655d34c092feULL
  #define LFDS710_PRNG_SPLITMIX_MAGIC_RATIO            0x9E3779B97F4A7C15ULL
  #define LFDS710_PRNG_SPLITMIX_SHIFT_CONSTANT_ONE     30
  #define LFDS710_PRNG_SPLITMIX_SHIFT_CONSTANT_TWO     27
  #define LFDS710_PRNG_SPLITMIX_SHIFT_CONSTANT_THREE   31
  #define LFDS710_PRNG_SPLITMIX_MULTIPLY_CONSTANT_ONE  0xBF58476D1CE4E5B9ULL
  #define LFDS710_PRNG_SPLITMIX_MULTIPLY_CONSTANT_TWO  0x94D049BB133111EBULL
#endif

// TRD : struct lfds710_prng_state prng_state, lfds710_pal_uint_t random_value
#define LFDS710_PRNG_GENERATE( prng_state, random_value )                                                                  \
{                                                                                                                          \
  LFDS710_PAL_ATOMIC_ADD( &(prng_state).entropy, LFDS710_PRNG_SPLITMIX_MAGIC_RATIO, (random_value), lfds710_pal_uint_t );  \
  LFDS710_PRNG_ST_MIXING_FUNCTION( random_value );                                                                         \
}

// TRD : struct lfds710_prng_state prng_st_state, lfds710_pal_uint_t random_value
#define LFDS710_PRNG_ST_GENERATE( prng_st_state, random_value )                       \
{                                                                                     \
  (random_value) = ( (prng_st_state).entropy += LFDS710_PRNG_SPLITMIX_MAGIC_RATIO );  \
  LFDS710_PRNG_ST_MIXING_FUNCTION( random_value );                                    \
}

// TRD : lfds710_pal_uint_t random_value
#define LFDS710_PRNG_ST_MIXING_FUNCTION( random_value )                                                                                            \
{                                                                                                                                                  \
  (random_value) = ((random_value) ^ ((random_value) >> LFDS710_PRNG_SPLITMIX_SHIFT_CONSTANT_ONE)) * LFDS710_PRNG_SPLITMIX_MULTIPLY_CONSTANT_ONE;  \
  (random_value) = ((random_value) ^ ((random_value) >> LFDS710_PRNG_SPLITMIX_SHIFT_CONSTANT_TWO)) * LFDS710_PRNG_SPLITMIX_MULTIPLY_CONSTANT_TWO;  \
  (random_value) = (random_value ^ (random_value >> LFDS710_PRNG_SPLITMIX_SHIFT_CONSTANT_THREE));                                                  \
}

/***** structs *****/
struct lfds710_prng_state
{
  lfds710_pal_uint_t volatile LFDS710_PAL_ALIGN(LFDS710_PAL_ATOMIC_ISOLATION_IN_BYTES)
    entropy;
};

struct lfds710_prng_st_state
{
  lfds710_pal_uint_t
    entropy;
};

/***** public prototypes *****/
void lfds710_prng_init_valid_on_current_logical_core( struct lfds710_prng_state *ps, lfds710_pal_uint_t seed );
void lfds710_prng_st_init( struct lfds710_prng_st_state *psts, lfds710_pal_uint_t seed );


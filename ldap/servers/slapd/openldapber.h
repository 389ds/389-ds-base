/*
 * openldap lber library does not provide an API which returns the ber size
 * (ber->ber_len) when the ber tag is LBER_DEFAULT or LBER_OVERFLOW.
 * The ber size is useful when issuing an error message to indicate how
 * large the maxbersize needs to be set.
 * Borrowed from liblber/lber-int.h
 */
struct lber_options {
    short lbo_valid;
    unsigned short      lbo_options;
    int         lbo_debug;
};
struct berelement {
    struct      lber_options ber_opts;
    ber_tag_t   ber_tag;
    ber_len_t   ber_len;
    ber_tag_t   ber_usertag;
    char        *ber_buf;
    char        *ber_ptr;
    char        *ber_end;
    char        *ber_sos_ptr;
    char        *ber_rwptr;
    void        *ber_memctx;
};
typedef struct berelement OLBerElement;

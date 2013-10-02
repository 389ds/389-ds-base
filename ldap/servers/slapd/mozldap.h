/*
 * mozldap does not have all the openldap "ber" functions, like ber_skip_element.
 * So we need to directly parse the ber element, and see inside the ber struct.
 * From lber-int.h
 */
typedef struct seqorset {
   ber_len_t    sos_clen;
   ber_tag_t    sos_tag;
   char         *sos_first;
   char         *sos_ptr;
   struct seqorset      *sos_next;
} Seqorset;

#define SOS_STACK_SIZE 8 /* depth of the pre-allocated sos structure stack */
#define MAX_TAG_SIZE (1 + sizeof(ber_int_t)) /* One byte for the length of the tag */
#define MAX_LEN_SIZE (1 + sizeof(ber_int_t)) /* One byte for the length of the length */
#define MAX_VALUE_PREFIX_SIZE (2 + sizeof(ber_int_t)) /* 1 byte for the tag and 1 for the len (msgid) */
#define BER_ARRAY_QUANTITY 7 /* 0:Tag   1:Length   2:Value-prefix   3:Value   4:Value-suffix  */

struct berelement {
    ldap_x_iovec  ber_struct[BER_ARRAY_QUANTITY];   /* See above */
    char          ber_tag_contents[MAX_TAG_SIZE];
    char          ber_len_contents[MAX_LEN_SIZE];
    char          ber_pre_contents[MAX_VALUE_PREFIX_SIZE];
    char          ber_suf_contents[MAX_LEN_SIZE+1];
    char          *ber_buf; /* update the value value when writing in case realloc is called */
    char          *ber_ptr;
    char          *ber_end;
    struct seqorset       *ber_sos;
    ber_len_t ber_tag_len_read;
    ber_tag_t     ber_tag; /* Remove me someday */
    ber_len_t     ber_len; /* Remove me someday */
    int           ber_usertag;
    char          ber_options;
    char          *ber_rwptr;
    BERTranslateProc ber_encode_translate_proc;
    BERTranslateProc ber_decode_translate_proc;
    int           ber_flags;
#define LBER_FLAG_NO_FREE_BUFFER        1       /* don't free ber_buf */
    unsigned  int ber_buf_reallocs;              /* realloc counter */
    int           ber_sos_stack_posn;
    Seqorset      ber_sos_stack[SOS_STACK_SIZE];
};
typedef struct berelement MozElement;


#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stddef.h>  /* size_t */

/* ------------------------------------------------------------------ */
/* Token types                                                          */
/* ------------------------------------------------------------------ */
typedef enum {
    TOK_TEXT = 0,
    TOK_OPEN_TAG,
    TOK_CLOSE_TAG,
    TOK_SELF_CLOSE_TAG,
    TOK_DOCTYPE,
    TOK_COMMENT,
    TOK_ENTITY,
    TOK_CDATA,
    TOK_SCRIPT,
    TOK_STYLE,
    TOK_SYNTHETIC_CLOSE,  /* implicitly closed tag — start=NULL, len=TagId */
} HtmlTokenType;

extern const char *TOKEN_NAMES[];  /* index with HtmlTokenType */

typedef struct {
    HtmlTokenType  type;
    const char    *start;  /* pointer into the original buffer */
    size_t         len;
} Token;

/* ------------------------------------------------------------------ */
/* Dynamic token array                                                  */
/* ------------------------------------------------------------------ */
typedef struct {
    Token  *data;
    size_t  count;
    size_t  cap;
} TokenArray;

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/* Tokenize [buf, buf+len). Returns a heap-allocated TokenArray.
   Caller must free ta.data when done. */
TokenArray html_tokenize(const char *buf, size_t len);

#endif /* TOKENIZER_H */
/*
 * tokenizer.c
 * HTML scanner (SSE2 SIMD) and tokenizer implementation.
 *
 * Compile (GCC/Clang, x86-64):
 *   gcc -O2 -msse2 -c tokenizer.c
 */

#include "tokenizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <emmintrin.h> /* SSE2 */
#ifdef _WIN32
#include <windows.h>
#endif

/* ------------------------------------------------------------------ */
/* Token name table                                                     */
/* ------------------------------------------------------------------ */
const char *TOKEN_NAMES[] = {
    "TEXT", "OPEN_TAG", "CLOSE_TAG", "SELF_CLOSE_TAG",
    "DOCTYPE", "COMMENT", "ENTITY", "CDATA", "SCRIPT", "STYLE",
    "SYNTHETIC_CLOSE"};

/* ------------------------------------------------------------------ */
/* Tag identity enum (for implicit closing logic)                       */
/* Only tags that participate in implicit closing need entries.         */
/* ------------------------------------------------------------------ */
typedef enum
{
    TAG_UNKNOWN = 0,
    TAG_P,
    TAG_LI,
    TAG_DT,
    TAG_DD,
    TAG_TR,
    TAG_TD,
    TAG_TH,
    /* block-level tags that force-close <p> when opened */
    TAG_DIV,
    TAG_H1,
    TAG_H2,
    TAG_H3,
    TAG_H4,
    TAG_H5,
    TAG_H6,
    TAG_UL,
    TAG_OL,
    TAG_TABLE,
    TAG_BLOCKQUOTE,
    TAG_PRE,
    TAG_DL,
    TAG_HR,
    TAG_ADDRESS,
    /* container close tags that flush their children */
    TAG_BODY,
    TAG_HTML,
} TagId;

/* Synthetic close token — emitted when we implicitly close a tag */
#define TOK_SYNTHETIC_CLOSE ((HtmlTokenType)10)

/* ------------------------------------------------------------------ */
/* Open element stack with small-buffer optimisation                    */
/* ------------------------------------------------------------------ */
#define STACK_INLINE_CAP 32

typedef struct
{
    TagId inline_buf[STACK_INLINE_CAP];
    TagId *data; /* points to inline_buf normally, heap if overflowed */
    int top;     /* index of next free slot                           */
    int cap;     /* current capacity                                  */
    int heap;    /* 1 if data is heap-allocated                       */
} ElemStack;

static void es_init(ElemStack *es)
{
    es->data = es->inline_buf;
    es->top = 0;
    es->cap = STACK_INLINE_CAP;
    es->heap = 0;
}

static void es_free(ElemStack *es)
{
    if (es->heap)
        free(es->data);
}

static int es_push(ElemStack *es, TagId id)
{
    if (es->top == es->cap)
    {
        int newcap = es->cap * 2;
        TagId *newdata = (TagId *)malloc((size_t)newcap * sizeof(TagId));
        if (!newdata)
            return 0;
        memcpy(newdata, es->data, (size_t)es->top * sizeof(TagId));
        if (es->heap)
            free(es->data);
        es->data = newdata;
        es->cap = newcap;
        es->heap = 1;
    }
    es->data[es->top++] = id;
    return 1;
}

static TagId es_peek(const ElemStack *es)
{
    return es->top > 0 ? es->data[es->top - 1] : TAG_UNKNOWN;
}

static TagId es_pop(ElemStack *es)
{
    return es->top > 0 ? es->data[--es->top] : TAG_UNKNOWN;
}

/* ------------------------------------------------------------------ */
/* Internal: dynamic token array                                        */
/* ------------------------------------------------------------------ */
static void ta_init(TokenArray *ta)
{
    ta->cap = 1 << 16;
    ta->count = 0;
    ta->data = (Token *)malloc(ta->cap * sizeof(Token));
    if (!ta->data)
    {
        fprintf(stderr, "OOM\n");
        exit(1);
    }
}

static void ta_push(TokenArray *ta, HtmlTokenType type,
                    const char *start, size_t len)
{
    if (ta->count == ta->cap)
    {
        ta->cap *= 2;
        ta->data = (Token *)realloc(ta->data, ta->cap * sizeof(Token));
        if (!ta->data)
        {
            fprintf(stderr, "OOM\n");
            exit(1);
        }
    }
    ta->data[ta->count].type = type;
    ta->data[ta->count].start = start;
    ta->data[ta->count].len = len;
    ta->count++;
}

/* ------------------------------------------------------------------ */
/* SSE2 scanner                                                         */
/* ------------------------------------------------------------------ */
static const char *simd_scan(const char *start, const char *end)
{
    const __m128i q = _mm_set1_epi8('<');
    const __m128i e = _mm_set1_epi8('&');
    const __m128i r = _mm_set1_epi8('\r');
    const __m128i z = _mm_set1_epi8('\0');

    while (start + 15 < end)
    {
        __m128i data = _mm_loadu_si128((const __m128i *)start);
        __m128i m = _mm_or_si128(
            _mm_or_si128(_mm_cmpeq_epi8(data, q),
                         _mm_cmpeq_epi8(data, z)),
            _mm_or_si128(_mm_cmpeq_epi8(data, e),
                         _mm_cmpeq_epi8(data, r)));
        int mask = _mm_movemask_epi8(m);
        if (mask != 0)
        {
#ifdef _MSC_VER
            unsigned long idx;
            _BitScanForward(&idx, (unsigned long)mask);
            start += idx;
#else
            start += __builtin_ctz(mask);
#endif
            return start;
        }
        start += 16;
    }
    while (start < end)
    {
        unsigned char c = (unsigned char)*start;
        if (c == '<' || c == '&' || c == '\r' || c == '\0')
            return start;
        start++;
    }
    return end;
}

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */
static int starts_with_ci(const char *p, const char *end,
                          const char *needle)
{
    while (*needle)
    {
        if (p >= end)
            return 0;
        char c = *p;
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (c != *needle)
            return 0;
        p++;
        needle++;
    }
    return 1;
}

static const char *find_str(const char *p, const char *end,
                            const char *needle)
{
    size_t nlen = strlen(needle);
    while (p + nlen <= end)
    {
        if (memcmp(p, needle, nlen) == 0)
            return p;
        p++;
    }
    return NULL;
}

static const char *simd_scan_tag(const char *p, const char *end)
{
    const __m128i gt = _mm_set1_epi8('>');
    const __m128i dq = _mm_set1_epi8('"');
    const __m128i sq = _mm_set1_epi8('\'');

    while (p + 15 < end)
    {
        __m128i data = _mm_loadu_si128((const __m128i *)p);
        __m128i m = _mm_or_si128(
            _mm_cmpeq_epi8(data, gt),
            _mm_or_si128(
                _mm_cmpeq_epi8(data, dq),
                _mm_cmpeq_epi8(data, sq)));
        int mask = _mm_movemask_epi8(m);
        if (mask != 0)
        {
#ifdef _MSC_VER
            unsigned long idx;
            _BitScanForward(&idx, (unsigned long)mask);
            p += idx;
#else
            p += __builtin_ctz(mask);
#endif
            return p;
        }
        p += 16;
    }
    while (p < end)
    {
        unsigned char c = (unsigned char)*p;
        if (c == '>' || c == '"' || c == '\'')
            return p;
        p++;
    }
    return end;
}

static const char *simd_scan_quoted(const char *p, const char *end,
                                    char quote)
{
    const __m128i qc = _mm_set1_epi8(quote);

    while (p + 15 < end)
    {
        __m128i data = _mm_loadu_si128((const __m128i *)p);
        __m128i m = _mm_cmpeq_epi8(data, qc);
        int mask = _mm_movemask_epi8(m);
        if (mask != 0)
        {
#ifdef _MSC_VER
            unsigned long idx;
            _BitScanForward(&idx, (unsigned long)mask);
            p += idx;
#else
            p += __builtin_ctz(mask);
#endif
            return p;
        }
        p += 16;
    }
    while (p < end && *p != quote)
        p++;
    return p;
}

static const char *skip_to_tag_end(const char *p, const char *end)
{
    while (p < end)
    {
        p = simd_scan_tag(p, end);
        if (p >= end)
            break;
        char c = *p;
        if (c == '>')
            return p;
        p++;
        p = simd_scan_quoted(p, end, c);
        if (p < end)
            p++;
    }
    return end;
}

/* ------------------------------------------------------------------ */
/* Tag name recognition                                                 */
/* ------------------------------------------------------------------ */
static TagId identify_tag(const char *p, const char *end)
{
    if (p >= end)
        return TAG_UNKNOWN;

    char c0 = *p;
    if (c0 >= 'A' && c0 <= 'Z')
        c0 += 32;

    switch (c0)
    {
    case 'p':
        if (p + 1 < end)
        {
            char c1 = *(p + 1);
            if (c1 >= 'A' && c1 <= 'Z')
                c1 += 32;
            if (c1 == 'r')
                return TAG_PRE;
        }
        return TAG_P;
    case 'l':
        if (p + 1 < end)
        {
            char c1 = *(p + 1);
            if (c1 >= 'A' && c1 <= 'Z')
                c1 += 32;
            if (c1 == 'i')
                return TAG_LI;
        }
        return TAG_UNKNOWN;
    case 'd':
        if (p + 1 < end)
        {
            char c1 = *(p + 1);
            if (c1 >= 'A' && c1 <= 'Z')
                c1 += 32;
            if (c1 == 't')
                return TAG_DT;
            if (c1 == 'd')
                return TAG_DD;
            if (c1 == 'i')
                return TAG_DIV;
            if (c1 == 'l')
                return TAG_DL;
        }
        return TAG_UNKNOWN;
    case 't':
        if (p + 1 < end)
        {
            char c1 = *(p + 1);
            if (c1 >= 'A' && c1 <= 'Z')
                c1 += 32;
            if (c1 == 'r')
                return TAG_TR;
            if (c1 == 'd')
                return TAG_TD;
            if (c1 == 'h')
                return TAG_TH;
        }
        return TAG_UNKNOWN;
    case 'h':
        if (p + 1 < end)
        {
            char c1 = *(p + 1);
            if (c1 >= 'A' && c1 <= 'Z')
                c1 += 32;
            if (c1 == 'r')
                return TAG_HR;
            if (c1 == '1')
                return TAG_H1;
            if (c1 == '2')
                return TAG_H2;
            if (c1 == '3')
                return TAG_H3;
            if (c1 == '4')
                return TAG_H4;
            if (c1 == '5')
                return TAG_H5;
            if (c1 == '6')
                return TAG_H6;
            if (starts_with_ci(p, end, "html"))
                return TAG_HTML;
        }
        return TAG_UNKNOWN;
    case 'u':
        if (p + 1 < end)
        {
            char c1 = *(p + 1);
            if (c1 >= 'A' && c1 <= 'Z')
                c1 += 32;
            if (c1 == 'l')
                return TAG_UL;
        }
        return TAG_UNKNOWN;
    case 'o':
        if (p + 1 < end)
        {
            char c1 = *(p + 1);
            if (c1 >= 'A' && c1 <= 'Z')
                c1 += 32;
            if (c1 == 'l')
                return TAG_OL;
        }
        return TAG_UNKNOWN;
    case 'b':
        if (starts_with_ci(p, end, "blockquote"))
            return TAG_BLOCKQUOTE;
        if (starts_with_ci(p, end, "body"))
            return TAG_BODY;
        return TAG_UNKNOWN;
    case 'a':
        if (starts_with_ci(p, end, "address"))
            return TAG_ADDRESS;
        return TAG_UNKNOWN;
    default:
        return TAG_UNKNOWN;
    }
}

/* ------------------------------------------------------------------ */
/* Implicit close logic                                                 */
/* ------------------------------------------------------------------ */
static void emit_synthetic_close(TokenArray *ta, TagId id)
{
    /* start=NULL, len=id encodes which tag was synthetically closed */
    ta_push(ta, TOK_SYNTHETIC_CLOSE, NULL, (size_t)id);
}

static int implicitly_closes(TagId opener, TagId stacked)
{
    switch (stacked)
    {
    case TAG_P:
        return (opener == TAG_P || opener == TAG_DIV ||
                opener == TAG_H1 || opener == TAG_H2 ||
                opener == TAG_H3 || opener == TAG_H4 ||
                opener == TAG_H5 || opener == TAG_H6 ||
                opener == TAG_UL || opener == TAG_OL ||
                opener == TAG_TABLE || opener == TAG_BLOCKQUOTE ||
                opener == TAG_PRE || opener == TAG_DL ||
                opener == TAG_HR || opener == TAG_ADDRESS);
    case TAG_LI:
        return (opener == TAG_LI);
    case TAG_DT:
        return (opener == TAG_DT || opener == TAG_DD);
    case TAG_DD:
        return (opener == TAG_DT || opener == TAG_DD);
    case TAG_TR:
        return (opener == TAG_TR);
    case TAG_TD:
    case TAG_TH:
        return (opener == TAG_TD || opener == TAG_TH);
    default:
        return 0;
    }
}

static int is_void_tag(TagId id)
{
    return (id == TAG_HR);
}

static void handle_implicit_closes(TokenArray *ta, ElemStack *es,
                                   TagId opener)
{
    while (es->top > 0)
    {
        TagId top = es_peek(es);
        if (implicitly_closes(opener, top))
        {
            es_pop(es);
            emit_synthetic_close(ta, top);
        }
        else
        {
            break;
        }
    }
}

static void handle_explicit_close(TokenArray *ta, ElemStack *es,
                                  TagId closer)
{
    while (es->top > 0)
    {
        TagId top = es_pop(es);
        if (top == closer)
            break;
        emit_synthetic_close(ta, top);
    }
}

/* ------------------------------------------------------------------ */
/* Public: html_tokenize                                                */
/* ------------------------------------------------------------------ */
TokenArray html_tokenize(const char *buf, size_t len)
{
    TokenArray ta;
    ta_init(&ta);

    ElemStack es;
    es_init(&es);

    const char *p = buf;
    const char *end = buf + len;

    while (p < end)
    {
        const char *text_start = p;
        p = simd_scan(p, end);

        if (p > text_start)
            ta_push(&ta, TOK_TEXT, text_start, (size_t)(p - text_start));

        if (p >= end)
            break;

        unsigned char c = (unsigned char)*p;

        if (c == '\r')
        {
            p++;
            continue;
        }
        if (c == '\0')
        {
            p++;
            continue;
        }

        if (c == '&')
        {
            const char *ent_start = p++;
            while (p < end && *p != ';' && *p != '<' &&
                   (p - ent_start) < 12)
                p++;
            if (p < end && *p == ';')
                p++;
            ta_push(&ta, TOK_ENTITY, ent_start, (size_t)(p - ent_start));
            continue;
        }

        if (c == '<')
        {
            /* COMMENT */
            if (p + 3 < end && p[1] == '!' && p[2] == '-' && p[3] == '-')
            {
                const char *cs = p;
                p += 4;
                const char *ce = find_str(p, end, "-->");
                p = ce ? ce + 3 : end;
                ta_push(&ta, TOK_COMMENT, cs, (size_t)(p - cs));
                continue;
            }

            /* CDATA */
            if (p + 8 < end && memcmp(p, "<![CDATA[", 9) == 0)
            {
                const char *cs = p;
                p += 9;
                const char *ce = find_str(p, end, "]]>");
                p = ce ? ce + 3 : end;
                ta_push(&ta, TOK_CDATA, cs, (size_t)(p - cs));
                continue;
            }

            /* DOCTYPE */
            if (p + 1 < end && p[1] == '!' &&
                starts_with_ci(p + 2, end, "doctype"))
            {
                const char *cs = p;
                const char *te = skip_to_tag_end(p + 1, end);
                if (te < end)
                    te++;
                p = te;
                ta_push(&ta, TOK_DOCTYPE, cs, (size_t)(p - cs));
                continue;
            }

            /* CLOSE TAG */
            if (p + 1 < end && p[1] == '/')
            {
                const char *cs = p;
                const char *te = skip_to_tag_end(p + 2, end);
                if (te < end)
                    te++;
                p = te;
                const char fc = *(cs + 2);
                TagId closer = (fc == 'p' || fc == 'l' || fc == 'd' || fc == 't' || fc == 'h' ||
                                fc == 'u' || fc == 'o' || fc == 'b' || fc == 'a' ||
                                fc == 'P' || fc == 'L' || fc == 'D' || fc == 'T' || fc == 'H' ||
                                fc == 'U' || fc == 'O' || fc == 'B' || fc == 'A')
                                   ? identify_tag(cs + 2, te)
                                   : TAG_UNKNOWN;
                if (closer != TAG_UNKNOWN)
                    handle_explicit_close(&ta, &es, closer);
                ta_push(&ta, TOK_CLOSE_TAG, cs, (size_t)(p - cs));
                continue;
            }

            /* SCRIPT — bounds-safe */
            if (p + 7 < end && starts_with_ci(p + 1, end, "script"))
            {
                char c7 = p[7];
                if (c7 == '>' || c7 == ' ' || c7 == '\t' ||
                    c7 == '\n' || c7 == '\r' || c7 == '/')
                {
                    const char *cs = p;
                    const char *te = skip_to_tag_end(p + 1, end);
                    if (te < end)
                        te++;
                    ta_push(&ta, TOK_OPEN_TAG, cs, (size_t)(te - cs));
                    p = te;
                    const char *body = p;
                    const char *ce = find_str(p, end, "</script");
                    if (!ce)
                        ce = end;
                    if (ce > body)
                        ta_push(&ta, TOK_SCRIPT, body, (size_t)(ce - body));
                    p = ce;
                    continue;
                }
            }

            /* STYLE — bounds-safe */
            if (p + 6 < end && starts_with_ci(p + 1, end, "style"))
            {
                char c6 = p[6];
                if (c6 == '>' || c6 == ' ' || c6 == '\t' ||
                    c6 == '\n' || c6 == '\r' || c6 == '/')
                {
                    const char *cs = p;
                    const char *te = skip_to_tag_end(p + 1, end);
                    if (te < end)
                        te++;
                    ta_push(&ta, TOK_OPEN_TAG, cs, (size_t)(te - cs));
                    p = te;
                    const char *body = p;
                    const char *ce = find_str(p, end, "</style");
                    if (!ce)
                        ce = end;
                    if (ce > body)
                        ta_push(&ta, TOK_STYLE, body, (size_t)(ce - body));
                    p = ce;
                    continue;
                }
            }

            /* OPEN TAG (possibly self-closing) */
            {
                const char *cs = p;
                const char *te = skip_to_tag_end(p + 1, end);
                int self_close = (te > p && te < end && *(te - 1) == '/');
                if (te < end)
                    te++;
                p = te;

                const char fc = *(cs + 1);
                TagId tid = (fc == 'p' || fc == 'l' || fc == 'd' || fc == 't' || fc == 'h' ||
                             fc == 'u' || fc == 'o' || fc == 'b' || fc == 'a' ||
                             fc == 'P' || fc == 'L' || fc == 'D' || fc == 'T' || fc == 'H' ||
                             fc == 'U' || fc == 'O' || fc == 'B' || fc == 'A')
                                ? identify_tag(cs + 1, te)
                                : TAG_UNKNOWN;

                if (!self_close && tid != TAG_UNKNOWN)
                {
                    handle_implicit_closes(&ta, &es, tid);
                    if (!is_void_tag(tid))
                        es_push(&es, tid);
                }

                ta_push(&ta,
                        self_close ? TOK_SELF_CLOSE_TAG : TOK_OPEN_TAG,
                        cs, (size_t)(p - cs));
                continue;
            }
        }

        p++;
    }

    es_free(&es);
    return ta;
}
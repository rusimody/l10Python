
/* Tokenizer implementation */

#include "Python.h"
#include "pgenheaders.h"

#include <ctype.h>
#include <assert.h>

#include "tokenizer.h"
#include "errcode.h"

#ifndef PGEN
#include "unicodeobject.h"
#include "bytesobject.h"
#include "fileobject.h"
#include "codecs.h"
#include "abstract.h"
#endif /* PGEN */

#define is_potential_identifier_start(c) (\
              (c >= 'a' && c <= 'z')\
               || (c >= 'A' && c <= 'Z')\
               || c == '_'\
               || (c >= 128))

#define is_potential_identifier_char(c) (\
              (c >= 'a' && c <= 'z')\
               || (c >= 'A' && c <= 'Z')\
               || (c >= '0' && c <= '9')\
               || c == '_'\
               || (c >= 128))

extern char *PyOS_Readline(FILE *, FILE *, const char *);
/* Return malloc'ed string including trailing \n;
   empty malloc'ed string for EOF;
   NULL if interrupted */

/* Don't ever change this -- it would break the portability of Python code */
#define TABSIZE 8

/* Forward */
static struct tok_state *tok_new(void);
static int tok_nextc(struct tok_state *tok);
static void tok_backup(struct tok_state *tok, int c);


/* Token names */

const char *_PyParser_TokenNames[] = {
    "ENDMARKER",
    "NAME",
    "NUMBER",
    "STRING",
    "NEWLINE",
    "INDENT",
    "DEDENT",
    "LPAR",
    "RPAR",
    "LSQB",
    "RSQB",
    "COLON",
    "COMMA",
    "SEMI",
    "PLUS",
    "MINUS",
    "STAR",
    "SLASH",
    "VBAR",
    "AMPER",
    "LESS",
    "GREATER",
    "EQUAL",
    "DOT",
    "PERCENT",
    "LBRACE",
    "RBRACE",
    "EQEQUAL",
    "NOTEQUAL",
    "LESSEQUAL",
    "GREATEREQUAL",
    "TILDE",
    "CIRCUMFLEX",
    "LEFTSHIFT",
    "RIGHTSHIFT",
    "DOUBLESTAR",
    "PLUSEQUAL",
    "MINEQUAL",
    "STAREQUAL",
    "SLASHEQUAL",
    "PERCENTEQUAL",
    "AMPEREQUAL",
    "VBAREQUAL",
    "CIRCUMFLEXEQUAL",
    "LEFTSHIFTEQUAL",
    "RIGHTSHIFTEQUAL",
    "DOUBLESTAREQUAL",
    "DOUBLESLASH",
    "DOUBLESLASHEQUAL",
    "AT",
    "ATEQUAL",
    "RARROW",
    "ELLIPSIS",
    /* This table must match the #defines in token.h! */
    "OP",
    "AWAIT",
    "ASYNC",
    "<ERRORTOKEN>",
    "<N_TOKENS>"
};


/* Create and initialize a new tok_state structure */

static struct tok_state *
tok_new(void)
{
    struct tok_state *tok = (struct tok_state *)PyMem_MALLOC(
                                            sizeof(struct tok_state));
    if (tok == NULL)
        return NULL;
    tok->buf = tok->cur = tok->end = tok->inp = tok->start = NULL;
    tok->done = E_OK;
    tok->fp = NULL;
    tok->input = NULL;
    tok->tabsize = TABSIZE;
    tok->indent = 0;
    tok->indstack[0] = 0;

    tok->def = 0;
    tok->defstack[0] = 0;
    tok->deftypestack[0] = 0;

    tok->atbol = 1;
    tok->pendin = 0;
    tok->prompt = tok->nextprompt = NULL;
    tok->lineno = 0;
    tok->level = 0;
    tok->altwarning = 1;
    tok->alterror = 1;
    tok->alttabsize = 1;
    tok->altindstack[0] = 0;
    tok->decoding_state = STATE_INIT;
    tok->decoding_erred = 0;
    tok->read_coding_spec = 0;
    tok->enc = NULL;
    tok->encoding = NULL;
    tok->cont_line = 0;
#ifndef PGEN
    tok->filename = NULL;
    tok->decoding_readline = NULL;
    tok->decoding_buffer = NULL;
#endif
    return tok;
}

static char *
new_string(const char *s, Py_ssize_t len, struct tok_state *tok)
{
    char* result = (char *)PyMem_MALLOC(len + 1);
    if (!result) {
        tok->done = E_NOMEM;
        return NULL;
    }
    memcpy(result, s, len);
    result[len] = '\0';
    return result;
}

#ifdef PGEN

static char *
decoding_fgets(char *s, int size, struct tok_state *tok)
{
    return fgets(s, size, tok->fp);
}

static int
decoding_feof(struct tok_state *tok)
{
    return feof(tok->fp);
}

static char *
decode_str(const char *str, int exec_input, struct tok_state *tok)
{
    return new_string(str, strlen(str), tok);
}

#else /* PGEN */

static char *
error_ret(struct tok_state *tok) /* XXX */
{
    tok->decoding_erred = 1;
    if (tok->fp != NULL && tok->buf != NULL) /* see PyTokenizer_Free */
        PyMem_FREE(tok->buf);
    tok->buf = NULL;
    return NULL;                /* as if it were EOF */
}


static char *
get_normal_name(char *s)        /* for utf-8 and latin-1 */
{
    char buf[13];
    int i;
    for (i = 0; i < 12; i++) {
        int c = s[i];
        if (c == '\0')
            break;
        else if (c == '_')
            buf[i] = '-';
        else
            buf[i] = tolower(c);
    }
    buf[i] = '\0';
    if (strcmp(buf, "utf-8") == 0 ||
        strncmp(buf, "utf-8-", 6) == 0)
        return "utf-8";
    else if (strcmp(buf, "latin-1") == 0 ||
             strcmp(buf, "iso-8859-1") == 0 ||
             strcmp(buf, "iso-latin-1") == 0 ||
             strncmp(buf, "latin-1-", 8) == 0 ||
             strncmp(buf, "iso-8859-1-", 11) == 0 ||
             strncmp(buf, "iso-latin-1-", 12) == 0)
        return "iso-8859-1";
    else
        return s;
}

/* Return the coding spec in S, or NULL if none is found.  */

static int
get_coding_spec(const char *s, char **spec, Py_ssize_t size, struct tok_state *tok)
{
    Py_ssize_t i;
    *spec = NULL;
    /* Coding spec must be in a comment, and that comment must be
     * the only statement on the source code line. */
    for (i = 0; i < size - 6; i++) {
        if (s[i] == '#')
            break;
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\014')
            return 1;
    }
    for (; i < size - 6; i++) { /* XXX inefficient search */
        const char* t = s + i;
        if (strncmp(t, "coding", 6) == 0) {
            const char* begin = NULL;
            t += 6;
            if (t[0] != ':' && t[0] != '=')
                continue;
            do {
                t++;
            } while (t[0] == '\x20' || t[0] == '\t');

            begin = t;
            while (Py_ISALNUM(t[0]) ||
                   t[0] == '-' || t[0] == '_' || t[0] == '.')
                t++;

            if (begin < t) {
                char* r = new_string(begin, t - begin, tok);
                char* q;
                if (!r)
                    return 0;
                q = get_normal_name(r);
                if (r != q) {
                    PyMem_FREE(r);
                    r = new_string(q, strlen(q), tok);
                    if (!r)
                        return 0;
                }
                *spec = r;
            }
        }
    }
    return 1;
}

/* Check whether the line contains a coding spec. If it does,
   invoke the set_readline function for the new encoding.
   This function receives the tok_state and the new encoding.
   Return 1 on success, 0 on failure.  */

static int
check_coding_spec(const char* line, Py_ssize_t size, struct tok_state *tok,
                  int set_readline(struct tok_state *, const char *))
{
    char *cs;
    int r = 1;

    if (tok->cont_line) {
        /* It's a continuation line, so it can't be a coding spec. */
        tok->read_coding_spec = 1;
        return 1;
    }
    if (!get_coding_spec(line, &cs, size, tok))
        return 0;
    if (!cs) {
        Py_ssize_t i;
        for (i = 0; i < size; i++) {
            if (line[i] == '#' || line[i] == '\n' || line[i] == '\r')
                break;
            if (line[i] != ' ' && line[i] != '\t' && line[i] != '\014') {
                /* Stop checking coding spec after a line containing
                 * anything except a comment. */
                tok->read_coding_spec = 1;
                break;
            }
        }
        return 1;
    }
    tok->read_coding_spec = 1;
    if (tok->encoding == NULL) {
        assert(tok->decoding_state == STATE_RAW);
        if (strcmp(cs, "utf-8") == 0) {
            tok->encoding = cs;
        } else {
            r = set_readline(tok, cs);
            if (r) {
                tok->encoding = cs;
                tok->decoding_state = STATE_NORMAL;
            }
            else {
                PyErr_Format(PyExc_SyntaxError,
                             "encoding problem: %s", cs);
                PyMem_FREE(cs);
            }
        }
    } else {                /* then, compare cs with BOM */
        r = (strcmp(tok->encoding, cs) == 0);
        if (!r)
            PyErr_Format(PyExc_SyntaxError,
                         "encoding problem: %s with BOM", cs);
        PyMem_FREE(cs);
    }
    return r;
}

/* See whether the file starts with a BOM. If it does,
   invoke the set_readline function with the new encoding.
   Return 1 on success, 0 on failure.  */

static int
check_bom(int get_char(struct tok_state *),
          void unget_char(int, struct tok_state *),
          int set_readline(struct tok_state *, const char *),
          struct tok_state *tok)
{
    int ch1, ch2, ch3;
    ch1 = get_char(tok);
    tok->decoding_state = STATE_RAW;
    if (ch1 == EOF) {
        return 1;
    } else if (ch1 == 0xEF) {
        ch2 = get_char(tok);
        if (ch2 != 0xBB) {
            unget_char(ch2, tok);
            unget_char(ch1, tok);
            return 1;
        }
        ch3 = get_char(tok);
        if (ch3 != 0xBF) {
            unget_char(ch3, tok);
            unget_char(ch2, tok);
            unget_char(ch1, tok);
            return 1;
        }
#if 0
    /* Disable support for UTF-16 BOMs until a decision
       is made whether this needs to be supported.  */
    } else if (ch1 == 0xFE) {
        ch2 = get_char(tok);
        if (ch2 != 0xFF) {
            unget_char(ch2, tok);
            unget_char(ch1, tok);
            return 1;
        }
        if (!set_readline(tok, "utf-16-be"))
            return 0;
        tok->decoding_state = STATE_NORMAL;
    } else if (ch1 == 0xFF) {
        ch2 = get_char(tok);
        if (ch2 != 0xFE) {
            unget_char(ch2, tok);
            unget_char(ch1, tok);
            return 1;
        }
        if (!set_readline(tok, "utf-16-le"))
            return 0;
        tok->decoding_state = STATE_NORMAL;
#endif
    } else {
        unget_char(ch1, tok);
        return 1;
    }
    if (tok->encoding != NULL)
        PyMem_FREE(tok->encoding);
    tok->encoding = new_string("utf-8", 5, tok);
    if (!tok->encoding)
        return 0;
    /* No need to set_readline: input is already utf-8 */
    return 1;
}

/* Read a line of text from TOK into S, using the stream in TOK.
   Return NULL on failure, else S.

   On entry, tok->decoding_buffer will be one of:
     1) NULL: need to call tok->decoding_readline to get a new line
     2) PyUnicodeObject *: decoding_feof has called tok->decoding_readline and
       stored the result in tok->decoding_buffer
     3) PyByteArrayObject *: previous call to fp_readl did not have enough room
       (in the s buffer) to copy entire contents of the line read
       by tok->decoding_readline.  tok->decoding_buffer has the overflow.
       In this case, fp_readl is called in a loop (with an expanded buffer)
       until the buffer ends with a '\n' (or until the end of the file is
       reached): see tok_nextc and its calls to decoding_fgets.
*/

static char *
fp_readl(char *s, int size, struct tok_state *tok)
{
    PyObject* bufobj;
    const char *buf;
    Py_ssize_t buflen;

    /* Ask for one less byte so we can terminate it */
    assert(size > 0);
    size--;

    if (tok->decoding_buffer) {
        bufobj = tok->decoding_buffer;
        Py_INCREF(bufobj);
    }
    else
    {
        bufobj = PyObject_CallObject(tok->decoding_readline, NULL);
        if (bufobj == NULL)
            goto error;
    }
    if (PyUnicode_CheckExact(bufobj))
    {
        buf = _PyUnicode_AsStringAndSize(bufobj, &buflen);
        if (buf == NULL) {
            goto error;
        }
    }
    else
    {
        buf = PyByteArray_AsString(bufobj);
        if (buf == NULL) {
            goto error;
        }
        buflen = PyByteArray_GET_SIZE(bufobj);
    }

    Py_XDECREF(tok->decoding_buffer);
    if (buflen > size) {
        /* Too many chars, the rest goes into tok->decoding_buffer */
        tok->decoding_buffer = PyByteArray_FromStringAndSize(buf+size,
                                                         buflen-size);
        if (tok->decoding_buffer == NULL)
            goto error;
        buflen = size;
    }
    else
        tok->decoding_buffer = NULL;

    memcpy(s, buf, buflen);
    s[buflen] = '\0';
    if (buflen == 0) /* EOF */
        s = NULL;
    Py_DECREF(bufobj);
    return s;

error:
    Py_XDECREF(bufobj);
    return error_ret(tok);
}

/* Set the readline function for TOK to a StreamReader's
   readline function. The StreamReader is named ENC.

   This function is called from check_bom and check_coding_spec.

   ENC is usually identical to the future value of tok->encoding,
   except for the (currently unsupported) case of UTF-16.

   Return 1 on success, 0 on failure. */

static int
fp_setreadl(struct tok_state *tok, const char* enc)
{
    PyObject *readline = NULL, *stream = NULL, *io = NULL;
    _Py_IDENTIFIER(open);
    _Py_IDENTIFIER(readline);
    int fd;
    long pos;

    io = PyImport_ImportModuleNoBlock("io");
    if (io == NULL)
        goto cleanup;

    fd = fileno(tok->fp);
    /* Due to buffering the file offset for fd can be different from the file
     * position of tok->fp.  If tok->fp was opened in text mode on Windows,
     * its file position counts CRLF as one char and can't be directly mapped
     * to the file offset for fd.  Instead we step back one byte and read to
     * the end of line.*/
    pos = ftell(tok->fp);
    if (pos == -1 ||
        lseek(fd, (off_t)(pos > 0 ? pos - 1 : pos), SEEK_SET) == (off_t)-1) {
        PyErr_SetFromErrnoWithFilename(PyExc_OSError, NULL);
        goto cleanup;
    }

    stream = _PyObject_CallMethodId(io, &PyId_open, "isisOOO",
                    fd, "r", -1, enc, Py_None, Py_None, Py_False);
    if (stream == NULL)
        goto cleanup;

    Py_XDECREF(tok->decoding_readline);
    readline = _PyObject_GetAttrId(stream, &PyId_readline);
    tok->decoding_readline = readline;
    if (pos > 0) {
        if (PyObject_CallObject(readline, NULL) == NULL) {
            readline = NULL;
            goto cleanup;
        }
    }

  cleanup:
    Py_XDECREF(stream);
    Py_XDECREF(io);
    return readline != NULL;
}

/* Fetch the next byte from TOK. */

static int fp_getc(struct tok_state *tok) {
    return getc(tok->fp);
}

/* Unfetch the last byte back into TOK.  */

static void fp_ungetc(int c, struct tok_state *tok) {
    ungetc(c, tok->fp);
}

/* Check whether the characters at s start a valid
   UTF-8 sequence. Return the number of characters forming
   the sequence if yes, 0 if not.  */
static int valid_utf8(const unsigned char* s)
{
    int expected = 0;
    int length;
    if (*s < 0x80)
        /* single-byte code */
        return 1;
    if (*s < 0xc0)
        /* following byte */
        return 0;
    if (*s < 0xE0)
        expected = 1;
    else if (*s < 0xF0)
        expected = 2;
    else if (*s < 0xF8)
        expected = 3;
    else
        return 0;
    length = expected + 1;
    for (; expected; expected--)
        if (s[expected] < 0x80 || s[expected] >= 0xC0)
            return 0;
    return length;
}

/* Read a line of input from TOK. Determine encoding
   if necessary.  */

static char *
decoding_fgets(char *s, int size, struct tok_state *tok)
{
    char *line = NULL;
    int badchar = 0;
    for (;;) {
        if (tok->decoding_state == STATE_NORMAL) {
            /* We already have a codec associated with
               this input. */
            line = fp_readl(s, size, tok);
            break;
        } else if (tok->decoding_state == STATE_RAW) {
            /* We want a 'raw' read. */
            line = Py_UniversalNewlineFgets(s, size,
                                            tok->fp, NULL);
            break;
        } else {
            /* We have not yet determined the encoding.
               If an encoding is found, use the file-pointer
               reader functions from now on. */
            if (!check_bom(fp_getc, fp_ungetc, fp_setreadl, tok))
                return error_ret(tok);
            assert(tok->decoding_state != STATE_INIT);
        }
    }
    if (line != NULL && tok->lineno < 2 && !tok->read_coding_spec) {
        if (!check_coding_spec(line, strlen(line), tok, fp_setreadl)) {
            return error_ret(tok);
        }
    }
#ifndef PGEN
    /* The default encoding is UTF-8, so make sure we don't have any
       non-UTF-8 sequences in it. */
    if (line && !tok->encoding) {
        unsigned char *c;
        int length;
        for (c = (unsigned char *)line; *c; c += length)
            if (!(length = valid_utf8(c))) {
                badchar = *c;
                break;
            }
    }
    if (badchar) {
        /* Need to add 1 to the line number, since this line
           has not been counted, yet.  */
        PyErr_Format(PyExc_SyntaxError,
                "Non-UTF-8 code starting with '\\x%.2x' "
                "in file %U on line %i, "
                "but no encoding declared; "
                "see http://python.org/dev/peps/pep-0263/ for details",
                badchar, tok->filename, tok->lineno + 1);
        return error_ret(tok);
    }
#endif
    return line;
}

static int
decoding_feof(struct tok_state *tok)
{
    if (tok->decoding_state != STATE_NORMAL) {
        return feof(tok->fp);
    } else {
        PyObject* buf = tok->decoding_buffer;
        if (buf == NULL) {
            buf = PyObject_CallObject(tok->decoding_readline, NULL);
            if (buf == NULL) {
                error_ret(tok);
                return 1;
            } else {
                tok->decoding_buffer = buf;
            }
        }
        return PyObject_Length(buf) == 0;
    }
}

/* Fetch a byte from TOK, using the string buffer. */

static int
buf_getc(struct tok_state *tok) {
    return Py_CHARMASK(*tok->str++);
}

/* Unfetch a byte from TOK, using the string buffer. */

static void
buf_ungetc(int c, struct tok_state *tok) {
    tok->str--;
    assert(Py_CHARMASK(*tok->str) == c);        /* tok->cur may point to read-only segment */
}

/* Set the readline function for TOK to ENC. For the string-based
   tokenizer, this means to just record the encoding. */

static int
buf_setreadl(struct tok_state *tok, const char* enc) {
    tok->enc = enc;
    return 1;
}

/* Return a UTF-8 encoding Python string object from the
   C byte string STR, which is encoded with ENC. */

static PyObject *
translate_into_utf8(const char* str, const char* enc) {
    PyObject *utf8;
    PyObject* buf = PyUnicode_Decode(str, strlen(str), enc, NULL);
    if (buf == NULL)
        return NULL;
    utf8 = PyUnicode_AsUTF8String(buf);
    Py_DECREF(buf);
    return utf8;
}


static char *
translate_newlines(const char *s, int exec_input, struct tok_state *tok) {
    int skip_next_lf = 0;
    size_t needed_length = strlen(s) + 2, final_length;
    char *buf, *current;
    char c = '\0';
    buf = PyMem_MALLOC(needed_length);
    if (buf == NULL) {
        tok->done = E_NOMEM;
        return NULL;
    }
    for (current = buf; *s; s++, current++) {
        c = *s;
        if (skip_next_lf) {
            skip_next_lf = 0;
            if (c == '\n') {
                c = *++s;
                if (!c)
                    break;
            }
        }
        if (c == '\r') {
            skip_next_lf = 1;
            c = '\n';
        }
        *current = c;
    }
    /* If this is exec input, add a newline to the end of the string if
       there isn't one already. */
    if (exec_input && c != '\n') {
        *current = '\n';
        current++;
    }
    *current = '\0';
    final_length = current - buf + 1;
    if (final_length < needed_length && final_length)
        /* should never fail */
        buf = PyMem_REALLOC(buf, final_length);
    return buf;
}

/* Decode a byte string STR for use as the buffer of TOK.
   Look for encoding declarations inside STR, and record them
   inside TOK.  */

static const char *
decode_str(const char *input, int single, struct tok_state *tok)
{
    PyObject* utf8 = NULL;
    const char *str;
    const char *s;
    const char *newl[2] = {NULL, NULL};
    int lineno = 0;
    tok->input = str = translate_newlines(input, single, tok);
    if (str == NULL)
        return NULL;
    tok->enc = NULL;
    tok->str = str;
    if (!check_bom(buf_getc, buf_ungetc, buf_setreadl, tok))
        return error_ret(tok);
    str = tok->str;             /* string after BOM if any */
    assert(str);
    if (tok->enc != NULL) {
        utf8 = translate_into_utf8(str, tok->enc);
        if (utf8 == NULL)
            return error_ret(tok);
        str = PyBytes_AsString(utf8);
    }
    for (s = str;; s++) {
        if (*s == '\0') break;
        else if (*s == '\n') {
            assert(lineno < 2);
            newl[lineno] = s;
            lineno++;
            if (lineno == 2) break;
        }
    }
    tok->enc = NULL;
    /* need to check line 1 and 2 separately since check_coding_spec
       assumes a single line as input */
    if (newl[0]) {
        if (!check_coding_spec(str, newl[0] - str, tok, buf_setreadl))
            return error_ret(tok);
        if (tok->enc == NULL && !tok->read_coding_spec && newl[1]) {
            if (!check_coding_spec(newl[0]+1, newl[1] - newl[0],
                                   tok, buf_setreadl))
                return error_ret(tok);
        }
    }
    if (tok->enc != NULL) {
        assert(utf8 == NULL);
        utf8 = translate_into_utf8(str, tok->enc);
        if (utf8 == NULL)
            return error_ret(tok);
        str = PyBytes_AS_STRING(utf8);
    }
    assert(tok->decoding_buffer == NULL);
    tok->decoding_buffer = utf8; /* CAUTION */
    return str;
}

#endif /* PGEN */

/* Set up tokenizer for string */

struct tok_state *
PyTokenizer_FromString(const char *str, int exec_input)
{
    struct tok_state *tok = tok_new();
    if (tok == NULL)
        return NULL;
    str = decode_str(str, exec_input, tok);
    if (str == NULL) {
        PyTokenizer_Free(tok);
        return NULL;
    }

    /* XXX: constify members. */
    tok->buf = tok->cur = tok->end = tok->inp = (char*)str;
    return tok;
}

struct tok_state *
PyTokenizer_FromUTF8(const char *str, int exec_input)
{
    struct tok_state *tok = tok_new();
    if (tok == NULL)
        return NULL;
#ifndef PGEN
    tok->input = str = translate_newlines(str, exec_input, tok);
#endif
    if (str == NULL) {
        PyTokenizer_Free(tok);
        return NULL;
    }
    tok->decoding_state = STATE_RAW;
    tok->read_coding_spec = 1;
    tok->enc = NULL;
    tok->str = str;
    tok->encoding = (char *)PyMem_MALLOC(6);
    if (!tok->encoding) {
        PyTokenizer_Free(tok);
        return NULL;
    }
    strcpy(tok->encoding, "utf-8");

    /* XXX: constify members. */
    tok->buf = tok->cur = tok->end = tok->inp = (char*)str;
    return tok;
}

/* Set up tokenizer for file */

struct tok_state *
PyTokenizer_FromFile(FILE *fp, const char* enc,
                     const char *ps1, const char *ps2)
{
    struct tok_state *tok = tok_new();
    if (tok == NULL)
        return NULL;
    if ((tok->buf = (char *)PyMem_MALLOC(BUFSIZ)) == NULL) {
        PyTokenizer_Free(tok);
        return NULL;
    }
    tok->cur = tok->inp = tok->buf;
    tok->end = tok->buf + BUFSIZ;
    tok->fp = fp;
    tok->prompt = ps1;
    tok->nextprompt = ps2;
    if (enc != NULL) {
        /* Must copy encoding declaration since it
           gets copied into the parse tree. */
        tok->encoding = PyMem_MALLOC(strlen(enc)+1);
        if (!tok->encoding) {
            PyTokenizer_Free(tok);
            return NULL;
        }
        strcpy(tok->encoding, enc);
        tok->decoding_state = STATE_NORMAL;
    }
    return tok;
}


/* Free a tok_state structure */

void
PyTokenizer_Free(struct tok_state *tok)
{
    if (tok->encoding != NULL)
        PyMem_FREE(tok->encoding);
#ifndef PGEN
    Py_XDECREF(tok->decoding_readline);
    Py_XDECREF(tok->decoding_buffer);
    Py_XDECREF(tok->filename);
#endif
    if (tok->fp != NULL && tok->buf != NULL)
        PyMem_FREE(tok->buf);
    if (tok->input)
        PyMem_FREE((char *)tok->input);
    PyMem_FREE(tok);
}

/* Get next char, updating state; error code goes into tok->done */

static int
tok_nextc(struct tok_state *tok)
{
    for (;;) {
        if (tok->cur != tok->inp) {
            return Py_CHARMASK(*tok->cur++); /* Fast path */
        }
        if (tok->done != E_OK)
            return EOF;
        if (tok->fp == NULL) {
            char *end = strchr(tok->inp, '\n');
            if (end != NULL)
                end++;
            else {
                end = strchr(tok->inp, '\0');
                if (end == tok->inp) {
                    tok->done = E_EOF;
                    return EOF;
                }
            }
            if (tok->start == NULL)
                tok->buf = tok->cur;
            tok->line_start = tok->cur;
            tok->lineno++;
            tok->inp = end;
            return Py_CHARMASK(*tok->cur++);
        }
        if (tok->prompt != NULL) {
            char *newtok = PyOS_Readline(stdin, stdout, tok->prompt);
#ifndef PGEN
            if (newtok != NULL) {
                char *translated = translate_newlines(newtok, 0, tok);
                PyMem_FREE(newtok);
                if (translated == NULL)
                    return EOF;
                newtok = translated;
            }
            if (tok->encoding && newtok && *newtok) {
                /* Recode to UTF-8 */
                Py_ssize_t buflen;
                const char* buf;
                PyObject *u = translate_into_utf8(newtok, tok->encoding);
                PyMem_FREE(newtok);
                if (!u) {
                    tok->done = E_DECODE;
                    return EOF;
                }
                buflen = PyBytes_GET_SIZE(u);
                buf = PyBytes_AS_STRING(u);
                if (!buf) {
                    Py_DECREF(u);
                    tok->done = E_DECODE;
                    return EOF;
                }
                newtok = PyMem_MALLOC(buflen+1);
                strcpy(newtok, buf);
                Py_DECREF(u);
            }
#endif
            if (tok->nextprompt != NULL)
                tok->prompt = tok->nextprompt;
            if (newtok == NULL)
                tok->done = E_INTR;
            else if (*newtok == '\0') {
                PyMem_FREE(newtok);
                tok->done = E_EOF;
            }
            else if (tok->start != NULL) {
                size_t start = tok->start - tok->buf;
                size_t oldlen = tok->cur - tok->buf;
                size_t newlen = oldlen + strlen(newtok);
                char *buf = tok->buf;
                buf = (char *)PyMem_REALLOC(buf, newlen+1);
                tok->lineno++;
                if (buf == NULL) {
                    PyMem_FREE(tok->buf);
                    tok->buf = NULL;
                    PyMem_FREE(newtok);
                    tok->done = E_NOMEM;
                    return EOF;
                }
                tok->buf = buf;
                tok->cur = tok->buf + oldlen;
                tok->line_start = tok->cur;
                strcpy(tok->buf + oldlen, newtok);
                PyMem_FREE(newtok);
                tok->inp = tok->buf + newlen;
                tok->end = tok->inp + 1;
                tok->start = tok->buf + start;
            }
            else {
                tok->lineno++;
                if (tok->buf != NULL)
                    PyMem_FREE(tok->buf);
                tok->buf = newtok;
                tok->line_start = tok->buf;
                tok->cur = tok->buf;
                tok->line_start = tok->buf;
                tok->inp = strchr(tok->buf, '\0');
                tok->end = tok->inp + 1;
            }
        }
        else {
            int done = 0;
            Py_ssize_t cur = 0;
            char *pt;
            if (tok->start == NULL) {
                if (tok->buf == NULL) {
                    tok->buf = (char *)
                        PyMem_MALLOC(BUFSIZ);
                    if (tok->buf == NULL) {
                        tok->done = E_NOMEM;
                        return EOF;
                    }
                    tok->end = tok->buf + BUFSIZ;
                }
                if (decoding_fgets(tok->buf, (int)(tok->end - tok->buf),
                          tok) == NULL) {
                    tok->done = E_EOF;
                    done = 1;
                }
                else {
                    tok->done = E_OK;
                    tok->inp = strchr(tok->buf, '\0');
                    done = tok->inp[-1] == '\n';
                }
            }
            else {
                cur = tok->cur - tok->buf;
                if (decoding_feof(tok)) {
                    tok->done = E_EOF;
                    done = 1;
                }
                else
                    tok->done = E_OK;
            }
            tok->lineno++;
            /* Read until '\n' or EOF */
            while (!done) {
                Py_ssize_t curstart = tok->start == NULL ? -1 :
                          tok->start - tok->buf;
                Py_ssize_t curvalid = tok->inp - tok->buf;
                Py_ssize_t newsize = curvalid + BUFSIZ;
                char *newbuf = tok->buf;
                newbuf = (char *)PyMem_REALLOC(newbuf,
                                               newsize);
                if (newbuf == NULL) {
                    tok->done = E_NOMEM;
                    tok->cur = tok->inp;
                    return EOF;
                }
                tok->buf = newbuf;
                tok->inp = tok->buf + curvalid;
                tok->end = tok->buf + newsize;
                tok->start = curstart < 0 ? NULL :
                         tok->buf + curstart;
                if (decoding_fgets(tok->inp,
                               (int)(tok->end - tok->inp),
                               tok) == NULL) {
                    /* Break out early on decoding
                       errors, as tok->buf will be NULL
                     */
                    if (tok->decoding_erred)
                        return EOF;
                    /* Last line does not end in \n,
                       fake one */
                    strcpy(tok->inp, "\n");
                }
                tok->inp = strchr(tok->inp, '\0');
                done = tok->inp[-1] == '\n';
            }
            if (tok->buf != NULL) {
                tok->cur = tok->buf + cur;
                tok->line_start = tok->cur;
                /* replace "\r\n" with "\n" */
                /* For Mac leave the \r, giving a syntax error */
                pt = tok->inp - 2;
                if (pt >= tok->buf && *pt == '\r') {
                    *pt++ = '\n';
                    *pt = '\0';
                    tok->inp = pt;
                }
            }
        }
        if (tok->done != E_OK) {
            if (tok->prompt != NULL)
                PySys_WriteStderr("\n");
            tok->cur = tok->inp;
            return EOF;
        }
    }
    /*NOTREACHED*/
}


/* Back-up one character */

static void
tok_backup(struct tok_state *tok, int c)
{
    if (c != EOF) {
        if (--tok->cur < tok->buf)
            Py_FatalError("tok_backup: beginning of buffer");
        if (*tok->cur != c)
            *tok->cur = c;
    }
}


/* Return the token corresponding to a single character */

int
PyToken_OneChar(int c)
{
    switch (c) {
    case '(':           return LPAR;
    case ')':           return RPAR;
    case '[':           return LSQB;
    case ']':           return RSQB;
    case ':':           return COLON;
    case ',':           return COMMA;
    case ';':           return SEMI;
    case '+':           return PLUS;
    case '-':           return MINUS;
    case '*':           return STAR;
    case '/':           return SLASH;
    case '|':           return VBAR;
    case '&':           return AMPER;
    case '<':           return LESS;
    case '>':           return GREATER;
    case '=':           return EQUAL;
    case '.':           return DOT;
    case '%':           return PERCENT;
    case '{':           return LBRACE;
    case '}':           return RBRACE;
    case '^':           return CIRCUMFLEX;
    case '~':           return TILDE;
    case '@':           return AT;
    default:            return OP;
    }
}


int
PyToken_TwoChars(int c1, int c2)
{
    switch (c1) {
    case '=':
        switch (c2) {
        case '=':               return EQEQUAL;
        }
        break;
    case '!':
        switch (c2) {
        case '=':               return NOTEQUAL;
        }
        break;
    case '<':
        switch (c2) {
        case '>':               return NOTEQUAL;
        case '=':               return LESSEQUAL;
        case '<':               return LEFTSHIFT;
        }
        break;
    case '>':
        switch (c2) {
        case '=':               return GREATEREQUAL;
        case '>':               return RIGHTSHIFT;
        }
        break;
    case '+':
        switch (c2) {
        case '=':               return PLUSEQUAL;
        }
        break;
    case '-':
        switch (c2) {
        case '=':               return MINEQUAL;
        case '>':               return RARROW;
        }
        break;
    case '*':
        switch (c2) {
        case '*':               return DOUBLESTAR;
        case '=':               return STAREQUAL;
        }
        break;
    case '/':
        switch (c2) {
        case '/':               return DOUBLESLASH;
        case '=':               return SLASHEQUAL;
        }
        break;
    case '|':
        switch (c2) {
        case '=':               return VBAREQUAL;
        }
        break;
    case '%':
        switch (c2) {
        case '=':               return PERCENTEQUAL;
        }
        break;
    case '&':
        switch (c2) {
        case '=':               return AMPEREQUAL;
        }
        break;
    case '^':
        switch (c2) {
        case '=':               return CIRCUMFLEXEQUAL;
        }
        break;
    case '@':
        switch (c2) {
        case '=':               return ATEQUAL;
        }
        break;
    }
    return OP;
}

int
PyToken_ThreeChars(int c1, int c2, int c3)
{
    switch (c1) {
    case '<':
        switch (c2) {
        case '<':
            switch (c3) {
            case '=':
                return LEFTSHIFTEQUAL;
            }
            break;
        }
        break;
    case '>':
        switch (c2) {
        case '>':
            switch (c3) {
            case '=':
                return RIGHTSHIFTEQUAL;
            }
            break;
        }
        break;
    case '*':
        switch (c2) {
        case '*':
            switch (c3) {
            case '=':
                return DOUBLESTAREQUAL;
            }
            break;
        }
        break;
    case '/':
        switch (c2) {
        case '/':
            switch (c3) {
            case '=':
                return DOUBLESLASHEQUAL;
            }
            break;
        }
        break;
    case '.':
        switch (c2) {
        case '.':
            switch (c3) {
            case '.':
                return ELLIPSIS;
            }
            break;
        }
        break;
    }
    return OP;
}

static int
indenterror(struct tok_state *tok)
{
    if (tok->alterror) {
        tok->done = E_TABSPACE;
        tok->cur = tok->inp;
        return 1;
    }
    if (tok->altwarning) {
#ifdef PGEN
        PySys_WriteStderr("inconsistent use of tabs and spaces "
                          "in indentation\n");
#else
        PySys_FormatStderr("%U: inconsistent use of tabs and spaces "
                          "in indentation\n", tok->filename);
#endif
        tok->altwarning = 0;
    }
    return 0;
}

#ifdef PGEN
#define verify_identifier(tok) 1
#else
/* Verify that the identifier follows PEP 3131.
   All identifier strings are guaranteed to be "ready" unicode objects.
 */
static int
verify_identifier(struct tok_state *tok)
{
    PyObject *s;
    int result;
    if (tok->decoding_erred)
        return 0;
    s = PyUnicode_DecodeUTF8(tok->start, tok->cur - tok->start, NULL);
    if (s == NULL || PyUnicode_READY(s) == -1) {
        if (PyErr_ExceptionMatches(PyExc_UnicodeDecodeError)) {
            PyErr_Clear();
            tok->done = E_IDENTIFIER;
        } else {
            tok->done = E_ERROR;
        }
        return 0;
    }
    result = PyUnicode_IsIdentifier(s);
    Py_DECREF(s);
    if (result == 0)
        tok->done = E_IDENTIFIER;
    return result;
}
#endif

/* Get next token, after space stripping etc. */



static double uni2Num(int ch)
{
    switch (ch) {
    case 0x0F33:
        return (double) -1.0/2.0;
    case 0x0030:
    case 0x0660:
    case 0x06F0:
    case 0x07C0:
    case 0x0966:
    case 0x09E6:
    case 0x0A66:
    case 0x0AE6:
    case 0x0B66:
    case 0x0BE6:
    case 0x0C66:
    case 0x0C78:
    case 0x0CE6:
    case 0x0D66:
    case 0x0DE6:
    case 0x0E50:
    case 0x0ED0:
    case 0x0F20:
    case 0x1040:
    case 0x1090:
    case 0x17E0:
    case 0x17F0:
    case 0x1810:
    case 0x1946:
    case 0x19D0:
    case 0x1A80:
    case 0x1A90:
    case 0x1B50:
    case 0x1BB0:
    case 0x1C40:
    case 0x1C50:
    case 0x2070:
    case 0x2080:
    case 0x2189:
    case 0x24EA:
    case 0x24FF:
    case 0x3007:
    case 0x96F6:
    case 0xA620:
    case 0xA6EF:
    case 0xA8D0:
    case 0xA900:
    case 0xA9D0:
    case 0xA9F0:
    case 0xAA50:
    case 0xABF0:
    case 0xF9B2:
    case 0xFF10:
    case 0x1018A:
    case 0x104A0:
    case 0x11066:
    case 0x110F0:
    case 0x11136:
    case 0x111D0:
    case 0x112F0:
    case 0x114D0:
    case 0x11650:
    case 0x116C0:
    case 0x118E0:
    case 0x16A60:
    case 0x16B50:
    case 0x1D7CE:
    case 0x1D7D8:
    case 0x1D7E2:
    case 0x1D7EC:
    case 0x1D7F6:
    case 0x1F100:
    case 0x1F101:
    case 0x1F10B:
    case 0x1F10C:
        return (double) 0.0;
    case 0x0031:
    case 0x00B9:
    case 0x0661:
    case 0x06F1:
    case 0x07C1:
    case 0x0967:
    case 0x09E7:
    case 0x0A67:
    case 0x0AE7:
    case 0x0B67:
    case 0x0BE7:
    case 0x0C67:
    case 0x0C79:
    case 0x0C7C:
    case 0x0CE7:
    case 0x0D67:
    case 0x0DE7:
    case 0x0E51:
    case 0x0ED1:
    case 0x0F21:
    case 0x1041:
    case 0x1091:
    case 0x1369:
    case 0x17E1:
    case 0x17F1:
    case 0x1811:
    case 0x1947:
    case 0x19D1:
    case 0x19DA:
    case 0x1A81:
    case 0x1A91:
    case 0x1B51:
    case 0x1BB1:
    case 0x1C41:
    case 0x1C51:
    case 0x2081:
    case 0x215F:
    case 0x2160:
    case 0x2170:
    case 0x2460:
    case 0x2474:
    case 0x2488:
    case 0x24F5:
    case 0x2776:
    case 0x2780:
    case 0x278A:
    case 0x3021:
    case 0x3192:
    case 0x3220:
    case 0x3280:
    case 0x4E00:
    case 0x58F1:
    case 0x58F9:
    case 0x5E7A:
    case 0x5F0C:
    case 0xA621:
    case 0xA6E6:
    case 0xA8D1:
    case 0xA901:
    case 0xA9D1:
    case 0xA9F1:
    case 0xAA51:
    case 0xABF1:
    case 0xFF11:
    case 0x10107:
    case 0x10142:
    case 0x10158:
    case 0x10159:
    case 0x1015A:
    case 0x102E1:
    case 0x10320:
    case 0x103D1:
    case 0x104A1:
    case 0x10858:
    case 0x10879:
    case 0x108A7:
    case 0x10916:
    case 0x10A40:
    case 0x10A7D:
    case 0x10A9D:
    case 0x10AEB:
    case 0x10B58:
    case 0x10B78:
    case 0x10BA9:
    case 0x10E60:
    case 0x11052:
    case 0x11067:
    case 0x110F1:
    case 0x11137:
    case 0x111D1:
    case 0x111E1:
    case 0x112F1:
    case 0x114D1:
    case 0x11651:
    case 0x116C1:
    case 0x118E1:
    case 0x12415:
    case 0x1241E:
    case 0x1242C:
    case 0x12434:
    case 0x1244F:
    case 0x12458:
    case 0x16A61:
    case 0x16B51:
    case 0x1D360:
    case 0x1D7CF:
    case 0x1D7D9:
    case 0x1D7E3:
    case 0x1D7ED:
    case 0x1D7F7:
    case 0x1E8C7:
    case 0x1F102:
    case 0x2092A:
        return (double) 1.0;
    case 0x2152:
        return (double) 1.0/10.0;
    case 0x09F4:
    case 0x0B75:
    case 0xA833:
        return (double) 1.0/16.0;
    case 0x00BD:
    case 0x0B73:
    case 0x0D74:
    case 0x0F2A:
    case 0x2CFD:
    case 0xA831:
    case 0x10141:
    case 0x10175:
    case 0x10176:
    case 0x10E7B:
    case 0x12464:
        return (double) 1.0/2.0;
    case 0x2153:
    case 0x10E7D:
    case 0x1245A:
    case 0x1245D:
    case 0x12465:
        return (double) 1.0/3.0;
    case 0x00BC:
    case 0x09F7:
    case 0x0B72:
    case 0x0D73:
    case 0xA830:
    case 0x10140:
    case 0x1018B:
    case 0x10E7C:
    case 0x12460:
    case 0x12462:
    case 0x12463:
        return (double) 1.0/4.0;
    case 0x2155:
        return (double) 1.0/5.0;
    case 0x2159:
    case 0x12461:
        return (double) 1.0/6.0;
    case 0x2150:
        return (double) 1.0/7.0;
    case 0x09F5:
    case 0x0B76:
    case 0x215B:
    case 0xA834:
    case 0x1245F:
        return (double) 1.0/8.0;
    case 0x2151:
        return (double) 1.0/9.0;
    case 0x0BF0:
    case 0x0D70:
    case 0x1372:
    case 0x2169:
    case 0x2179:
    case 0x2469:
    case 0x247D:
    case 0x2491:
    case 0x24FE:
    case 0x277F:
    case 0x2789:
    case 0x2793:
    case 0x3038:
    case 0x3229:
    case 0x3248:
    case 0x3289:
    case 0x4EC0:
    case 0x5341:
    case 0x62FE:
    case 0xF973:
    case 0xF9FD:
    case 0x10110:
    case 0x10149:
    case 0x10150:
    case 0x10157:
    case 0x10160:
    case 0x10161:
    case 0x10162:
    case 0x10163:
    case 0x10164:
    case 0x102EA:
    case 0x10322:
    case 0x103D3:
    case 0x1085B:
    case 0x1087E:
    case 0x108AD:
    case 0x10917:
    case 0x10A44:
    case 0x10A9E:
    case 0x10AED:
    case 0x10B5C:
    case 0x10B7C:
    case 0x10BAD:
    case 0x10E69:
    case 0x1105B:
    case 0x111EA:
    case 0x118EA:
    case 0x16B5B:
    case 0x1D369:
        return (double) 10.0;
    case 0x0BF1:
    case 0x0D71:
    case 0x137B:
    case 0x216D:
    case 0x217D:
    case 0x4F70:
    case 0x767E:
    case 0x964C:
    case 0x10119:
    case 0x1014B:
    case 0x10152:
    case 0x1016A:
    case 0x102F3:
    case 0x103D5:
    case 0x1085D:
    case 0x108AF:
    case 0x10919:
    case 0x10A46:
    case 0x10AEF:
    case 0x10B5E:
    case 0x10B7E:
    case 0x10BAF:
    case 0x10E72:
    case 0x11064:
    case 0x111F3:
    case 0x16B5C:
        return (double) 100.0;
    case 0x0BF2:
    case 0x0D72:
    case 0x216F:
    case 0x217F:
    case 0x2180:
    case 0x4EDF:
    case 0x5343:
    case 0x9621:
    case 0x10122:
    case 0x1014D:
    case 0x10154:
    case 0x10171:
    case 0x1085E:
    case 0x10A47:
    case 0x10B5F:
    case 0x10B7F:
    case 0x11065:
    case 0x111F4:
        return (double) 1000.0;
    case 0x137C:
    case 0x2182:
    case 0x4E07:
    case 0x842C:
    case 0x1012B:
    case 0x10155:
    case 0x1085F:
    case 0x16B5D:
        return (double) 10000.0;
    case 0x2188:
        return (double) 100000.0;
    case 0x16B5E:
        return (double) 1000000.0;
    case 0x4EBF:
    case 0x5104:
    case 0x16B5F:
        return (double) 100000000.0;
    case 0x16B60:
        return (double) 10000000000.0;
    case 0x5146:
    case 0x16B61:
        return (double) 1000000000000.0;
    case 0x216A:
    case 0x217A:
    case 0x246A:
    case 0x247E:
    case 0x2492:
    case 0x24EB:
        return (double) 11.0;
    case 0x0F2F:
        return (double) 11.0/2.0;
    case 0x216B:
    case 0x217B:
    case 0x246B:
    case 0x247F:
    case 0x2493:
    case 0x24EC:
        return (double) 12.0;
    case 0x246C:
    case 0x2480:
    case 0x2494:
    case 0x24ED:
        return (double) 13.0;
    case 0x0F30:
        return (double) 13.0/2.0;
    case 0x246D:
    case 0x2481:
    case 0x2495:
    case 0x24EE:
        return (double) 14.0;
    case 0x246E:
    case 0x2482:
    case 0x2496:
    case 0x24EF:
        return (double) 15.0;
    case 0x0F31:
        return (double) 15.0/2.0;
    case 0x09F9:
    case 0x246F:
    case 0x2483:
    case 0x2497:
    case 0x24F0:
        return (double) 16.0;
    case 0x16EE:
    case 0x2470:
    case 0x2484:
    case 0x2498:
    case 0x24F1:
        return (double) 17.0;
    case 0x0F32:
        return (double) 17.0/2.0;
    case 0x16EF:
    case 0x2471:
    case 0x2485:
    case 0x2499:
    case 0x24F2:
        return (double) 18.0;
    case 0x16F0:
    case 0x2472:
    case 0x2486:
    case 0x249A:
    case 0x24F3:
        return (double) 19.0;
    case 0x0032:
    case 0x00B2:
    case 0x0662:
    case 0x06F2:
    case 0x07C2:
    case 0x0968:
    case 0x09E8:
    case 0x0A68:
    case 0x0AE8:
    case 0x0B68:
    case 0x0BE8:
    case 0x0C68:
    case 0x0C7A:
    case 0x0C7D:
    case 0x0CE8:
    case 0x0D68:
    case 0x0DE8:
    case 0x0E52:
    case 0x0ED2:
    case 0x0F22:
    case 0x1042:
    case 0x1092:
    case 0x136A:
    case 0x17E2:
    case 0x17F2:
    case 0x1812:
    case 0x1948:
    case 0x19D2:
    case 0x1A82:
    case 0x1A92:
    case 0x1B52:
    case 0x1BB2:
    case 0x1C42:
    case 0x1C52:
    case 0x2082:
    case 0x2161:
    case 0x2171:
    case 0x2461:
    case 0x2475:
    case 0x2489:
    case 0x24F6:
    case 0x2777:
    case 0x2781:
    case 0x278B:
    case 0x3022:
    case 0x3193:
    case 0x3221:
    case 0x3281:
    case 0x3483:
    case 0x4E8C:
    case 0x5169:
    case 0x5F0D:
    case 0x5F10:
    case 0x8CAE:
    case 0x8CB3:
    case 0x8D30:
    case 0xA622:
    case 0xA6E7:
    case 0xA8D2:
    case 0xA902:
    case 0xA9D2:
    case 0xA9F2:
    case 0xAA52:
    case 0xABF2:
    case 0xF978:
    case 0xFF12:
    case 0x10108:
    case 0x1015B:
    case 0x1015C:
    case 0x1015D:
    case 0x1015E:
    case 0x102E2:
    case 0x103D2:
    case 0x104A2:
    case 0x10859:
    case 0x1087A:
    case 0x108A8:
    case 0x1091A:
    case 0x10A41:
    case 0x10B59:
    case 0x10B79:
    case 0x10BAA:
    case 0x10E61:
    case 0x11053:
    case 0x11068:
    case 0x110F2:
    case 0x11138:
    case 0x111D2:
    case 0x111E2:
    case 0x112F2:
    case 0x114D2:
    case 0x11652:
    case 0x116C2:
    case 0x118E2:
    case 0x12400:
    case 0x12416:
    case 0x1241F:
    case 0x12423:
    case 0x1242D:
    case 0x12435:
    case 0x1244A:
    case 0x12450:
    case 0x12456:
    case 0x12459:
    case 0x16A62:
    case 0x16B52:
    case 0x1D361:
    case 0x1D7D0:
    case 0x1D7DA:
    case 0x1D7E4:
    case 0x1D7EE:
    case 0x1D7F8:
    case 0x1E8C8:
    case 0x1F103:
    case 0x22390:
        return (double) 2.0;
    case 0x2154:
    case 0x10177:
    case 0x10E7E:
    case 0x1245B:
    case 0x1245E:
    case 0x12466:
        return (double) 2.0/3.0;
    case 0x2156:
        return (double) 2.0/5.0;
    case 0x1373:
    case 0x2473:
    case 0x2487:
    case 0x249B:
    case 0x24F4:
    case 0x3039:
    case 0x3249:
    case 0x5344:
    case 0x5EFF:
    case 0x10111:
    case 0x102EB:
    case 0x103D4:
    case 0x1085C:
    case 0x1087F:
    case 0x108AE:
    case 0x10918:
    case 0x10A45:
    case 0x10A9F:
    case 0x10AEE:
    case 0x10B5D:
    case 0x10B7D:
    case 0x10BAE:
    case 0x10E6A:
    case 0x1105C:
    case 0x111EB:
    case 0x118EB:
    case 0x1D36A:
        return (double) 20.0;
    case 0x1011A:
    case 0x102F4:
    case 0x10E73:
        return (double) 200.0;
    case 0x10123:
        return (double) 2000.0;
    case 0x1012C:
        return (double) 20000.0;
    case 0x3251:
        return (double) 21.0;
    case 0x12432:
        return (double) 216000.0;
    case 0x3252:
        return (double) 22.0;
    case 0x3253:
        return (double) 23.0;
    case 0x3254:
        return (double) 24.0;
    case 0x3255:
        return (double) 25.0;
    case 0x3256:
        return (double) 26.0;
    case 0x3257:
        return (double) 27.0;
    case 0x3258:
        return (double) 28.0;
    case 0x3259:
        return (double) 29.0;
    case 0x0033:
    case 0x00B3:
    case 0x0663:
    case 0x06F3:
    case 0x07C3:
    case 0x0969:
    case 0x09E9:
    case 0x0A69:
    case 0x0AE9:
    case 0x0B69:
    case 0x0BE9:
    case 0x0C69:
    case 0x0C7B:
    case 0x0C7E:
    case 0x0CE9:
    case 0x0D69:
    case 0x0DE9:
    case 0x0E53:
    case 0x0ED3:
    case 0x0F23:
    case 0x1043:
    case 0x1093:
    case 0x136B:
    case 0x17E3:
    case 0x17F3:
    case 0x1813:
    case 0x1949:
    case 0x19D3:
    case 0x1A83:
    case 0x1A93:
    case 0x1B53:
    case 0x1BB3:
    case 0x1C43:
    case 0x1C53:
    case 0x2083:
    case 0x2162:
    case 0x2172:
    case 0x2462:
    case 0x2476:
    case 0x248A:
    case 0x24F7:
    case 0x2778:
    case 0x2782:
    case 0x278C:
    case 0x3023:
    case 0x3194:
    case 0x3222:
    case 0x3282:
    case 0x4E09:
    case 0x4EE8:
    case 0x53C1:
    case 0x53C2:
    case 0x53C3:
    case 0x53C4:
    case 0x5F0E:
    case 0xA623:
    case 0xA6E8:
    case 0xA8D3:
    case 0xA903:
    case 0xA9D3:
    case 0xA9F3:
    case 0xAA53:
    case 0xABF3:
    case 0xF96B:
    case 0xFF13:
    case 0x10109:
    case 0x102E3:
    case 0x104A3:
    case 0x1085A:
    case 0x1087B:
    case 0x108A9:
    case 0x1091B:
    case 0x10A42:
    case 0x10B5A:
    case 0x10B7A:
    case 0x10BAB:
    case 0x10E62:
    case 0x11054:
    case 0x11069:
    case 0x110F3:
    case 0x11139:
    case 0x111D3:
    case 0x111E3:
    case 0x112F3:
    case 0x114D3:
    case 0x11653:
    case 0x116C3:
    case 0x118E3:
    case 0x12401:
    case 0x12408:
    case 0x12417:
    case 0x12420:
    case 0x12424:
    case 0x12425:
    case 0x1242E:
    case 0x1242F:
    case 0x12436:
    case 0x12437:
    case 0x1243A:
    case 0x1243B:
    case 0x1244B:
    case 0x12451:
    case 0x12457:
    case 0x16A63:
    case 0x16B53:
    case 0x1D362:
    case 0x1D7D1:
    case 0x1D7DB:
    case 0x1D7E5:
    case 0x1D7EF:
    case 0x1D7F9:
    case 0x1E8C9:
    case 0x1F104:
    case 0x20AFD:
    case 0x20B19:
    case 0x22998:
    case 0x23B1B:
        return (double) 3.0;
    case 0x09F6:
    case 0x0B77:
    case 0xA835:
        return (double) 3.0/16.0;
    case 0x0F2B:
        return (double) 3.0/2.0;
    case 0x00BE:
    case 0x09F8:
    case 0x0B74:
    case 0x0D75:
    case 0xA832:
    case 0x10178:
        return (double) 3.0/4.0;
    case 0x2157:
        return (double) 3.0/5.0;
    case 0x215C:
        return (double) 3.0/8.0;
    case 0x1374:
    case 0x303A:
    case 0x324A:
    case 0x325A:
    case 0x5345:
    case 0x10112:
    case 0x10165:
    case 0x102EC:
    case 0x10E6B:
    case 0x1105D:
    case 0x111EC:
    case 0x118EC:
    case 0x1D36B:
    case 0x20983:
        return (double) 30.0;
    case 0x1011B:
    case 0x1016B:
    case 0x102F5:
    case 0x10E74:
        return (double) 300.0;
    case 0x10124:
        return (double) 3000.0;
    case 0x1012D:
        return (double) 30000.0;
    case 0x325B:
        return (double) 31.0;
    case 0x325C:
        return (double) 32.0;
    case 0x325D:
        return (double) 33.0;
    case 0x325E:
        return (double) 34.0;
    case 0x325F:
        return (double) 35.0;
    case 0x32B1:
        return (double) 36.0;
    case 0x32B2:
        return (double) 37.0;
    case 0x32B3:
        return (double) 38.0;
    case 0x32B4:
        return (double) 39.0;
    case 0x0034:
    case 0x0664:
    case 0x06F4:
    case 0x07C4:
    case 0x096A:
    case 0x09EA:
    case 0x0A6A:
    case 0x0AEA:
    case 0x0B6A:
    case 0x0BEA:
    case 0x0C6A:
    case 0x0CEA:
    case 0x0D6A:
    case 0x0DEA:
    case 0x0E54:
    case 0x0ED4:
    case 0x0F24:
    case 0x1044:
    case 0x1094:
    case 0x136C:
    case 0x17E4:
    case 0x17F4:
    case 0x1814:
    case 0x194A:
    case 0x19D4:
    case 0x1A84:
    case 0x1A94:
    case 0x1B54:
    case 0x1BB4:
    case 0x1C44:
    case 0x1C54:
    case 0x2074:
    case 0x2084:
    case 0x2163:
    case 0x2173:
    case 0x2463:
    case 0x2477:
    case 0x248B:
    case 0x24F8:
    case 0x2779:
    case 0x2783:
    case 0x278D:
    case 0x3024:
    case 0x3195:
    case 0x3223:
    case 0x3283:
    case 0x4E96:
    case 0x56DB:
    case 0x8086:
    case 0xA624:
    case 0xA6E9:
    case 0xA8D4:
    case 0xA904:
    case 0xA9D4:
    case 0xA9F4:
    case 0xAA54:
    case 0xABF4:
    case 0xFF14:
    case 0x1010A:
    case 0x102E4:
    case 0x104A4:
    case 0x1087C:
    case 0x108AA:
    case 0x108AB:
    case 0x10A43:
    case 0x10B5B:
    case 0x10B7B:
    case 0x10BAC:
    case 0x10E63:
    case 0x11055:
    case 0x1106A:
    case 0x110F4:
    case 0x1113A:
    case 0x111D4:
    case 0x111E4:
    case 0x112F4:
    case 0x114D4:
    case 0x11654:
    case 0x116C4:
    case 0x118E4:
    case 0x12402:
    case 0x12409:
    case 0x1240F:
    case 0x12418:
    case 0x12421:
    case 0x12426:
    case 0x12430:
    case 0x12438:
    case 0x1243C:
    case 0x1243D:
    case 0x1243E:
    case 0x1243F:
    case 0x1244C:
    case 0x12452:
    case 0x12453:
    case 0x12469:
    case 0x16A64:
    case 0x16B54:
    case 0x1D363:
    case 0x1D7D2:
    case 0x1D7DC:
    case 0x1D7E6:
    case 0x1D7F0:
    case 0x1D7FA:
    case 0x1E8CA:
    case 0x1F105:
    case 0x20064:
    case 0x200E2:
    case 0x2626D:
        return (double) 4.0;
    case 0x2158:
        return (double) 4.0/5.0;
    case 0x1375:
    case 0x324B:
    case 0x32B5:
    case 0x534C:
    case 0x10113:
    case 0x102ED:
    case 0x10E6C:
    case 0x1105E:
    case 0x111ED:
    case 0x118ED:
    case 0x12467:
    case 0x1D36C:
    case 0x2098C:
    case 0x2099C:
        return (double) 40.0;
    case 0x1011C:
    case 0x102F6:
    case 0x10E75:
        return (double) 400.0;
    case 0x10125:
        return (double) 4000.0;
    case 0x1012E:
        return (double) 40000.0;
    case 0x32B6:
        return (double) 41.0;
    case 0x32B7:
        return (double) 42.0;
    case 0x32B8:
        return (double) 43.0;
    case 0x12433:
        return (double) 432000.0;
    case 0x32B9:
        return (double) 44.0;
    case 0x32BA:
        return (double) 45.0;
    case 0x32BB:
        return (double) 46.0;
    case 0x32BC:
        return (double) 47.0;
    case 0x32BD:
        return (double) 48.0;
    case 0x32BE:
        return (double) 49.0;
    case 0x0035:
    case 0x0665:
    case 0x06F5:
    case 0x07C5:
    case 0x096B:
    case 0x09EB:
    case 0x0A6B:
    case 0x0AEB:
    case 0x0B6B:
    case 0x0BEB:
    case 0x0C6B:
    case 0x0CEB:
    case 0x0D6B:
    case 0x0DEB:
    case 0x0E55:
    case 0x0ED5:
    case 0x0F25:
    case 0x1045:
    case 0x1095:
    case 0x136D:
    case 0x17E5:
    case 0x17F5:
    case 0x1815:
    case 0x194B:
    case 0x19D5:
    case 0x1A85:
    case 0x1A95:
    case 0x1B55:
    case 0x1BB5:
    case 0x1C45:
    case 0x1C55:
    case 0x2075:
    case 0x2085:
    case 0x2164:
    case 0x2174:
    case 0x2464:
    case 0x2478:
    case 0x248C:
    case 0x24F9:
    case 0x277A:
    case 0x2784:
    case 0x278E:
    case 0x3025:
    case 0x3224:
    case 0x3284:
    case 0x3405:
    case 0x382A:
    case 0x4E94:
    case 0x4F0D:
    case 0xA625:
    case 0xA6EA:
    case 0xA8D5:
    case 0xA905:
    case 0xA9D5:
    case 0xA9F5:
    case 0xAA55:
    case 0xABF5:
    case 0xFF15:
    case 0x1010B:
    case 0x10143:
    case 0x10148:
    case 0x1014F:
    case 0x1015F:
    case 0x10173:
    case 0x102E5:
    case 0x10321:
    case 0x104A5:
    case 0x1087D:
    case 0x108AC:
    case 0x10AEC:
    case 0x10E64:
    case 0x11056:
    case 0x1106B:
    case 0x110F5:
    case 0x1113B:
    case 0x111D5:
    case 0x111E5:
    case 0x112F5:
    case 0x114D5:
    case 0x11655:
    case 0x116C5:
    case 0x118E5:
    case 0x12403:
    case 0x1240A:
    case 0x12410:
    case 0x12419:
    case 0x12422:
    case 0x12427:
    case 0x12431:
    case 0x12439:
    case 0x1244D:
    case 0x12454:
    case 0x12455:
    case 0x1246A:
    case 0x16A65:
    case 0x16B55:
    case 0x1D364:
    case 0x1D7D3:
    case 0x1D7DD:
    case 0x1D7E7:
    case 0x1D7F1:
    case 0x1D7FB:
    case 0x1E8CB:
    case 0x1F106:
    case 0x20121:
        return (double) 5.0;
    case 0x0F2C:
        return (double) 5.0/2.0;
    case 0x215A:
    case 0x1245C:
        return (double) 5.0/6.0;
    case 0x215D:
        return (double) 5.0/8.0;
    case 0x1376:
    case 0x216C:
    case 0x217C:
    case 0x2186:
    case 0x324C:
    case 0x32BF:
    case 0x10114:
    case 0x10144:
    case 0x1014A:
    case 0x10151:
    case 0x10166:
    case 0x10167:
    case 0x10168:
    case 0x10169:
    case 0x10174:
    case 0x102EE:
    case 0x10323:
    case 0x10A7E:
    case 0x10E6D:
    case 0x1105F:
    case 0x111EE:
    case 0x118EE:
    case 0x12468:
    case 0x1D36D:
        return (double) 50.0;
    case 0x216E:
    case 0x217E:
    case 0x1011D:
    case 0x10145:
    case 0x1014C:
    case 0x10153:
    case 0x1016C:
    case 0x1016D:
    case 0x1016E:
    case 0x1016F:
    case 0x10170:
    case 0x102F7:
    case 0x10E76:
        return (double) 500.0;
    case 0x2181:
    case 0x10126:
    case 0x10146:
    case 0x1014E:
    case 0x10172:
        return (double) 5000.0;
    case 0x2187:
    case 0x1012F:
    case 0x10147:
    case 0x10156:
        return (double) 50000.0;
    case 0x0036:
    case 0x0666:
    case 0x06F6:
    case 0x07C6:
    case 0x096C:
    case 0x09EC:
    case 0x0A6C:
    case 0x0AEC:
    case 0x0B6C:
    case 0x0BEC:
    case 0x0C6C:
    case 0x0CEC:
    case 0x0D6C:
    case 0x0DEC:
    case 0x0E56:
    case 0x0ED6:
    case 0x0F26:
    case 0x1046:
    case 0x1096:
    case 0x136E:
    case 0x17E6:
    case 0x17F6:
    case 0x1816:
    case 0x194C:
    case 0x19D6:
    case 0x1A86:
    case 0x1A96:
    case 0x1B56:
    case 0x1BB6:
    case 0x1C46:
    case 0x1C56:
    case 0x2076:
    case 0x2086:
    case 0x2165:
    case 0x2175:
    case 0x2185:
    case 0x2465:
    case 0x2479:
    case 0x248D:
    case 0x24FA:
    case 0x277B:
    case 0x2785:
    case 0x278F:
    case 0x3026:
    case 0x3225:
    case 0x3285:
    case 0x516D:
    case 0x9646:
    case 0x9678:
    case 0xA626:
    case 0xA6EB:
    case 0xA8D6:
    case 0xA906:
    case 0xA9D6:
    case 0xA9F6:
    case 0xAA56:
    case 0xABF6:
    case 0xF9D1:
    case 0xF9D3:
    case 0xFF16:
    case 0x1010C:
    case 0x102E6:
    case 0x104A6:
    case 0x10E65:
    case 0x11057:
    case 0x1106C:
    case 0x110F6:
    case 0x1113C:
    case 0x111D6:
    case 0x111E6:
    case 0x112F6:
    case 0x114D6:
    case 0x11656:
    case 0x116C6:
    case 0x118E6:
    case 0x12404:
    case 0x1240B:
    case 0x12411:
    case 0x1241A:
    case 0x12428:
    case 0x12440:
    case 0x1244E:
    case 0x1246B:
    case 0x16A66:
    case 0x16B56:
    case 0x1D365:
    case 0x1D7D4:
    case 0x1D7DE:
    case 0x1D7E8:
    case 0x1D7F2:
    case 0x1D7FC:
    case 0x1E8CC:
    case 0x1F107:
    case 0x20AEA:
        return (double) 6.0;
    case 0x1377:
    case 0x324D:
    case 0x10115:
    case 0x102EF:
    case 0x10E6E:
    case 0x11060:
    case 0x111EF:
    case 0x118EF:
    case 0x1D36E:
        return (double) 60.0;
    case 0x1011E:
    case 0x102F8:
    case 0x10E77:
        return (double) 600.0;
    case 0x10127:
        return (double) 6000.0;
    case 0x10130:
        return (double) 60000.0;
    case 0x0037:
    case 0x0667:
    case 0x06F7:
    case 0x07C7:
    case 0x096D:
    case 0x09ED:
    case 0x0A6D:
    case 0x0AED:
    case 0x0B6D:
    case 0x0BED:
    case 0x0C6D:
    case 0x0CED:
    case 0x0D6D:
    case 0x0DED:
    case 0x0E57:
    case 0x0ED7:
    case 0x0F27:
    case 0x1047:
    case 0x1097:
    case 0x136F:
    case 0x17E7:
    case 0x17F7:
    case 0x1817:
    case 0x194D:
    case 0x19D7:
    case 0x1A87:
    case 0x1A97:
    case 0x1B57:
    case 0x1BB7:
    case 0x1C47:
    case 0x1C57:
    case 0x2077:
    case 0x2087:
    case 0x2166:
    case 0x2176:
    case 0x2466:
    case 0x247A:
    case 0x248E:
    case 0x24FB:
    case 0x277C:
    case 0x2786:
    case 0x2790:
    case 0x3027:
    case 0x3226:
    case 0x3286:
    case 0x3B4D:
    case 0x4E03:
    case 0x67D2:
    case 0x6F06:
    case 0xA627:
    case 0xA6EC:
    case 0xA8D7:
    case 0xA907:
    case 0xA9D7:
    case 0xA9F7:
    case 0xAA57:
    case 0xABF7:
    case 0xFF17:
    case 0x1010D:
    case 0x102E7:
    case 0x104A7:
    case 0x10E66:
    case 0x11058:
    case 0x1106D:
    case 0x110F7:
    case 0x1113D:
    case 0x111D7:
    case 0x111E7:
    case 0x112F7:
    case 0x114D7:
    case 0x11657:
    case 0x116C7:
    case 0x118E7:
    case 0x12405:
    case 0x1240C:
    case 0x12412:
    case 0x1241B:
    case 0x12429:
    case 0x12441:
    case 0x12442:
    case 0x12443:
    case 0x1246C:
    case 0x16A67:
    case 0x16B57:
    case 0x1D366:
    case 0x1D7D5:
    case 0x1D7DF:
    case 0x1D7E9:
    case 0x1D7F3:
    case 0x1D7FD:
    case 0x1E8CD:
    case 0x1F108:
    case 0x20001:
        return (double) 7.0;
    case 0x0F2D:
        return (double) 7.0/2.0;
    case 0x215E:
        return (double) 7.0/8.0;
    case 0x1378:
    case 0x324E:
    case 0x10116:
    case 0x102F0:
    case 0x10E6F:
    case 0x11061:
    case 0x111F0:
    case 0x118F0:
    case 0x1D36F:
        return (double) 70.0;
    case 0x1011F:
    case 0x102F9:
    case 0x10E78:
        return (double) 700.0;
    case 0x10128:
        return (double) 7000.0;
    case 0x10131:
        return (double) 70000.0;
    case 0x0038:
    case 0x0668:
    case 0x06F8:
    case 0x07C8:
    case 0x096E:
    case 0x09EE:
    case 0x0A6E:
    case 0x0AEE:
    case 0x0B6E:
    case 0x0BEE:
    case 0x0C6E:
    case 0x0CEE:
    case 0x0D6E:
    case 0x0DEE:
    case 0x0E58:
    case 0x0ED8:
    case 0x0F28:
    case 0x1048:
    case 0x1098:
    case 0x1370:
    case 0x17E8:
    case 0x17F8:
    case 0x1818:
    case 0x194E:
    case 0x19D8:
    case 0x1A88:
    case 0x1A98:
    case 0x1B58:
    case 0x1BB8:
    case 0x1C48:
    case 0x1C58:
    case 0x2078:
    case 0x2088:
    case 0x2167:
    case 0x2177:
    case 0x2467:
    case 0x247B:
    case 0x248F:
    case 0x24FC:
    case 0x277D:
    case 0x2787:
    case 0x2791:
    case 0x3028:
    case 0x3227:
    case 0x3287:
    case 0x516B:
    case 0x634C:
    case 0xA628:
    case 0xA6ED:
    case 0xA8D8:
    case 0xA908:
    case 0xA9D8:
    case 0xA9F8:
    case 0xAA58:
    case 0xABF8:
    case 0xFF18:
    case 0x1010E:
    case 0x102E8:
    case 0x104A8:
    case 0x10E67:
    case 0x11059:
    case 0x1106E:
    case 0x110F8:
    case 0x1113E:
    case 0x111D8:
    case 0x111E8:
    case 0x112F8:
    case 0x114D8:
    case 0x11658:
    case 0x116C8:
    case 0x118E8:
    case 0x12406:
    case 0x1240D:
    case 0x12413:
    case 0x1241C:
    case 0x1242A:
    case 0x12444:
    case 0x12445:
    case 0x1246D:
    case 0x16A68:
    case 0x16B58:
    case 0x1D367:
    case 0x1D7D6:
    case 0x1D7E0:
    case 0x1D7EA:
    case 0x1D7F4:
    case 0x1D7FE:
    case 0x1E8CE:
    case 0x1F109:
        return (double) 8.0;
    case 0x1379:
    case 0x324F:
    case 0x10117:
    case 0x102F1:
    case 0x10E70:
    case 0x11062:
    case 0x111F1:
    case 0x118F1:
    case 0x1D370:
        return (double) 80.0;
    case 0x10120:
    case 0x102FA:
    case 0x10E79:
        return (double) 800.0;
    case 0x10129:
        return (double) 8000.0;
    case 0x10132:
        return (double) 80000.0;
    case 0x0039:
    case 0x0669:
    case 0x06F9:
    case 0x07C9:
    case 0x096F:
    case 0x09EF:
    case 0x0A6F:
    case 0x0AEF:
    case 0x0B6F:
    case 0x0BEF:
    case 0x0C6F:
    case 0x0CEF:
    case 0x0D6F:
    case 0x0DEF:
    case 0x0E59:
    case 0x0ED9:
    case 0x0F29:
    case 0x1049:
    case 0x1099:
    case 0x1371:
    case 0x17E9:
    case 0x17F9:
    case 0x1819:
    case 0x194F:
    case 0x19D9:
    case 0x1A89:
    case 0x1A99:
    case 0x1B59:
    case 0x1BB9:
    case 0x1C49:
    case 0x1C59:
    case 0x2079:
    case 0x2089:
    case 0x2168:
    case 0x2178:
    case 0x2468:
    case 0x247C:
    case 0x2490:
    case 0x24FD:
    case 0x277E:
    case 0x2788:
    case 0x2792:
    case 0x3029:
    case 0x3228:
    case 0x3288:
    case 0x4E5D:
    case 0x5EFE:
    case 0x7396:
    case 0xA629:
    case 0xA6EE:
    case 0xA8D9:
    case 0xA909:
    case 0xA9D9:
    case 0xA9F9:
    case 0xAA59:
    case 0xABF9:
    case 0xFF19:
    case 0x1010F:
    case 0x102E9:
    case 0x104A9:
    case 0x10E68:
    case 0x1105A:
    case 0x1106F:
    case 0x110F9:
    case 0x1113F:
    case 0x111D9:
    case 0x111E9:
    case 0x112F9:
    case 0x114D9:
    case 0x11659:
    case 0x116C9:
    case 0x118E9:
    case 0x12407:
    case 0x1240E:
    case 0x12414:
    case 0x1241D:
    case 0x1242B:
    case 0x12446:
    case 0x12447:
    case 0x12448:
    case 0x12449:
    case 0x1246E:
    case 0x16A69:
    case 0x16B59:
    case 0x1D368:
    case 0x1D7D7:
    case 0x1D7E1:
    case 0x1D7EB:
    case 0x1D7F5:
    case 0x1D7FF:
    case 0x1E8CF:
    case 0x1F10A:
    case 0x2F890:
        return (double) 9.0;
    case 0x0F2E:
        return (double) 9.0/2.0;
    case 0x137A:
    case 0x10118:
    case 0x102F2:
    case 0x10341:
    case 0x10E71:
    case 0x11063:
    case 0x111F2:
    case 0x118F2:
    case 0x1D371:
        return (double) 90.0;
    case 0x10121:
    case 0x102FB:
    case 0x1034A:
    case 0x10E7A:
        return (double) 900.0;
    case 0x1012A:
        return (double) 9000.0;
    case 0x10133:
        return (double) 90000.0;
    }
    return -1.0;
}



static int unicodify(int c, struct tok_state *tok)
{
  
  int bytelim=1;
  int mask=63; //00111111

  unsigned long int temp=0;
  unsigned long int temp2=0;
  unsigned int tempfinal=0;
   
  int i=0;
  int firstpass=1;
  int array[3];

  int finalresult = 48; //ASCII for Zero

  
  if (c < 128)
    {
      return c;
    }
  else if (c >= 224)
    {
      //printf("3 bytes!\n");
      bytelim = 3;
      mask=15; //0000 1111
      
      array[0] = c;
      array[1] = tok_nextc(tok);
      array[2] = tok_nextc(tok);
    }
  else if (c >= 192)
    {
      //printf("2 bytes!\n");
      bytelim = 2;
      mask=31; //0001 1111
      
      array[0] = c;
      array[1] = tok_nextc(tok);
    }
  else if ( c >= 128)
    {
      //printf("One byte!\n");
      //0011 1111
 
      array[0] = c;
    }
  
  //extract codepoint
  for(i=0, temp = array[0], firstpass = 1; i < bytelim-1; i++)
    {
          
      temp2 = array[i+1];
      
      if(firstpass == 1)
	{
	  temp = temp & mask;
	  mask = 63;
	  firstpass = 0;
	  
	}
    
      temp2 = temp2 & mask;		

      temp = (temp << 6) | temp2;

		
      tempfinal = temp;
    }

  //printf("The codepoint in decimal is %u\n", tempfinal);
  //printf("The codepoint in hex is %x\n", tempfinal);

  finalresult = (int)uni2Num(tempfinal) + 48;
     
  //printf("The value is %d\n", finalresult);

  return finalresult;

  
}

static int
tok_get(struct tok_state *tok, char **p_start, char **p_end)
{
    int c;
    int blankline, nonascii;

    int tok_len;
    struct tok_state ahead_tok;
    char *ahead_tok_start = NULL, *ahead_top_end = NULL;
    int ahead_tok_kind;

    *p_start = *p_end = NULL;
  nextline:
    tok->start = NULL;
    blankline = 0;

    /* Get indentation level */
    if (tok->atbol) {
        int col = 0;
        int altcol = 0;
        tok->atbol = 0;
        for (;;) {
            c = tok_nextc(tok);
            if (c == ' ')
                col++, altcol++;
            else if (c == '\t') {
                col = (col/tok->tabsize + 1) * tok->tabsize;
                altcol = (altcol/tok->alttabsize + 1)
                    * tok->alttabsize;
            }
            else if (c == '\014') /* Control-L (formfeed) */
                col = altcol = 0; /* For Emacs users */
            else
                break;
        }
        tok_backup(tok, c);
        if (c == '#' || c == '\n') {
            /* Lines with only whitespace and/or comments
               shouldn't affect the indentation and are
               not passed to the parser as NEWLINE tokens,
               except *totally* empty lines in interactive
               mode, which signal the end of a command group. */
            if (col == 0 && c == '\n' && tok->prompt != NULL)
                blankline = 0; /* Let it through */
            else
                blankline = 1; /* Ignore completely */
            /* We can't jump back right here since we still
               may need to skip to the end of a comment */
        }
        if (!blankline && tok->level == 0) {
            if (col == tok->indstack[tok->indent]) {
                /* No change */
                if (altcol != tok->altindstack[tok->indent]) {
                    if (indenterror(tok))
                        return ERRORTOKEN;
                }
            }
            else if (col > tok->indstack[tok->indent]) {
                /* Indent -- always one */
                if (tok->indent+1 >= MAXINDENT) {
                    tok->done = E_TOODEEP;
                    tok->cur = tok->inp;
                    return ERRORTOKEN;
                }
                if (altcol <= tok->altindstack[tok->indent]) {
                    if (indenterror(tok))
                        return ERRORTOKEN;
                }
                tok->pendin++;
                tok->indstack[++tok->indent] = col;
                tok->altindstack[tok->indent] = altcol;
            }
            else /* col < tok->indstack[tok->indent] */ {
                /* Dedent -- any number, must be consistent */
                while (tok->indent > 0 &&
                    col < tok->indstack[tok->indent]) {
                    tok->pendin--;
                    tok->indent--;
                }
                if (col != tok->indstack[tok->indent]) {
                    tok->done = E_DEDENT;
                    tok->cur = tok->inp;
                    return ERRORTOKEN;
                }
                if (altcol != tok->altindstack[tok->indent]) {
                    if (indenterror(tok))
                        return ERRORTOKEN;
                }
            }
        }
    }

    tok->start = tok->cur;

    /* Return pending indents/dedents */
    if (tok->pendin != 0) {
        if (tok->pendin < 0) {
            tok->pendin++;

            while (tok->def && tok->defstack[tok->def] >= tok->indent) {
                tok->def--;
            }

            return DEDENT;
        }
        else {
            tok->pendin--;
            return INDENT;
        }
    }

 again:
    tok->start = NULL;
    /* Skip spaces */
    do {
        c = tok_nextc(tok);
    } while (c == ' ' || c == '\t' || c == '\014');

    /* Set start of current token */
    tok->start = tok->cur - 1;

    /* Skip comment */
    if (c == '#')
        while (c != EOF && c != '\n')
            c = tok_nextc(tok);

    /* Check for EOF and errors now */
    if (c == EOF) {
        return tok->done == E_EOF ? ENDMARKER : ERRORTOKEN;
    }

    /* Identifier (most frequent token!) */
    nonascii = 0;

    c = unicodify(c, tok);

    if (is_potential_identifier_start(c)) {
        /* Process b"", r"", u"", br"" and rb"" */
        int saw_b = 0, saw_r = 0, saw_u = 0;
        while (1) {
            if (!(saw_b || saw_u) && (c == 'b' || c == 'B'))
                saw_b = 1;
            /* Since this is a backwards compatibility support literal we don't
               want to support it in arbitrary order like byte literals. */
            else if (!(saw_b || saw_u || saw_r) && (c == 'u' || c == 'U'))
                saw_u = 1;
            /* ur"" and ru"" are not supported */
            else if (!(saw_r || saw_u) && (c == 'r' || c == 'R'))
                saw_r = 1;
            else
                break;
            c = tok_nextc(tok);
            if (c == '"' || c == '\'')
                goto letter_quote;
        }
        while (is_potential_identifier_char(c)) {
            if (c >= 128)
                nonascii = 1;
            c = tok_nextc(tok);
        }
        tok_backup(tok, c);
        if (nonascii && !verify_identifier(tok))
            return ERRORTOKEN;
        *p_start = tok->start;
        *p_end = tok->cur;

        tok_len = tok->cur - tok->start;
        if (tok_len == 3 && memcmp(tok->start, "def", 3) == 0) {
            if (tok->def && tok->deftypestack[tok->def] == 3) {
                tok->deftypestack[tok->def] = 2;
            }
            else if (tok->defstack[tok->def] < tok->indent) {
                /* We advance defs stack only when we see "def" *and*
                   the indentation level was increased relative to the
                   previous "def". */

                if (tok->def + 1 >= MAXINDENT) {
                    tok->done = E_TOODEEP;
                    tok->cur = tok->inp;
                    return ERRORTOKEN;
                }

                tok->def++;
                tok->defstack[tok->def] = tok->indent;
                tok->deftypestack[tok->def] = 1;
            }
        }
        else if (tok_len == 5) {
            if (memcmp(tok->start, "async", 5) == 0) {
                memcpy(&ahead_tok, tok, sizeof(ahead_tok));

                ahead_tok_kind = tok_get(&ahead_tok, &ahead_tok_start,
                                         &ahead_top_end);

                if (ahead_tok_kind == NAME &&
                        ahead_tok.cur - ahead_tok.start == 3 &&
                        memcmp(ahead_tok.start, "def", 3) == 0) {

                    if (tok->def + 1 >= MAXINDENT) {
                        tok->done = E_TOODEEP;
                        tok->cur = tok->inp;
                        return ERRORTOKEN;
                    }

                    tok->def++;
                    tok->defstack[tok->def] = tok->indent;
                    tok->deftypestack[tok->def] = 3;

                    return ASYNC;
                }
                else if (tok->def && tok->deftypestack[tok->def] == 2
                         && tok->defstack[tok->def] < tok->indent) {

                    return ASYNC;
                }

            }
            else if (memcmp(tok->start, "await", 5) == 0
                        && tok->def && tok->deftypestack[tok->def] == 2
                        && tok->defstack[tok->def] < tok->indent) {

                return AWAIT;
            }
        }

        return NAME;
    }

    /* Newline */
    if (c == '\n') {
        tok->atbol = 1;
        if (blankline || tok->level > 0)
            goto nextline;
        *p_start = tok->start;
        *p_end = tok->cur - 1; /* Leave '\n' out of the string */
        tok->cont_line = 0;
        return NEWLINE;
    }

    /* Period or number starting with period? */
    if (c == '.') {
        c = tok_nextc(tok);
	c = unicodify(c, tok);
        if (isdigit(c)) {
            goto fraction;
        } else if (c == '.') {
            c = tok_nextc(tok);
            if (c == '.') {
                *p_start = tok->start;
                *p_end = tok->cur;
                return ELLIPSIS;
            } else {
                tok_backup(tok, c);
            }
            tok_backup(tok, '.');
        } else {
            tok_backup(tok, c);
        }
        *p_start = tok->start;
        *p_end = tok->cur;
        return DOT;
    }

    /* Number */
    if (isdigit(c)) {
        if (c == '0') {
            /* Hex, octal or binary -- maybe. */
            c = tok_nextc(tok);
            if (c == '.')
                goto fraction;
            if (c == 'j' || c == 'J')
                goto imaginary;
            if (c == 'x' || c == 'X') {

                /* Hex */
                c = tok_nextc(tok);
		c = unicodify(c, tok);
                if (!isxdigit(c)) {
                    tok->done = E_TOKEN;
                    tok_backup(tok, c);
                    return ERRORTOKEN;
                }
                do {
                    c = tok_nextc(tok);
		    c = unicodify(c, tok);
                } while (isxdigit(c));
            }
            else if (c == 'o' || c == 'O') {
                /* Octal */
                c = tok_nextc(tok);
		c = unicodify(c, tok);
		
                if (c < '0' || c >= '8') {
                    tok->done = E_TOKEN;
                    tok_backup(tok, c);
                    return ERRORTOKEN;
                }
                do {
                    c = tok_nextc(tok);
		    c = unicodify(c, tok);
                } while ('0' <= c && c < '8');
            }
            else if (c == 'b' || c == 'B') {
                /* Binary */
                c = tok_nextc(tok);
		c = unicodify(c, tok);
                if (c != '0' && c != '1') {
                    tok->done = E_TOKEN;
                    tok_backup(tok, c);
                    return ERRORTOKEN;
                }
                do {
                    c = tok_nextc(tok);
		    c = unicodify(c, tok);
                } while (c == '0' || c == '1');
            }
            else {
                int nonzero = 0;
                /* maybe old-style octal; c is first char of it */
                /* in any case, allow '0' as a literal */
                while (c == '0')
                    c = tok_nextc(tok);
                while (isdigit(c)) {
                    nonzero = 1;
                    c = tok_nextc(tok);
                }
                if (c == '.')
                    goto fraction;
                else if (c == 'e' || c == 'E')
                    goto exponent;
                else if (c == 'j' || c == 'J')
                    goto imaginary;
                else if (nonzero) {
                    tok->done = E_TOKEN;
                    tok_backup(tok, c);
                    return ERRORTOKEN;
                }
            }
        }
        else {
            /* Decimal */
            do {
                c = tok_nextc(tok);
		c = unicodify(c, tok);
            } while (isdigit(c));
            {
                /* Accept floating point numbers. */
                if (c == '.') {
        fraction:
                    /* Fraction */
                    do {
                        c = tok_nextc(tok);
			c = unicodify(c, tok);
                    } while (isdigit(c));
                }
                if (c == 'e' || c == 'E') {
                    int e;
                  exponent:
                    e = c;
                    /* Exponent part */
                    c = tok_nextc(tok);
		    c = unicodify(c, tok);
                    if (c == '+' || c == '-') {
                        c = tok_nextc(tok);
                        if (!isdigit(c)) {
                            tok->done = E_TOKEN;
                            tok_backup(tok, c);
                            return ERRORTOKEN;
                        }
                    } else if (!isdigit(c)) {
                        tok_backup(tok, c);
                        tok_backup(tok, e);
                        *p_start = tok->start;
                        *p_end = tok->cur;
                        return NUMBER;
                    }
                    do {
                        c = tok_nextc(tok);
			c = unicodify(c, tok);
                    } while (isdigit(c));
                }
                if (c == 'j' || c == 'J')
                    /* Imaginary part */
        imaginary:
                    c = tok_nextc(tok);
            }
        }
        tok_backup(tok, c);
        *p_start = tok->start;
        *p_end = tok->cur;
        return NUMBER;
    }

  letter_quote:
    /* String */
    if (c == '\'' || c == '"') {
        int quote = c;
        int quote_size = 1;             /* 1 or 3 */
        int end_quote_size = 0;

        /* Find the quote size and start of string */
        c = tok_nextc(tok);
        if (c == quote) {
            c = tok_nextc(tok);
            if (c == quote)
                quote_size = 3;
            else
                end_quote_size = 1;     /* empty string found */
        }
        if (c != quote)
            tok_backup(tok, c);

        /* Get rest of string */
        while (end_quote_size != quote_size) {
            c = tok_nextc(tok);
            if (c == EOF) {
                if (quote_size == 3)
                    tok->done = E_EOFS;
                else
                    tok->done = E_EOLS;
                tok->cur = tok->inp;
                return ERRORTOKEN;
            }
            if (quote_size == 1 && c == '\n') {
                tok->done = E_EOLS;
                tok->cur = tok->inp;
                return ERRORTOKEN;
            }
            if (c == quote)
                end_quote_size += 1;
            else {
                end_quote_size = 0;
                if (c == '\\')
                c = tok_nextc(tok);  /* skip escaped char */
            }
        }

        *p_start = tok->start;
        *p_end = tok->cur;
        return STRING;
    }

    /* Line continuation */
    if (c == '\\') {
        c = tok_nextc(tok);
        if (c != '\n') {
            tok->done = E_LINECONT;
            tok->cur = tok->inp;
            return ERRORTOKEN;
        }
        tok->cont_line = 1;
        goto again; /* Read next line */
    }

    /* Check for two-character token */
    {
        int c2 = tok_nextc(tok);
        int token = PyToken_TwoChars(c, c2);
        if (token != OP) {
            int c3 = tok_nextc(tok);
            int token3 = PyToken_ThreeChars(c, c2, c3);
            if (token3 != OP) {
                token = token3;
            } else {
                tok_backup(tok, c3);
            }
            *p_start = tok->start;
            *p_end = tok->cur;
            return token;
        }
        tok_backup(tok, c2);
    }

    /* Keep track of parentheses nesting level */
    switch (c) {
    case '(':
    case '[':
    case '{':
        tok->level++;
        break;
    case ')':
    case ']':
    case '}':
        tok->level--;
        break;
    }

    /* Punctuation character */
    *p_start = tok->start;
    *p_end = tok->cur;
    return PyToken_OneChar(c);
}

int
PyTokenizer_Get(struct tok_state *tok, char **p_start, char **p_end)
{
    int result = tok_get(tok, p_start, p_end);
    if (tok->decoding_erred) {
        result = ERRORTOKEN;
        tok->done = E_DECODE;
    }
    return result;
}

/* Get the encoding of a Python file. Check for the coding cookie and check if
   the file starts with a BOM.

   PyTokenizer_FindEncodingFilename() returns NULL when it can't find the
   encoding in the first or second line of the file (in which case the encoding
   should be assumed to be UTF-8).

   The char* returned is malloc'ed via PyMem_MALLOC() and thus must be freed
   by the caller. */

char *
PyTokenizer_FindEncodingFilename(int fd, PyObject *filename)
{
    struct tok_state *tok;
    FILE *fp;
    char *p_start =NULL , *p_end =NULL , *encoding = NULL;

#ifndef PGEN
    fd = _Py_dup(fd);
#else
    fd = dup(fd);
#endif
    if (fd < 0) {
        return NULL;
    }

    fp = fdopen(fd, "r");
    if (fp == NULL) {
        return NULL;
    }
    tok = PyTokenizer_FromFile(fp, NULL, NULL, NULL);
    if (tok == NULL) {
        fclose(fp);
        return NULL;
    }
#ifndef PGEN
    if (filename != NULL) {
        Py_INCREF(filename);
        tok->filename = filename;
    }
    else {
        tok->filename = PyUnicode_FromString("<string>");
        if (tok->filename == NULL) {
            fclose(fp);
            PyTokenizer_Free(tok);
            return encoding;
        }
    }
#endif
    while (tok->lineno < 2 && tok->done == E_OK) {
        PyTokenizer_Get(tok, &p_start, &p_end);
    }
    fclose(fp);
    if (tok->encoding) {
        encoding = (char *)PyMem_MALLOC(strlen(tok->encoding) + 1);
        if (encoding)
        strcpy(encoding, tok->encoding);
    }
    PyTokenizer_Free(tok);
    return encoding;
}

char *
PyTokenizer_FindEncoding(int fd)
{
    return PyTokenizer_FindEncodingFilename(fd, NULL);
}

#ifdef Py_DEBUG

void
tok_dump(int type, char *start, char *end)
{
    printf("%s", _PyParser_TokenNames[type]);
    if (type == NAME || type == NUMBER || type == STRING || type == OP)
        printf("(%.*s)", (int)(end - start), start);
}

#endif

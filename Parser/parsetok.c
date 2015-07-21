/* Parser-tokenizer link implementation */

#include "pgenheaders.h"
#include "tokenizer.h"
#include "node.h"
#include "grammar.h"
#include "parser.h"
#include "parsetok.h"
#include "errcode.h"
#include "graminit.h"


/* Forward */
static node *parsetok(struct tok_state *, grammar *, int, perrdetail *, int *);
static int initerr(perrdetail *err_ret, PyObject * filename);

/* Parse input coming from a string.  Return error code, print some errors. */
node *
PyParser_ParseString(const char *s, grammar *g, int start, perrdetail *err_ret)
{
    return PyParser_ParseStringFlagsFilename(s, NULL, g, start, err_ret, 0);
}

node *
PyParser_ParseStringFlags(const char *s, grammar *g, int start,
                          perrdetail *err_ret, int flags)
{
    return PyParser_ParseStringFlagsFilename(s, NULL,
                                             g, start, err_ret, flags);
}

node *
PyParser_ParseStringFlagsFilename(const char *s, const char *filename,
                          grammar *g, int start,
                          perrdetail *err_ret, int flags)
{
    int iflags = flags;
    return PyParser_ParseStringFlagsFilenameEx(s, filename, g, start,
                                               err_ret, &iflags);
}

node *
PyParser_ParseStringObject(const char *s, PyObject *filename,
                           grammar *g, int start,
                           perrdetail *err_ret, int *flags)
{
    struct tok_state *tok;
    int exec_input = start == file_input;

    if (initerr(err_ret, filename) < 0)
        return NULL;

    if (*flags & PyPARSE_IGNORE_COOKIE)
        tok = PyTokenizer_FromUTF8(s, exec_input);
    else
        tok = PyTokenizer_FromString(s, exec_input);
    if (tok == NULL) {
        err_ret->error = PyErr_Occurred() ? E_DECODE : E_NOMEM;
        return NULL;
    }

#ifndef PGEN
    Py_INCREF(err_ret->filename);
    tok->filename = err_ret->filename;
#endif
    return parsetok(tok, g, start, err_ret, flags);
}

node *
PyParser_ParseStringFlagsFilenameEx(const char *s, const char *filename_str,
                          grammar *g, int start,
                          perrdetail *err_ret, int *flags)
{
    node *n;
    PyObject *filename = NULL;
#ifndef PGEN
    if (filename_str != NULL) {
        filename = PyUnicode_DecodeFSDefault(filename_str);
        if (filename == NULL) {
            err_ret->error = E_ERROR;
            return NULL;
        }
    }
#endif
    n = PyParser_ParseStringObject(s, filename, g, start, err_ret, flags);
#ifndef PGEN
    Py_XDECREF(filename);
#endif
    return n;
}

/* Parse input coming from a file.  Return error code, print some errors. */

node *
PyParser_ParseFile(FILE *fp, const char *filename, grammar *g, int start,
                   const char *ps1, const char *ps2,
                   perrdetail *err_ret)
{
    return PyParser_ParseFileFlags(fp, filename, NULL,
                                   g, start, ps1, ps2, err_ret, 0);
}

node *
PyParser_ParseFileFlags(FILE *fp, const char *filename, const char *enc,
                        grammar *g, int start,
                        const char *ps1, const char *ps2,
                        perrdetail *err_ret, int flags)
{
    int iflags = flags;
    return PyParser_ParseFileFlagsEx(fp, filename, enc, g, start, ps1,
                                     ps2, err_ret, &iflags);
}

node *
PyParser_ParseFileObject(FILE *fp, PyObject *filename,
                         const char *enc, grammar *g, int start,
                         const char *ps1, const char *ps2,
                         perrdetail *err_ret, int *flags)
{
    struct tok_state *tok;

    if (initerr(err_ret, filename) < 0)
        return NULL;

    if ((tok = PyTokenizer_FromFile(fp, enc, ps1, ps2)) == NULL) {
        err_ret->error = E_NOMEM;
        return NULL;
    }
#ifndef PGEN
    Py_INCREF(err_ret->filename);
    tok->filename = err_ret->filename;
#endif
    return parsetok(tok, g, start, err_ret, flags);
}

node *
PyParser_ParseFileFlagsEx(FILE *fp, const char *filename,
                          const char *enc, grammar *g, int start,
                          const char *ps1, const char *ps2,
                          perrdetail *err_ret, int *flags)
{
    node *n;
    PyObject *fileobj = NULL;
#ifndef PGEN
    if (filename != NULL) {
        fileobj = PyUnicode_DecodeFSDefault(filename);
        if (fileobj == NULL) {
            err_ret->error = E_ERROR;
            return NULL;
        }
    }
#endif
    n = PyParser_ParseFileObject(fp, fileobj, enc, g,
                                 start, ps1, ps2, err_ret, flags);
#ifndef PGEN
    Py_XDECREF(fileobj);
#endif
    return n;
}

#ifdef PY_PARSER_REQUIRES_FUTURE_KEYWORD
#if 0
static char with_msg[] =
"%s:%d: Warning: 'with' will become a reserved keyword in Python 2.6\n";

static char as_msg[] =
"%s:%d: Warning: 'as' will become a reserved keyword in Python 2.6\n";

static void
warn(const char *msg, const char *filename, int lineno)
{
    if (filename == NULL)
        filename = "<string>";
    PySys_WriteStderr(msg, filename, lineno);
}
#endif
#endif

/* Parse input coming from the given tokenizer structure.
   Return error code. */



static int parse_uni2Num(int ch)
{

    if (ch >= 0x966 && ch <= 0x96F) //Devanagri - 3 bytes
    {
        ch = ch - 0x966;
        return ch;
    }
    else if(ch >= 0x660 && ch <= 0x669) //Arabic Indic - 3 bytes
    {
        ch = ch - 0x660;
        return ch;
    }
    else if(ch >= 0x9E6 && ch <= 0x9EF) //Bengali - 3 bytes
    {
        ch = ch - 0x9E6;
        return ch;
    }
    return -1;

}


static node *
parsetok(struct tok_state *tok, grammar *g, int start, perrdetail *err_ret,
         int *flags)
{
    parser_state *ps;
    node *n;
    int started = 0;

    if ((ps = PyParser_New(g, start)) == NULL) {
        err_ret->error = E_NOMEM;
        PyTokenizer_Free(tok);
        return NULL;
    }
#ifdef PY_PARSER_REQUIRES_FUTURE_KEYWORD
    if (*flags & PyPARSE_BARRY_AS_BDFL)
        ps->p_flags |= CO_FUTURE_BARRY_AS_BDFL;
#endif

    for (;;) {
        char *a, *b;
        int type;
        size_t len;
        char *str;
        int col_offset;
	int counter=0;
	int i = 0;
	int tempchar=0;
	int bytelim=1;
	int mask=63;
	unsigned long int temp=0;
	unsigned long int temp2=0;
	unsigned int tempfinal=0;
	int firstpass=1;
	int array[3];
	int nonascii=0;
	int finalresult=0;
        int flag = 0;
        int manydigit = 0;

        type = PyTokenizer_Get(tok, &a, &b);
        if (type == ERRORTOKEN) {
            err_ret->error = tok->done;
            break;
        }
        if (type == ENDMARKER && started) {
            type = NEWLINE; /* Add an extra newline */
            started = 0;
            /* Add the right number of dedent tokens,
               except if a certain flag is given --
               codeop.py uses this. */
            if (tok->indent &&
                !(*flags & PyPARSE_DONT_IMPLY_DEDENT))
            {
                tok->pendin = -tok->indent;
                tok->indent = 0;
            }
        }
        else
            started = 1;
        len = b - a; /* XXX this may compute NULL - NULL */
        str = (char *) PyObject_MALLOC(len + 1);
        if (str == NULL) {
            err_ret->error = E_NOMEM;
            break;
        }
        if (len > 0)
            strncpy(str, a, len);
        str[len] = '\0';


        //printf("Before for-loop str = %s\n",str);

        for(counter = 0, finalresult=0, nonascii=0, firstpass =1, manydigit=0; counter < len; counter++)
        {

            if(str[counter] == -49 && str[counter+1] == -128 ) //π Operator
            {
                str = (char *) PyObject_MALLOC(len + 3);
                str[manydigit++]= 51;
                str[manydigit++]= 46;
                str[manydigit++]= 49;
                str[manydigit++]= 52;
                // printf("After Pi Adjustment: str = %s\n",str );
                type = NUMBER;
                break;
            }

            else if(str[counter] == -50 && str[counter+1] == -93 ) //Σ Operator
            {
                /*>>> utf(931)
                  (206, 163, None, None)
                */
                str = (char *) PyObject_MALLOC(len + 3);
                str[manydigit++]= 's';
                str[manydigit++]= 'u';
                str[manydigit++]= 'm';
                //printf("After Sigma Adjustment: str = %s\n",str );
                type = 1;
                break;
            }

            else if(str[counter] == -50 && str[counter+1] == -69 ) //λ Operator
            {
                /*>>> utf(955)
                  (206, 187, None, None)
                */
                str = (char *) PyObject_MALLOC(len + 6);
                str[manydigit++]= 'l';
                str[manydigit++]= 'a';
                str[manydigit++]= 'm';
                str[manydigit++]= 'b';
                str[manydigit++]= 'd';
                str[manydigit++]= 'a';
                //printf("After Lamda Adjustment: str = %s\n",str );
                type = 1;
                break;
            }

            else if(str[counter] == -30 && str[counter+1] == -120 && str[counter+2] == -120) //∈ Operator
            {
                str[manydigit++]= 'i';
                str[manydigit++]= 'n';
                type = 1;
                break;
            }

            else if(str[counter] == -30 && str[counter+1] == -120) //∪ Operator
            {
                if(str[counter+2] == -86)
                {
                    str[manydigit++]= '|';
                    type = 18;
                    break;
                }
                else if(str[counter+2] == -87) //∩ Operator
                {
                    str[manydigit++]= '&';
                    type = 19;
                    break;
                }
            }

            else if(str[0] == -30 && str[1] == -119 && str[2]==-96 )//not equal to operator
            {
                str[manydigit++] = '!';
                str[manydigit++] = 61;
                type = NOTEQUAL;
                break;
            }
           else if(str[0] == -30 && str[1] == -119 && str[2]==-92 )
	     {
                  str[manydigit++] = '<';
                  str[manydigit++] = 61;
                  type = 29; 
                    break;
             } 
            else if(str[0] == -30 && str[1] == -119 && str[2]==-91 )
	     {
                  str[manydigit++] = '>';
                  str[manydigit++] = 61;
                  type =30;
                    break;
             }   
            tempchar = str[counter];
            tempchar = tempchar & 0x000000FF;

            if (tempchar < 128)
	    {
                //ASCII Value
                nonascii = 0;
                str[manydigit] = tempchar;
                manydigit++;
	    }
            else if (tempchar >= 224)
	    {
                //printf("3 bytes in parsetok.c!\n");
                bytelim = 3;
                mask=15; //0000 1111
                array[0] = tempchar;
                array[1] = str[counter+1];
                array[2] = str[counter+2];
                nonascii = 1;
	    }
            else if (tempchar >= 192)
	    {
                //printf("2 bytes in parsetok.c!\n");
                bytelim = 2;
                mask=31; //0001 1111
                array[0] = tempchar;
                array[1] = str[counter+1];
                nonascii = 1;
	    }
            else if (tempchar >= 128)
	    {
                //printf("One byte in parsetok.c!\n");
                //0011 1111
                array[0] = tempchar;
                nonascii = 1;
	    }

            if(nonascii == 1)
	    {
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
                //printf("The codepoint in decimal in parsetok.c is %u\n", tempfinal);
                //printf("The codepoint in hex is in parsetok.c %x\n", tempfinal);
                finalresult = (int)parse_uni2Num(tempfinal) + 48;
                //printf("The value of finalresult in parsetok.c is %d\n", finalresult);
                str[manydigit] = finalresult;
                manydigit++;
                counter = counter + (bytelim - 1);
	    }

        }
        str[manydigit]='\0';
	//printf("After for-loop str = %s\n",str);





#ifdef PY_PARSER_REQUIRES_FUTURE_KEYWORD
        if (type == NOTEQUAL) {
            if (!(ps->p_flags & CO_FUTURE_BARRY_AS_BDFL) &&
                            strcmp(str, "!=")) {
                PyObject_FREE(str);
                err_ret->error = E_SYNTAX;
                break;
            }
            else if ((ps->p_flags & CO_FUTURE_BARRY_AS_BDFL) &&
                            strcmp(str, "<>")) {
                PyObject_FREE(str);
                err_ret->text = "with Barry as BDFL, use '<>' "
                                "instead of '!='";
                err_ret->error = E_SYNTAX;
                break;
            }
        }
#endif
        if (a >= tok->line_start)
            col_offset = Py_SAFE_DOWNCAST(a - tok->line_start,
                                          Py_intptr_t, int);
        else
            col_offset = -1;

        if ((err_ret->error =
             PyParser_AddToken(ps, (int)type, str,
                               tok->lineno, col_offset,
                               &(err_ret->expected))) != E_OK) {
            if (err_ret->error != E_DONE) {
                PyObject_FREE(str);
                err_ret->token = type;
            }
            break;
        }
    }

    if (err_ret->error == E_DONE) {
        n = ps->p_tree;
        ps->p_tree = NULL;

#ifndef PGEN
        /* Check that the source for a single input statement really
           is a single statement by looking at what is left in the
           buffer after parsing.  Trailing whitespace and comments
           are OK.  */
        if (start == single_input) {
            char *cur = tok->cur;
            char c = *tok->cur;

            for (;;) {
                while (c == ' ' || c == '\t' || c == '\n' || c == '\014')
                    c = *++cur;

                if (!c)
                    break;

                if (c != '#') {
                    err_ret->error = E_BADSINGLE;
                    PyNode_Free(n);
                    n = NULL;
                    break;
                }

                /* Suck up comment. */
                while (c && c != '\n')
                    c = *++cur;
            }
        }
#endif
    }
    else
        n = NULL;

#ifdef PY_PARSER_REQUIRES_FUTURE_KEYWORD
    *flags = ps->p_flags;
#endif
    PyParser_Delete(ps);

    if (n == NULL) {
        if (tok->done == E_EOF)
            err_ret->error = E_EOF;
        err_ret->lineno = tok->lineno;
        if (tok->buf != NULL) {
            size_t len;
            assert(tok->cur - tok->buf < INT_MAX);
            err_ret->offset = (int)(tok->cur - tok->buf);
            len = tok->inp - tok->buf;
            err_ret->text = (char *) PyObject_MALLOC(len + 1);
            if (err_ret->text != NULL) {
                if (len > 0)
                    strncpy(err_ret->text, tok->buf, len);
                err_ret->text[len] = '\0';
            }
        }
    } else if (tok->encoding != NULL) {
        /* 'nodes->n_str' uses PyObject_*, while 'tok->encoding' was
         * allocated using PyMem_
         */
        node* r = PyNode_New(encoding_decl);
        if (r)
            r->n_str = PyObject_MALLOC(strlen(tok->encoding)+1);
        if (!r || !r->n_str) {
            err_ret->error = E_NOMEM;
            if (r)
                PyObject_FREE(r);
            n = NULL;
            goto done;
        }
        strcpy(r->n_str, tok->encoding);
        PyMem_FREE(tok->encoding);
        tok->encoding = NULL;
        r->n_nchildren = 1;
        r->n_child = n;
        n = r;
    }

done:
    PyTokenizer_Free(tok);

    return n;
}

static int
initerr(perrdetail *err_ret, PyObject *filename)
{
    err_ret->error = E_OK;
    err_ret->lineno = 0;
    err_ret->offset = 0;
    err_ret->text = NULL;
    err_ret->token = -1;
    err_ret->expected = -1;
#ifndef PGEN
    if (filename) {
        Py_INCREF(filename);
        err_ret->filename = filename;
    }
    else {
        err_ret->filename = PyUnicode_FromString("<string>");
        if (err_ret->filename == NULL) {
            err_ret->error = E_ERROR;
            return -1;
        }
    }
#endif
    return 0;
}

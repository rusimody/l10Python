
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



static double parse_uni2Num(int ch)
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
	
	int manydigit=0;

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

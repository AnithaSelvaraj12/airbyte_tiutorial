/*

Unicode implementation based on original code by Fredrik Lundh,
modified by Marc-Andre Lemburg <mal@lemburg.com>.

Major speed upgrades to the method implementations at the Reykjavik
NeedForSpeed sprint, by Fredrik Lundh and Andrew Dalke.

Copyright (c) Corporation for National Research Initiatives.

--------------------------------------------------------------------
The original string type implementation is:

  Copyright (c) 1999 by Secret Labs AB
  Copyright (c) 1999 by Fredrik Lundh

By obtaining, using, and/or copying this software and/or its
associated documentation, you agree that you have read, understood,
and will comply with the following terms and conditions:

Permission to use, copy, modify, and distribute this software and its
associated documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appears in all
copies, and that both that copyright notice and this permission notice
appear in supporting documentation, and that the name of Secret Labs
AB or the author not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior
permission.

SECRET LABS AB AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO
THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS.  IN NO EVENT SHALL SECRET LABS AB OR THE AUTHOR BE LIABLE FOR
ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
--------------------------------------------------------------------

*/

#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "pycore_abstract.h"      // _PyIndex_Check()
#include "pycore_atomic_funcs.h"  // _Py_atomic_size_get()
#include "pycore_bytes_methods.h" // _Py_bytes_lower()
#include "pycore_format.h"        // F_LJUST
#include "pycore_initconfig.h"    // _PyStatus_OK()
#include "pycore_interp.h"        // PyInterpreterState.fs_codec
#include "pycore_object.h"        // _PyObject_GC_TRACK()
#include "pycore_pathconfig.h"    // _Py_DumpPathConfig()
#include "pycore_pylifecycle.h"   // _Py_SetFileSystemEncoding()
#include "pycore_pystate.h"       // _PyInterpreterState_GET()
#include "pycore_ucnhash.h"       // _PyUnicode_Name_CAPI
#include "stringlib/eq.h"         // unicode_eq()

#ifdef MS_WINDOWS
#include <windows.h>
#endif

#ifdef HAVE_NON_UNICODE_WCHAR_T_REPRESENTATION
#include "pycore_fileutils.h"     // _Py_LocaleUsesNonUnicodeWchar()
#endif

/* Uncomment to display statistics on interned strings at exit
   in _PyUnicode_ClearInterned(). */
/* #define INTERNED_STATS 1 */


/*[clinic input]
class str "PyObject *" "&PyUnicode_Type"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=4884c934de622cf6]*/

/*[python input]
class Py_UCS4_converter(CConverter):
    type = 'Py_UCS4'
    converter = 'convert_uc'

    def converter_init(self):
        if self.default is not unspecified:
            self.c_default = ascii(self.default)
            if len(self.c_default) > 4 or self.c_default[0] != "'":
                self.c_default = hex(ord(self.default))

[python start generated code]*/
/*[python end generated code: output=da39a3ee5e6b4b0d input=88f5dd06cd8e7a61]*/

/* --- Globals ------------------------------------------------------------

NOTE: In the interpreter's initialization phase, some globals are currently
      initialized dynamically as needed. In the process Unicode objects may
      be created before the Unicode type is ready.

*/


#ifdef __cplusplus
extern "C" {
#endif

// Maximum code point of Unicode 6.0: 0x10ffff (1,114,111).
// The value must be the same in fileutils.c.
#define MAX_UNICODE 0x10ffff

#ifdef Py_DEBUG
#  define _PyUnicode_CHECK(op) _PyUnicode_CheckConsistency(op, 0)
#else
#  define _PyUnicode_CHECK(op) PyUnicode_Check(op)
#endif

#define _PyUnicode_UTF8(op)                             \
    (((PyCompactUnicodeObject*)(op))->utf8)
#define PyUnicode_UTF8(op)                              \
    (assert(_PyUnicode_CHECK(op)),                      \
     assert(PyUnicode_IS_READY(op)),                    \
     PyUnicode_IS_COMPACT_ASCII(op) ?                   \
         ((char*)((PyASCIIObject*)(op) + 1)) :          \
         _PyUnicode_UTF8(op))
#define _PyUnicode_UTF8_LENGTH(op)                      \
    (((PyCompactUnicodeObject*)(op))->utf8_length)
#define PyUnicode_UTF8_LENGTH(op)                       \
    (assert(_PyUnicode_CHECK(op)),                      \
     assert(PyUnicode_IS_READY(op)),                    \
     PyUnicode_IS_COMPACT_ASCII(op) ?                   \
         ((PyASCIIObject*)(op))->length :               \
         _PyUnicode_UTF8_LENGTH(op))
#define _PyUnicode_WSTR(op)                             \
    (((PyASCIIObject*)(op))->wstr)

/* Don't use deprecated macro of unicodeobject.h */
#undef PyUnicode_WSTR_LENGTH
#define PyUnicode_WSTR_LENGTH(op) \
    (PyUnicode_IS_COMPACT_ASCII(op) ?                  \
     ((PyASCIIObject*)op)->length :                    \
     ((PyCompactUnicodeObject*)op)->wstr_length)
#define _PyUnicode_WSTR_LENGTH(op)                      \
    (((PyCompactUnicodeObject*)(op))->wstr_length)
#define _PyUnicode_LENGTH(op)                           \
    (((PyASCIIObject *)(op))->length)
#define _PyUnicode_STATE(op)                            \
    (((PyASCIIObject *)(op))->state)
#define _PyUnicode_HASH(op)                             \
    (((PyASCIIObject *)(op))->hash)
#define _PyUnicode_KIND(op)                             \
    (assert(_PyUnicode_CHECK(op)),                      \
     ((PyASCIIObject *)(op))->state.kind)
#define _PyUnicode_GET_LENGTH(op)                       \
    (assert(_PyUnicode_CHECK(op)),                      \
     ((PyASCIIObject *)(op))->length)
#define _PyUnicode_DATA_ANY(op)                         \
    (((PyUnicodeObject*)(op))->data.any)

#undef PyUnicode_READY
#define PyUnicode_READY(op)                             \
    (assert(_PyUnicode_CHECK(op)),                      \
     (PyUnicode_IS_READY(op) ?                          \
      0 :                                               \
      _PyUnicode_Ready(op)))

#define _PyUnicode_SHARE_UTF8(op)                       \
    (assert(_PyUnicode_CHECK(op)),                      \
     assert(!PyUnicode_IS_COMPACT_ASCII(op)),           \
     (_PyUnicode_UTF8(op) == PyUnicode_DATA(op)))
#define _PyUnicode_SHARE_WSTR(op)                       \
    (assert(_PyUnicode_CHECK(op)),                      \
     (_PyUnicode_WSTR(unicode) == PyUnicode_DATA(op)))

/* true if the Unicode object has an allocated UTF-8 memory block
   (not shared with other data) */
#define _PyUnicode_HAS_UTF8_MEMORY(op)                  \
    ((!PyUnicode_IS_COMPACT_ASCII(op)                   \
      && _PyUnicode_UTF8(op)                            \
      && _PyUnicode_UTF8(op) != PyUnicode_DATA(op)))

/* true if the Unicode object has an allocated wstr memory block
   (not shared with other data) */
#define _PyUnicode_HAS_WSTR_MEMORY(op)                  \
    ((_PyUnicode_WSTR(op) &&                            \
      (!PyUnicode_IS_READY(op) ||                       \
       _PyUnicode_WSTR(op) != PyUnicode_DATA(op))))

/* Generic helper macro to convert characters of different types.
   from_type and to_type have to be valid type names, begin and end
   are pointers to the source characters which should be of type
   "from_type *".  to is a pointer of type "to_type *" and points to the
   buffer where the result characters are written to. */
#define _PyUnicode_CONVERT_BYTES(from_type, to_type, begin, end, to) \
    do {                                                \
        to_type *_to = (to_type *)(to);                \
        const from_type *_iter = (const from_type *)(begin);\
        const from_type *_end = (const from_type *)(end);\
        Py_ssize_t n = (_end) - (_iter);                \
        const from_type *_unrolled_end =                \
            _iter + _Py_SIZE_ROUND_DOWN(n, 4);          \
        while (_iter < (_unrolled_end)) {               \
            _to[0] = (to_type) _iter[0];                \
            _to[1] = (to_type) _iter[1];                \
            _to[2] = (to_type) _iter[2];                \
            _to[3] = (to_type) _iter[3];                \
            _iter += 4; _to += 4;                       \
        }                                               \
        while (_iter < (_end))                          \
            *_to++ = (to_type) *_iter++;                \
    } while (0)

#ifdef MS_WINDOWS
   /* On Windows, overallocate by 50% is the best factor */
#  define OVERALLOCATE_FACTOR 2
#else
   /* On Linux, overallocate by 25% is the best factor */
#  define OVERALLOCATE_FACTOR 4
#endif

/* bpo-40521: Interned strings are shared by all interpreters. */
#ifndef EXPERIMENTAL_ISOLATED_SUBINTERPRETERS
#  define INTERNED_STRINGS
#endif

/* This dictionary holds all interned unicode strings.  Note that references
   to strings in this dictionary are *not* counted in the string's ob_refcnt.
   When the interned string reaches a refcnt of 0 the string deallocation
   function will delete the reference from this dictionary.

   Another way to look at this is that to say that the actual reference
   count of a string is:  s->ob_refcnt + (s->state ? 2 : 0)
*/
#ifdef INTERNED_STRINGS
static PyObject *interned = NULL;
#endif

static struct _Py_unicode_state*
get_unicode_state(void)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    return &interp->unicode;
}


// Return a borrowed reference to the empty string singleton.
static inline PyObject* unicode_get_empty(void)
{
    struct _Py_unicode_state *state = get_unicode_state();
    // unicode_get_empty() must not be called before _PyUnicode_Init()
    // or after _PyUnicode_Fini()
    assert(state->empty_string != NULL);
    return state->empty_string;
}


// Return a strong reference to the empty string singleton.
static inline PyObject* unicode_new_empty(void)
{
    PyObject *empty = unicode_get_empty();
    Py_INCREF(empty);
    return empty;
}

#define _Py_RETURN_UNICODE_EMPTY()   \
    do {                             \
        return unicode_new_empty();  \
    } while (0)

static inline void
unicode_fill(enum PyUnicode_Kind kind, void *data, Py_UCS4 value,
             Py_ssize_t start, Py_ssize_t length)
{
    assert(0 <= start);
    assert(kind != PyUnicode_WCHAR_KIND);
    switch (kind) {
    case PyUnicode_1BYTE_KIND: {
        assert(value <= 0xff);
        Py_UCS1 ch = (unsigned char)value;
        Py_UCS1 *to = (Py_UCS1 *)data + start;
        memset(to, ch, length);
        break;
    }
    case PyUnicode_2BYTE_KIND: {
        assert(value <= 0xffff);
        Py_UCS2 ch = (Py_UCS2)value;
        Py_UCS2 *to = (Py_UCS2 *)data + start;
        const Py_UCS2 *end = to + length;
        for (; to < end; ++to) *to = ch;
        break;
    }
    case PyUnicode_4BYTE_KIND: {
        assert(value <= MAX_UNICODE);
        Py_UCS4 ch = value;
        Py_UCS4 * to = (Py_UCS4 *)data + start;
        const Py_UCS4 *end = to + length;
        for (; to < end; ++to) *to = ch;
        break;
    }
    default: Py_UNREACHABLE();
    }
}


/* Forward declaration */
static inline int
_PyUnicodeWriter_WriteCharInline(_PyUnicodeWriter *writer, Py_UCS4 ch);
static inline void
_PyUnicodeWriter_InitWithBuffer(_PyUnicodeWriter *writer, PyObject *buffer);
static PyObject *
unicode_encode_utf8(PyObject *unicode, _Py_error_handler error_handler,
                    const char *errors);
static PyObject *
unicode_decode_utf8(const char *s, Py_ssize_t size,
                    _Py_error_handler error_handler, const char *errors,
                    Py_ssize_t *consumed);

/* Fast detection of the most frequent whitespace characters */
const unsigned char _Py_ascii_whitespace[] = {
    0, 0, 0, 0, 0, 0, 0, 0,
/*     case 0x0009: * CHARACTER TABULATION */
/*     case 0x000A: * LINE FEED */
/*     case 0x000B: * LINE TABULATION */
/*     case 0x000C: * FORM FEED */
/*     case 0x000D: * CARRIAGE RETURN */
    0, 1, 1, 1, 1, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
/*     case 0x001C: * FILE SEPARATOR */
/*     case 0x001D: * GROUP SEPARATOR */
/*     case 0x001E: * RECORD SEPARATOR */
/*     case 0x001F: * UNIT SEPARATOR */
    0, 0, 0, 0, 1, 1, 1, 1,
/*     case 0x0020: * SPACE */
    1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,

    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

/* forward */
static PyUnicodeObject *_PyUnicode_New(Py_ssize_t length);
static PyObject* get_latin1_char(unsigned char ch);
static int unicode_modifiable(PyObject *unicode);


static PyObject *
_PyUnicode_FromUCS1(const Py_UCS1 *s, Py_ssize_t size);
static PyObject *
_PyUnicode_FromUCS2(const Py_UCS2 *s, Py_ssize_t size);
static PyObject *
_PyUnicode_FromUCS4(const Py_UCS4 *s, Py_ssize_t size);

static PyObject *
unicode_encode_call_errorhandler(const char *errors,
       PyObject **errorHandler,const char *encoding, const char *reason,
       PyObject *unicode, PyObject **exceptionObject,
       Py_ssize_t startpos, Py_ssize_t endpos, Py_ssize_t *newpos);

static void
raise_encode_exception(PyObject **exceptionObject,
                       const char *encoding,
                       PyObject *unicode,
                       Py_ssize_t startpos, Py_ssize_t endpos,
                       const char *reason);

/* Same for linebreaks */
static const unsigned char ascii_linebreak[] = {
    0, 0, 0, 0, 0, 0, 0, 0,
/*         0x000A, * LINE FEED */
/*         0x000B, * LINE TABULATION */
/*         0x000C, * FORM FEED */
/*         0x000D, * CARRIAGE RETURN */
    0, 0, 1, 1, 1, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
/*         0x001C, * FILE SEPARATOR */
/*         0x001D, * GROUP SEPARATOR */
/*         0x001E, * RECORD SEPARATOR */
    0, 0, 0, 0, 1, 1, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,

    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

static int convert_uc(PyObject *obj, void *addr);

#include "clinic/unicodeobject.c.h"

_Py_error_handler
_Py_GetErrorHandler(const char *errors)
{
    if (errors == NULL || strcmp(errors, "strict") == 0) {
        return _Py_ERROR_STRICT;
    }
    if (strcmp(errors, "surrogateescape") == 0) {
        return _Py_ERROR_SURROGATEESCAPE;
    }
    if (strcmp(errors, "replace") == 0) {
        return _Py_ERROR_REPLACE;
    }
    if (strcmp(errors, "ignore") == 0) {
        return _Py_ERROR_IGNORE;
    }
    if (strcmp(errors, "backslashreplace") == 0) {
        return _Py_ERROR_BACKSLASHREPLACE;
    }
    if (strcmp(errors, "surrogatepass") == 0) {
        return _Py_ERROR_SURROGATEPASS;
    }
    if (strcmp(errors, "xmlcharrefreplace") == 0) {
        return _Py_ERROR_XMLCHARREFREPLACE;
    }
    return _Py_ERROR_OTHER;
}


static _Py_error_handler
get_error_handler_wide(const wchar_t *errors)
{
    if (errors == NULL || wcscmp(errors, L"strict") == 0) {
        return _Py_ERROR_STRICT;
    }
    if (wcscmp(errors, L"surrogateescape") == 0) {
        return _Py_ERROR_SURROGATEESCAPE;
    }
    if (wcscmp(errors, L"replace") == 0) {
        return _Py_ERROR_REPLACE;
    }
    if (wcscmp(errors, L"ignore") == 0) {
        return _Py_ERROR_IGNORE;
    }
    if (wcscmp(errors, L"backslashreplace") == 0) {
        return _Py_ERROR_BACKSLASHREPLACE;
    }
    if (wcscmp(errors, L"surrogatepass") == 0) {
        return _Py_ERROR_SURROGATEPASS;
    }
    if (wcscmp(errors, L"xmlcharrefreplace") == 0) {
        return _Py_ERROR_XMLCHARREFREPLACE;
    }
    return _Py_ERROR_OTHER;
}


static inline int
unicode_check_encoding_errors(const char *encoding, const char *errors)
{
    if (encoding == NULL && errors == NULL) {
        return 0;
    }

    PyInterpreterState *interp = _PyInterpreterState_GET();
#ifndef Py_DEBUG
    /* In release mode, only check in development mode (-X dev) */
    if (!_PyInterpreterState_GetConfig(interp)->dev_mode) {
        return 0;
    }
#else
    /* Always check in debug mode */
#endif

    /* Avoid calling _PyCodec_Lookup() and PyCodec_LookupError() before the
       codec registry is ready: before_PyUnicode_InitEncodings() is called. */
    if (!interp->unicode.fs_codec.encoding) {
        return 0;
    }

    /* Disable checks during Python finalization. For example, it allows to
       call _PyObject_Dump() during finalization for debugging purpose. */
    if (interp->finalizing) {
        return 0;
    }

    if (encoding != NULL) {
        PyObject *handler = _PyCodec_Lookup(encoding);
        if (handler == NULL) {
            return -1;
        }
        Py_DECREF(handler);
    }

    if (errors != NULL) {
        PyObject *handler = PyCodec_LookupError(errors);
        if (handler == NULL) {
            return -1;
        }
        Py_DECREF(handler);
    }
    return 0;
}


int
_PyUnicode_CheckConsistency(PyObject *op, int check_content)
{
#define CHECK(expr) \
    do { if (!(expr)) { _PyObject_ASSERT_FAILED_MSG(op, Py_STRINGIFY(expr)); } } while (0)

    PyASCIIObject *ascii;
    unsigned int kind;

    assert(op != NULL);
    CHECK(PyUnicode_Check(op));

    ascii = (PyASCIIObject *)op;
    kind = ascii->state.kind;

    if (ascii->state.ascii == 1 && ascii->state.compact == 1) {
        CHECK(kind == PyUnicode_1BYTE_KIND);
        CHECK(ascii->state.ready == 1);
    }
    else {
        PyCompactUnicodeObject *compact = (PyCompactUnicodeObject *)op;
        void *data;

        if (ascii->state.compact == 1) {
            data = compact + 1;
            CHECK(kind == PyUnicode_1BYTE_KIND
                                 || kind == PyUnicode_2BYTE_KIND
                                 || kind == PyUnicode_4BYTE_KIND);
            CHECK(ascii->state.ascii == 0);
            CHECK(ascii->state.ready == 1);
            CHECK(compact->utf8 != data);
        }
        else {
            PyUnicodeObject *unicode = (PyUnicodeObject *)op;

            data = unicode->data.any;
            if (kind == PyUnicode_WCHAR_KIND) {
                CHECK(ascii->length == 0);
                CHECK(ascii->hash == -1);
                CHECK(ascii->state.compact == 0);
                CHECK(ascii->state.ascii == 0);
                CHECK(ascii->state.ready == 0);
                CHECK(ascii->state.interned == SSTATE_NOT_INTERNED);
                CHECK(ascii->wstr != NULL);
                CHECK(data == NULL);
                CHECK(compact->utf8 == NULL);
            }
            else {
                CHECK(kind == PyUnicode_1BYTE_KIND
                                     || kind == PyUnicode_2BYTE_KIND
                                     || kind == PyUnicode_4BYTE_KIND);
                CHECK(ascii->state.compact == 0);
                CHECK(ascii->state.ready == 1);
                CHECK(data != NULL);
                if (ascii->state.ascii) {
                    CHECK(compact->utf8 == data);
                    CHECK(compact->utf8_length == ascii->length);
                }
                else
                    CHECK(compact->utf8 != data);
            }
        }
        if (kind != PyUnicode_WCHAR_KIND) {
            if (
#if SIZEOF_WCHAR_T == 2
                kind == PyUnicode_2BYTE_KIND
#else
                kind == PyUnicode_4BYTE_KIND
#endif
               )
            {
                CHECK(ascii->wstr == data);
                CHECK(compact->wstr_length == ascii->length);
            } else
                CHECK(ascii->wstr != data);
        }

        if (compact->utf8 == NULL)
            CHECK(compact->utf8_length == 0);
        if (ascii->wstr == NULL)
            CHECK(compact->wstr_length == 0);
    }

    /* check that the best kind is used: O(n) operation */
    if (check_content && kind != PyUnicode_WCHAR_KIND) {
        Py_ssize_t i;
        Py_UCS4 maxchar = 0;
        const void *data;
        Py_UCS4 ch;

        data = PyUnicode_DATA(ascii);
        for (i=0; i < ascii->length; i++)
        {
            ch = PyUnicode_READ(kind, data, i);
            if (ch > maxchar)
                maxchar = ch;
        }
        if (kind == PyUnicode_1BYTE_KIND) {
            if (ascii->state.ascii == 0) {
                CHECK(maxchar >= 128);
                CHECK(maxchar <= 255);
            }
            else
                CHECK(maxchar < 128);
        }
        else if (kind == PyUnicode_2BYTE_KIND) {
            CHECK(maxchar >= 0x100);
            CHECK(maxchar <= 0xFFFF);
        }
        else {
            CHECK(maxchar >= 0x10000);
            CHECK(maxchar <= MAX_UNICODE);
        }
        CHECK(PyUnicode_READ(kind, data, ascii->length) == 0);
    }
    return 1;

#undef CHECK
}


static PyObject*
unicode_result_wchar(PyObject *unicode)
{
#ifndef Py_DEBUG
    Py_ssize_t len;

    len = _PyUnicode_WSTR_LENGTH(unicode);
    if (len == 0) {
        Py_DECREF(unicode);
        _Py_RETURN_UNICODE_EMPTY();
    }

    if (len == 1) {
        wchar_t ch = _PyUnicode_WSTR(unicode)[0];
        if ((Py_UCS4)ch < 256) {
            Py_DECREF(unicode);
            return get_latin1_char((unsigned char)ch);
        }
    }

    if (_PyUnicode_Ready(unicode) < 0) {
        Py_DECREF(unicode);
        return NULL;
    }
#else
    assert(Py_REFCNT(unicode) == 1);

    /* don't make the result ready in debug mode to ensure that the caller
       makes the string ready before using it */
    assert(_PyUnicode_CheckConsistency(unicode, 1));
#endif
    return unicode;
}

static PyObject*
unicode_result_ready(PyObject *unicode)
{
    Py_ssize_t length;

    length = PyUnicode_GET_LENGTH(unicode);
    if (length == 0) {
        PyObject *empty = unicode_get_empty();
        if (unicode != empty) {
            Py_DECREF(unicode);
            Py_INCREF(empty);
        }
        return empty;
    }

    if (length == 1) {
        int kind = PyUnicode_KIND(unicode);
        if (kind == PyUnicode_1BYTE_KIND) {
            const Py_UCS1 *data = PyUnicode_1BYTE_DATA(unicode);
            Py_UCS1 ch = data[0];
            struct _Py_unicode_state *state = get_unicode_state();
            PyObject *latin1_char = state->latin1[ch];
            if (latin1_char != NULL) {
                if (unicode != latin1_char) {
                    Py_INCREF(latin1_char);
                    Py_DECREF(unicode);
                }
                return latin1_char;
            }
            else {
                assert(_PyUnicode_CheckConsistency(unicode, 1));
                Py_INCREF(unicode);
                state->latin1[ch] = unicode;
                return unicode;
            }
        }
        else {
            assert(PyUnicode_READ_CHAR(unicode, 0) >= 256);
        }
    }

    assert(_PyUnicode_CheckConsistency(unicode, 1));
    return unicode;
}

static PyObject*
unicode_result(PyObject *unicode)
{
    assert(_PyUnicode_CHECK(unicode));
    if (PyUnicode_IS_READY(unicode))
        return unicode_result_ready(unicode);
    else
        return unicode_result_wchar(unicode);
}

static PyObject*
unicode_result_unchanged(PyObject *unicode)
{
    if (PyUnicode_CheckExact(unicode)) {
        if (PyUnicode_READY(unicode) == -1)
            return NULL;
        Py_INCREF(unicode);
        return unicode;
    }
    else
        /* Subtype -- return genuine unicode string with the same value. */
        return _PyUnicode_Copy(unicode);
}

/* Implementation of the "backslashreplace" error handler for 8-bit encodings:
   ASCII, Latin1, UTF-8, etc. */
static char*
backslashreplace(_PyBytesWriter *writer, char *str,
                 PyObject *unicode, Py_ssize_t collstart, Py_ssize_t collend)
{
    Py_ssize_t size, i;
    Py_UCS4 ch;
    enum PyUnicode_Kind kind;
    const void *data;

    assert(PyUnicode_IS_READY(unicode));
    kind = PyUnicode_KIND(unicode);
    data = PyUnicode_DATA(unicode);

    size = 0;
    /* determine replacement size */
    for (i = collstart; i < collend; ++i) {
        Py_ssize_t incr;

        ch = PyUnicode_READ(kind, data, i);
        if (ch < 0x100)
            incr = 2+2;
        else if (ch < 0x10000)
            incr = 2+4;
        else {
            assert(ch <= MAX_UNICODE);
            incr = 2+8;
        }
        if (size > PY_SSIZE_T_MAX - incr) {
            PyErr_SetString(PyExc_OverflowError,
                            "encoded result is too long for a Python string");
            return NULL;
        }
        size += incr;
    }

    str = _PyBytesWriter_Prepare(writer, str, size);
    if (str == NULL)
        return NULL;

    /* generate replacement */
    for (i = collstart; i < collend; ++i) {
        ch = PyUnicode_READ(kind, data, i);
        *str++ = '\\';
        if (ch >= 0x00010000) {
            *str++ = 'U';
            *str++ = Py_hexdigits[(ch>>28)&0xf];
            *str++ = Py_hexdigits[(ch>>24)&0xf];
            *str++ = Py_hexdigits[(ch>>20)&0xf];
            *str++ = Py_hexdigits[(ch>>16)&0xf];
            *str++ = Py_hexdigits[(ch>>12)&0xf];
            *str++ = Py_hexdigits[(ch>>8)&0xf];
        }
        else if (ch >= 0x100) {
            *str++ = 'u';
            *str++ = Py_hexdigits[(ch>>12)&0xf];
            *str++ = Py_hexdigits[(ch>>8)&0xf];
        }
        else
            *str++ = 'x';
        *str++ = Py_hexdigits[(ch>>4)&0xf];
        *str++ = Py_hexdigits[ch&0xf];
    }
    return str;
}

/* Implementation of the "xmlcharrefreplace" error handler for 8-bit encodings:
   ASCII, Latin1, UTF-8, etc. */
static char*
xmlcharrefreplace(_PyBytesWriter *writer, char *str,
                  PyObject *unicode, Py_ssize_t collstart, Py_ssize_t collend)
{
    Py_ssize_t size, i;
    Py_UCS4 ch;
    enum PyUnicode_Kind kind;
    const void *data;

    assert(PyUnicode_IS_READY(unicode));
    kind = PyUnicode_KIND(unicode);
    data = PyUnicode_DATA(unicode);

    size = 0;
    /* determine replacement size */
    for (i = collstart; i < collend; ++i) {
        Py_ssize_t incr;

        ch = PyUnicode_READ(kind, data, i);
        if (ch < 10)
            incr = 2+1+1;
        else if (ch < 100)
            incr = 2+2+1;
        else if (ch < 1000)
            incr = 2+3+1;
        else if (ch < 10000)
            incr = 2+4+1;
        else if (ch < 100000)
            incr = 2+5+1;
        else if (ch < 1000000)
            incr = 2+6+1;
        else {
            assert(ch <= MAX_UNICODE);
            incr = 2+7+1;
        }
        if (size > PY_SSIZE_T_MAX - incr) {
            PyErr_SetString(PyExc_OverflowError,
                            "encoded result is too long for a Python string");
            return NULL;
        }
        size += incr;
    }

    str = _PyBytesWriter_Prepare(writer, str, size);
    if (str == NULL)
        return NULL;

    /* generate replacement */
    for (i = collstart; i < collend; ++i) {
        size = sprintf(str, "&#%d;", PyUnicode_READ(kind, data, i));
        if (size < 0) {
            return NULL;
        }
        str += size;
    }
    return str;
}

/* --- Bloom Filters ----------------------------------------------------- */

/* stuff to implement simple "bloom filters" for Unicode characters.
   to keep things simple, we use a single bitmask, using the least 5
   bits from each unicode characters as the bit index. */

/* the linebreak mask is set up by _PyUnicode_Init() below */

#if LONG_BIT >= 128
#define BLOOM_WIDTH 128
#elif LONG_BIT >= 64
#define BLOOM_WIDTH 64
#elif LONG_BIT >= 32
#define BLOOM_WIDTH 32
#else
#error "LONG_BIT is smaller than 32"
#endif

#define BLOOM_MASK unsigned long

static BLOOM_MASK bloom_linebreak = ~(BLOOM_MASK)0;

#define BLOOM(mask, ch)     ((mask &  (1UL << ((ch) & (BLOOM_WIDTH - 1)))))

#define BLOOM_LINEBREAK(ch)                                             \
    ((ch) < 128U ? ascii_linebreak[(ch)] :                              \
     (BLOOM(bloom_linebreak, (ch)) && Py_UNICODE_ISLINEBREAK(ch)))

static inline BLOOM_MASK
make_bloom_mask(int kind, const void* ptr, Py_ssize_t len)
{
#define BLOOM_UPDATE(TYPE, MASK, PTR, LEN)             \
    do {                                               \
        TYPE *data = (TYPE *)PTR;                      \
        TYPE *end = data + LEN;                        \
        Py_UCS4 ch;                                    \
        for (; data != end; data++) {                  \
            ch = *data;                                \
            MASK |= (1UL << (ch & (BLOOM_WIDTH - 1))); \
        }                                              \
        break;                                         \
    } while (0)

    /* calculate simple bloom-style bitmask for a given unicode string */

    BLOOM_MASK mask;

    mask = 0;
    switch (kind) {
    case PyUnicode_1BYTE_KIND:
        BLOOM_UPDATE(Py_UCS1, mask, ptr, len);
        break;
    case PyUnicode_2BYTE_KIND:
        BLOOM_UPDATE(Py_UCS2, mask, ptr, len);
        break;
    case PyUnicode_4BYTE_KIND:
        BLOOM_UPDATE(Py_UCS4, mask, ptr, len);
        break;
    default:
        Py_UNREACHABLE();
    }
    return mask;

#undef BLOOM_UPDATE
}

static int
ensure_unicode(PyObject *obj)
{
    if (!PyUnicode_Check(obj)) {
        PyErr_Format(PyExc_TypeError,
                     "must be str, not %.100s",
                     Py_TYPE(obj)->tp_name);
        return -1;
    }
    return PyUnicode_READY(obj);
}

/* Compilation of templated routines */

#define STRINGLIB_GET_EMPTY() unicode_get_empty()

#include "stringlib/asciilib.h"
#include "stringlib/fastsearch.h"
#include "stringlib/partition.h"
#include "stringlib/split.h"
#include "stringlib/count.h"
#include "stringlib/find.h"
#include "stringlib/find_max_char.h"
#include "stringlib/undef.h"

#include "stringlib/ucs1lib.h"
#include "stringlib/fastsearch.h"
#include "stringlib/partition.h"
#include "stringlib/split.h"
#include "stringlib/count.h"
#include "stringlib/find.h"
#include "stringlib/replace.h"
#include "stringlib/find_max_char.h"
#include "stringlib/undef.h"

#include "stringlib/ucs2lib.h"
#include "stringlib/fastsearch.h"
#include "stringlib/partition.h"
#include "stringlib/split.h"
#include "stringlib/count.h"
#include "stringlib/find.h"
#include "stringlib/replace.h"
#include "stringlib/find_max_char.h"
#include "stringlib/undef.h"

#include "stringlib/ucs4lib.h"
#include "stringlib/fastsearch.h"
#include "stringlib/partition.h"
#include "stringlib/split.h"
#include "stringlib/count.h"
#include "stringlib/find.h"
#include "stringlib/replace.h"
#include "stringlib/find_max_char.h"
#include "stringlib/undef.h"

_Py_COMP_DIAG_PUSH
_Py_COMP_DIAG_IGNORE_DEPR_DECLS
#include "stringlib/unicodedefs.h"
#include "stringlib/fastsearch.h"
#include "stringlib/count.h"
#include "stringlib/find.h"
#include "stringlib/undef.h"
_Py_COMP_DIAG_POP

#undef STRINGLIB_GET_EMPTY

/* --- Unicode Object ----------------------------------------------------- */

static inline Py_ssize_t
findchar(const void *s, int kind,
         Py_ssize_t size, Py_UCS4 ch,
         int direction)
{
    switch (kind) {
    case PyUnicode_1BYTE_KIND:
        if ((Py_UCS1) ch != ch)
            return -1;
        if (direction > 0)
            return ucs1lib_find_char((const Py_UCS1 *) s, size, (Py_UCS1) ch);
        else
            return ucs1lib_rfind_char((const Py_UCS1 *) s, size, (Py_UCS1) ch);
    case PyUnicode_2BYTE_KIND:
        if ((Py_UCS2) ch != ch)
            return -1;
        if (direction > 0)
            return ucs2lib_find_char((const Py_UCS2 *) s, size, (Py_UCS2) ch);
        else
            return ucs2lib_rfind_char((const Py_UCS2 *) s, size, (Py_UCS2) ch);
    case PyUnicode_4BYTE_KIND:
        if (direction > 0)
            return ucs4lib_find_char((const Py_UCS4 *) s, size, ch);
        else
            return ucs4lib_rfind_char((const Py_UCS4 *) s, size, ch);
    default:
        Py_UNREACHABLE();
    }
}

#ifdef Py_DEBUG
/* Fill the data of a Unicode string with invalid characters to detect bugs
   earlier.

   _PyUnicode_CheckConsistency(str, 1) detects invalid characters, at least for
   ASCII and UCS-4 strings. U+00FF is invalid in ASCII and U+FFFFFFFF is an
   invalid character in Unicode 6.0. */
static void
unicode_fill_invalid(PyObject *unicode, Py_ssize_t old_length)
{
    int kind = PyUnicode_KIND(unicode);
    Py_UCS1 *data = PyUnicode_1BYTE_DATA(unicode);
    Py_ssize_t length = _PyUnicode_LENGTH(unicode);
    if (length <= old_length)
        return;
    memset(data + old_length * kind, 0xff, (length - old_length) * kind);
}
#endif

static PyObject*
resize_compact(PyObject *unicode, Py_ssize_t length)
{
    Py_ssize_t char_size;
    Py_ssize_t struct_size;
    Py_ssize_t new_size;
    int share_wstr;
    PyObject *new_unicode;
#ifdef Py_DEBUG
    Py_ssize_t old_length = _PyUnicode_LENGTH(unicode);
#endif

    assert(unicode_modifiable(unicode));
    assert(PyUnicode_IS_READY(unicode));
    assert(PyUnicode_IS_COMPACT(unicode));

    char_size = PyUnicode_KIND(unicode);
    if (PyUnicode_IS_ASCII(unicode))
        struct_size = sizeof(PyASCIIObject);
    else
        struct_size = sizeof(PyCompactUnicodeObject);
    share_wstr = _PyUnicode_SHARE_WSTR(unicode);

    if (length > ((PY_SSIZE_T_MAX - struct_size) / char_size - 1)) {
        PyErr_NoMemory();
        return NULL;
    }
    new_size = (struct_size + (length + 1) * char_size);

    if (_PyUnicode_HAS_UTF8_MEMORY(unicode)) {
        PyObject_Free(_PyUnicode_UTF8(unicode));
        _PyUnicode_UTF8(unicode) = NULL;
        _PyUnicode_UTF8_LENGTH(unicode) = 0;
    }
#ifdef Py_REF_DEBUG
    _Py_RefTotal--;
#endif
#ifdef Py_TRACE_REFS
    _Py_ForgetReference(unicode);
#endif

    new_unicode = (PyObject *)PyObject_Realloc(unicode, new_size);
    if (new_unicode == NULL) {
        _Py_NewReference(unicode);
        PyErr_NoMemory();
        return NULL;
    }
    unicode = new_unicode;
    _Py_NewReference(unicode);

    _PyUnicode_LENGTH(unicode) = length;
    if (share_wstr) {
        _PyUnicode_WSTR(unicode) = PyUnicode_DATA(unicode);
        if (!PyUnicode_IS_ASCII(unicode))
            _PyUnicode_WSTR_LENGTH(unicode) = length;
    }
    else if (_PyUnicode_HAS_WSTR_MEMORY(unicode)) {
        PyObject_Free(_PyUnicode_WSTR(unicode));
        _PyUnicode_WSTR(unicode) = NULL;
        if (!PyUnicode_IS_ASCII(unicode))
            _PyUnicode_WSTR_LENGTH(unicode) = 0;
    }
#ifdef Py_DEBUG
    unicode_fill_invalid(unicode, old_length);
#endif
    PyUnicode_WRITE(PyUnicode_KIND(unicode), PyUnicode_DATA(unicode),
                    length, 0);
    assert(_PyUnicode_CheckConsistency(unicode, 0));
    return unicode;
}

static int
resize_inplace(PyObject *unicode, Py_ssize_t length)
{
    wchar_t *wstr;
    Py_ssize_t new_size;
    assert(!PyUnicode_IS_COMPACT(unicode));
    assert(Py_REFCNT(unicode) == 1);

    if (PyUnicode_IS_READY(unicode)) {
        Py_ssize_t char_size;
        int share_wstr, share_utf8;
        void *data;
#ifdef Py_DEBUG
        Py_ssize_t old_length = _PyUnicode_LENGTH(unicode);
#endif

        data = _PyUnicode_DATA_ANY(unicode);
        char_size = PyUnicode_KIND(unicode);
        share_wstr = _PyUnicode_SHARE_WSTR(unicode);
        share_utf8 = _PyUnicode_SHARE_UTF8(unicode);

        if (length > (PY_SSIZE_T_MAX / char_size - 1)) {
            PyErr_NoMemory();
            return -1;
        }
        new_size = (length + 1) * char_size;

        if (!share_utf8 && _PyUnicode_HAS_UTF8_MEMORY(unicode))
        {
            PyObject_Free(_PyUnicode_UTF8(unicode));
            _PyUnicode_UTF8(unicode) = NULL;
            _PyUnicode_UTF8_LENGTH(unicode) = 0;
        }

        data = (PyObject *)PyObject_Realloc(data, new_size);
        if (data == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        _PyUnicode_DATA_ANY(unicode) = data;
        if (share_wstr) {
            _PyUnicode_WSTR(unicode) = data;
            _PyUnicode_WSTR_LENGTH(unicode) = length;
        }
        if (share_utf8) {
            _PyUnicode_UTF8(unicode) = data;
            _PyUnicode_UTF8_LENGTH(unicode) = length;
        }
        _PyUnicode_LENGTH(unicode) = length;
        PyUnicode_WRITE(PyUnicode_KIND(unicode), data, length, 0);
#ifdef Py_DEBUG
        unicode_fill_invalid(unicode, old_length);
#endif
        if (share_wstr || _PyUnicode_WSTR(unicode) == NULL) {
            assert(_PyUnicode_CheckConsistency(unicode, 0));
            return 0;
        }
    }
    assert(_PyUnicode_WSTR(unicode) != NULL);

    /* check for integer overflow */
    if (length > PY_SSIZE_T_MAX / (Py_ssize_t)sizeof(wchar_t) - 1) {
        PyErr_NoMemory();
        return -1;
    }
    new_size = sizeof(wchar_t) * (length + 1);
    wstr =  _PyUnicode_WSTR(unicode);
    wstr = PyObject_Realloc(wstr, new_size);
    if (!wstr) {
        PyErr_NoMemory();
        return -1;
    }
    _PyUnicode_WSTR(unicode) = wstr;
    _PyUnicode_WSTR(unicode)[length] = 0;
    _PyUnicode_WSTR_LENGTH(unicode) = length;
    assert(_PyUnicode_CheckConsistency(unicode, 0));
    return 0;
}

static PyObject*
resize_copy(PyObject *unicode, Py_ssize_t length)
{
    Py_ssize_t copy_length;
    if (_PyUnicode_KIND(unicode) != PyUnicode_WCHAR_KIND) {
        PyObject *copy;

        assert(PyUnicode_IS_READY(unicode));

        copy = PyUnicode_New(length, PyUnicode_MAX_CHAR_VALUE(unicode));
        if (copy == NULL)
            return NULL;

        copy_length = Py_MIN(length, PyUnicode_GET_LENGTH(unicode));
        _PyUnicode_FastCopyCharacters(copy, 0, unicode, 0, copy_length);
        return copy;
    }
    else {
        PyObject *w;

        w = (PyObject*)_PyUnicode_New(length);
        if (w == NULL)
            return NULL;
        copy_length = _PyUnicode_WSTR_LENGTH(unicode);
        copy_length = Py_MIN(copy_length, length);
        memcpy(_PyUnicode_WSTR(w), _PyUnicode_WSTR(unicode),
                  copy_length * sizeof(wchar_t));
        return w;
    }
}

/* We allocate one more byte to make sure the string is
   Ux0000 terminated; some code (e.g. new_identifier)
   relies on that.

   XXX This allocator could further be enhanced by assuring that the
   free list never reduces its size below 1.

*/

static PyUnicodeObject *
_PyUnicode_New(Py_ssize_t length)
{
    PyUnicodeObject *unicode;
    size_t new_size;

    /* Optimization for empty strings */
    if (length == 0) {
        return (PyUnicodeObject *)unicode_new_empty();
    }

    /* Ensure we won't overflow the size. */
    if (length > ((PY_SSIZE_T_MAX / (Py_ssize_t)sizeof(Py_UNICODE)) - 1)) {
        return (PyUnicodeObject *)PyErr_NoMemory();
    }
    if (length < 0) {
        PyErr_SetString(PyExc_SystemError,
                        "Negative size passed to _PyUnicode_New");
        return NULL;
    }

    unicode = PyObject_New(PyUnicodeObject, &PyUnicode_Type);
    if (unicode == NULL)
        return NULL;
    new_size = sizeof(Py_UNICODE) * ((size_t)length + 1);

    _PyUnicode_WSTR_LENGTH(unicode) = length;
    _PyUnicode_HASH(unicode) = -1;
    _PyUnicode_STATE(unicode).interned = 0;
    _PyUnicode_STATE(unicode).kind = 0;
    _PyUnicode_STATE(unicode).compact = 0;
    _PyUnicode_STATE(unicode).ready = 0;
    _PyUnicode_STATE(unicode).ascii = 0;
    _PyUnicode_DATA_ANY(unicode) = NULL;
    _PyUnicode_LENGTH(unicode) = 0;
    _PyUnicode_UTF8(unicode) = NULL;
    _PyUnicode_UTF8_LENGTH(unicode) = 0;

    _PyUnicode_WSTR(unicode) = (Py_UNICODE*) PyObject_Malloc(new_size);
    if (!_PyUnicode_WSTR(unicode)) {
        Py_DECREF(unicode);
        PyErr_NoMemory();
        return NULL;
    }

    /* Initialize the first element to guard against cases where
     * the caller fails before initializing str -- unicode_resize()
     * reads str[0], and the Keep-Alive optimization can keep memory
     * allocated for str alive across a call to unicode_dealloc(unicode).
     * We don't want unicode_resize to read uninitialized memory in
     * that case.
     */
    _PyUnicode_WSTR(unicode)[0] = 0;
    _PyUnicode_WSTR(unicode)[length] = 0;

    assert(_PyUnicode_CheckConsistency((PyObject *)unicode, 0));
    return unicode;
}

static const char*
unicode_kind_name(PyObject *unicode)
{
    /* don't check consistency: unicode_kind_name() is called from
       _PyUnicode_Dump() */
    if (!PyUnicode_IS_COMPACT(unicode))
    {
        if (!PyUnicode_IS_READY(unicode))
            return "wstr";
        switch (PyUnicode_KIND(unicode))
        {
        case PyUnicode_1BYTE_KIND:
            if (PyUnicode_IS_ASCII(unicode))
                return "legacy ascii";
            else
                return "legacy latin1";
        case PyUnicode_2BYTE_KIND:
            return "legacy UCS2";
        case PyUnicode_4BYTE_KIND:
            return "legacy UCS4";
        default:
            return "<legacy invalid kind>";
        }
    }
    assert(PyUnicode_IS_READY(unicode));
    switch (PyUnicode_KIND(unicode)) {
    case PyUnicode_1BYTE_KIND:
        if (PyUnicode_IS_ASCII(unicode))
            return "ascii";
        else
            return "latin1";
    case PyUnicode_2BYTE_KIND:
        return "UCS2";
    case PyUnicode_4BYTE_KIND:
        return "UCS4";
    default:
        return "<invalid compact kind>";
    }
}

#ifdef Py_DEBUG
/* Functions wrapping macros for use in debugger */
const char *_PyUnicode_utf8(void *unicode_raw){
    PyObject *unicode = _PyObject_CAST(unicode_raw);
    return PyUnicode_UTF8(unicode);
}

const void *_PyUnicode_compact_data(void *unicode_raw) {
    PyObject *unicode = _PyObject_CAST(unicode_raw);
    return _PyUnicode_COMPACT_DATA(unicode);
}
const void *_PyUnicode_data(void *unicode_raw) {
    PyObject *unicode = _PyObject_CAST(unicode_raw);
    printf("obj %p\n", (void*)unicode);
    printf("compact %d\n", PyUnicode_IS_COMPACT(unicode));
    printf("compact ascii %d\n", PyUnicode_IS_COMPACT_ASCII(unicode));
    printf("ascii op %p\n", ((void*)((PyASCIIObject*)(unicode) + 1)));
    printf("compact op %p\n", ((void*)((PyCompactUnicodeObject*)(unicode) + 1)));
    printf("compact data %p\n", _PyUnicode_COMPACT_DATA(unicode));
    return PyUnicode_DATA(unicode);
}

void
_PyUnicode_Dump(PyObject *op)
{
    PyASCIIObject *ascii = (PyASCIIObject *)op;
    PyCompactUnicodeObject *compact = (PyCompactUnicodeObject *)op;
    PyUnicodeObject *unicode = (PyUnicodeObject *)op;
    const void *data;

    if (ascii->state.compact)
    {
        if (ascii->state.ascii)
            data = (ascii + 1);
        else
            data = (compact + 1);
    }
    else
        data = unicode->data.any;
    printf("%s: len=%zu, ", unicode_kind_name(op), ascii->length);

    if (ascii->wstr == data)
        printf("shared ");
    printf("wstr=%p", (void *)ascii->wstr);

    if (!(ascii->state.ascii == 1 && ascii->state.compact == 1)) {
        printf(" (%zu), ", compact->wstr_length);
        if (!ascii->state.compact && compact->utf8 == unicode->data.any) {
            printf("shared ");
        }
        printf("utf8=%p (%zu)", (void *)compact->utf8, compact->utf8_length);
    }
    printf(", data=%p\n", data);
}
#endif

static int
unicode_create_empty_string_singleton(struct _Py_unicode_state *state)
{
    // Use size=1 rather than size=0, so PyUnicode_New(0, maxchar) can be
    // optimized to always use state->empty_string without having to check if
    // it is NULL or not.
    PyObject *empty = PyUnicode_New(1, 0);
    if (empty == NULL) {
        return -1;
    }
    PyUnicode_1BYTE_DATA(empty)[0] = 0;
    _PyUnicode_LENGTH(empty) = 0;
    assert(_PyUnicode_CheckConsistency(empty, 1));

    assert(state->empty_string == NULL);
    state->empty_string = empty;
    return 0;
}


PyObject *
PyUnicode_New(Py_ssize_t size, Py_UCS4 maxchar)
{
    /* Optimization for empty strings */
    if (size == 0) {
        return unicode_new_empty();
    }

    PyObject *obj;
    PyCompactUnicodeObject *unicode;
    void *data;
    enum PyUnicode_Kind kind;
    int is_sharing, is_ascii;
    Py_ssize_t char_size;
    Py_ssize_t struct_size;

    is_ascii = 0;
    is_sharing = 0;
    struct_size = sizeof(PyCompactUnicodeObject);
    if (maxchar < 128) {
        kind = PyUnicode_1BYTE_KIND;
        char_size = 1;
        is_ascii = 1;
        struct_size = sizeof(PyASCIIObject);
    }
    else if (maxchar < 256) {
        kind = PyUnicode_1BYTE_KIND;
        char_size = 1;
    }
    else if (maxchar < 65536) {
        kind = PyUnicode_2BYTE_KIND;
        char_size = 2;
        if (sizeof(wchar_t) == 2)
            is_sharing = 1;
    }
    else {
        if (maxchar > MAX_UNICODE) {
            PyErr_SetString(PyExc_SystemError,
                            "invalid maximum character passed to PyUnicode_New");
            return NULL;
        }
        kind = PyUnicode_4BYTE_KIND;
        char_size = 4;
        if (sizeof(wchar_t) == 4)
            is_sharing = 1;
    }

    /* Ensure we won't overflow the size. */
    if (size < 0) {
        PyErr_SetString(PyExc_SystemError,
                        "Negative size passed to PyUnicode_New");
        return NULL;
    }
    if (size > ((PY_SSIZE_T_MAX - struct_size) / char_size - 1))
        return PyErr_NoMemory();

    /* Duplicated allocation code from _PyObject_New() instead of a call to
     * PyObject_New() so we are able to allocate space for the object and
     * it's data buffer.
     */
    obj = (PyObject *) PyObject_Malloc(struct_size + (size + 1) * char_size);
    if (obj == NULL) {
        return PyErr_NoMemory();
    }
    _PyObject_Init(obj, &PyUnicode_Type);

    unicode = (PyCompactUnicodeObject *)obj;
    if (is_ascii)
        data = ((PyASCIIObject*)obj) + 1;
    else
        data = unicode + 1;
    _PyUnicode_LENGTH(unicode) = size;
    _PyUnicode_HASH(unicode) = -1;
    _PyUnicode_STATE(unicode).interned = 0;
    _PyUnicode_STATE(unicode).kind = kind;
    _PyUnicode_STATE(unicode).compact = 1;
    _PyUnicode_STATE(unicode).ready = 1;
    _PyUnicode_STATE(unicode).ascii = is_ascii;
    if (is_ascii) {
        ((char*)data)[size] = 0;
        _PyUnicode_WSTR(unicode) = NULL;
    }
    else if (kind == PyUnicode_1BYTE_KIND) {
        ((char*)data)[size] = 0;
        _PyUnicode_WSTR(unicode) = NULL;
        _PyUnicode_WSTR_LENGTH(unicode) = 0;
        unicode->utf8 = NULL;
        unicode->utf8_length = 0;
    }
    else {
        unicode->utf8 = NULL;
        unicode->utf8_length = 0;
        if (kind == PyUnicode_2BYTE_KIND)
            ((Py_UCS2*)data)[size] = 0;
        else /* kind == PyUnicode_4BYTE_KIND */
            ((Py_UCS4*)data)[size] = 0;
        if (is_sharing) {
            _PyUnicode_WSTR_LENGTH(unicode) = size;
            _PyUnicode_WSTR(unicode) = (wchar_t *)data;
        }
        else {
            _PyUnicode_WSTR_LENGTH(unicode) = 0;
            _PyUnicode_WSTR(unicode) = NULL;
        }
    }
#ifdef Py_DEBUG
    unicode_fill_invalid((PyObject*)unicode, 0);
#endif
    assert(_PyUnicode_CheckConsistency((PyObject*)unicode, 0));
    return obj;
}

#if SIZEOF_WCHAR_T == 2
/* Helper function to convert a 16-bits wchar_t representation to UCS4, this
   will decode surrogate pairs, the other conversions are implemented as macros
   for efficiency.

   This function assumes that unicode can hold one more code point than wstr
   characters for a terminating null character. */
static void
unicode_convert_wchar_to_ucs4(const wchar_t *begin, const wchar_t *end,
                              PyObject *unicode)
{
    const wchar_t *iter;
    Py_UCS4 *ucs4_out;

    assert(unicode != NULL);
    assert(_PyUnicode_CHECK(unicode));
    assert(_PyUnicode_KIND(unicode) == PyUnicode_4BYTE_KIND);
    ucs4_out = PyUnicode_4BYTE_DATA(unicode);

    for (iter = begin; iter < end; ) {
        assert(ucs4_out < (PyUnicode_4BYTE_DATA(unicode) +
                           _PyUnicode_GET_LENGTH(unicode)));
        if (Py_UNICODE_IS_HIGH_SURROGATE(iter[0])
            && (iter+1) < end
            && Py_UNICODE_IS_LOW_SURROGATE(iter[1]))
        {
            *ucs4_out++ = Py_UNICODE_JOIN_SURROGATES(iter[0], iter[1]);
            iter += 2;
        }
        else {
            *ucs4_out++ = *iter;
            iter++;
        }
    }
    assert(ucs4_out == (PyUnicode_4BYTE_DATA(unicode) +
                        _PyUnicode_GET_LENGTH(unicode)));

}
#endif

static int
unicode_check_modifiable(PyObject *unicode)
{
    if (!unicode_modifiable(unicode)) {
        PyErr_SetString(PyExc_SystemError,
                        "Cannot modify a string currently used");
        return -1;
    }
    return 0;
}

static int
_copy_characters(PyObject *to, Py_ssize_t to_start,
                 PyObject *from, Py_ssize_t from_start,
                 Py_ssize_t how_many, int check_maxchar)
{
    unsigned int from_kind, to_kind;
    const void *from_data;
    void *to_data;

    assert(0 <= how_many);
    assert(0 <= from_start);
    assert(0 <= to_start);
    assert(PyUnicode_Check(from));
    assert(PyUnicode_IS_READY(from));
    assert(from_start + how_many <= PyUnicode_GET_LENGTH(from));

    assert(PyUnicode_Check(to));
    assert(PyUnicode_IS_READY(to));
    assert(to_start + how_many <= PyUnicode_GET_LENGTH(to));

    if (how_many == 0)
        return 0;

    from_kind = PyUnicode_KIND(from);
    from_data = PyUnicode_DATA(from);
    to_kind = PyUnicode_KIND(to);
    to_data = PyUnicode_DATA(to);

#ifdef Py_DEBUG
    if (!check_maxchar
        && PyUnicode_MAX_CHAR_VALUE(from) > PyUnicode_MAX_CHAR_VALUE(to))
    {
        Py_UCS4 to_maxchar = PyUnicode_MAX_CHAR_VALUE(to);
        Py_UCS4 ch;
        Py_ssize_t i;
        for (i=0; i < how_many; i++) {
            ch = PyUnicode_READ(from_kind, from_data, from_start + i);
            assert(ch <= to_maxchar);
        }
    }
#endif

    if (from_kind == to_kind) {
        if (check_maxchar
            && !PyUnicode_IS_ASCII(from) && PyUnicode_IS_ASCII(to))
        {
            /* Writing Latin-1 characters into an ASCII string requires to
               check that all written characters are pure ASCII */
            Py_UCS4 max_char;
            max_char = ucs1lib_find_max_char(from_data,
                                             (const Py_UCS1*)from_data + how_many);
            if (max_char >= 128)
                return -1;
        }
        memcpy((char*)to_data + to_kind * to_start,
                  (const char*)from_data + from_kind * from_start,
                  to_kind * how_many);
    }
    else if (from_kind == PyUnicode_1BYTE_KIND
             && to_kind == PyUnicode_2BYTE_KIND)
    {
        _PyUnicode_CONVERT_BYTES(
            Py_UCS1, Py_UCS2,
            PyUnicode_1BYTE_DATA(from) + from_start,
            PyUnicode_1BYTE_DATA(from) + from_start + how_many,
            PyUnicode_2BYTE_DATA(to) + to_start
            );
    }
    else if (from_kind == PyUnicode_1BYTE_KIND
             && to_kind == PyUnicode_4BYTE_KIND)
    {
        _PyUnicode_CONVERT_BYTES(
            Py_UCS1, Py_UCS4,
            PyUnicode_1BYTE_DATA(from) + from_start,
            PyUnicode_1BYTE_DATA(from) + from_start + how_many,
            PyUnicode_4BYTE_DATA(to) + to_start
            );
    }
    else if (from_kind == PyUnicode_2BYTE_KIND
             && to_kind == PyUnicode_4BYTE_KIND)
    {
        _PyUnicode_CONVERT_BYTES(
            Py_UCS2, Py_UCS4,
            PyUnicode_2BYTE_DATA(from) + from_start,
            PyUnicode_2BYTE_DATA(from) + from_start + how_many,
            PyUnicode_4BYTE_DATA(to) + to_start
            );
    }
    else {
        assert (PyUnicode_MAX_CHAR_VALUE(from) > PyUnicode_MAX_CHAR_VALUE(to));

        if (!check_maxchar) {
            if (from_kind == PyUnicode_2BYTE_KIND
                && to_kind == PyUnicode_1BYTE_KIND)
            {
                _PyUnicode_CONVERT_BYTES(
                    Py_UCS2, Py_UCS1,
                    PyUnicode_2BYTE_DATA(from) + from_start,
                    PyUnicode_2BYTE_DATA(from) + from_start + how_many,
                    PyUnicode_1BYTE_DATA(to) + to_start
                    );
            }
            else if (from_kind == PyUnicode_4BYTE_KIND
                     && to_kind == PyUnicode_1BYTE_KIND)
            {
                _PyUnicode_CONVERT_BYTES(
                    Py_UCS4, Py_UCS1,
                    PyUnicode_4BYTE_DATA(from) + from_start,
                    PyUnicode_4BYTE_DATA(from) + from_start + how_many,
                    PyUnicode_1BYTE_DATA(to) + to_start
                    );
            }
            else if (from_kind == PyUnicode_4BYTE_KIND
                     && to_kind == PyUnicode_2BYTE_KIND)
            {
                _PyUnicode_CONVERT_BYTES(
                    Py_UCS4, Py_UCS2,
                    PyUnicode_4BYTE_DATA(from) + from_start,
                    PyUnicode_4BYTE_DATA(from) + from_start + how_many,
                    PyUnicode_2BYTE_DATA(to) + to_start
                    );
            }
            else {
                Py_UNREACHABLE();
            }
        }
        else {
            const Py_UCS4 to_maxchar = PyUnicode_MAX_CHAR_VALUE(to);
            Py_UCS4 ch;
            Py_ssize_t i;

            for (i=0; i < how_many; i++) {
                ch = PyUnicode_READ(from_kind, from_data, from_start + i);
                if (ch > to_maxchar)
                    return -1;
                PyUnicode_WRITE(to_kind, to_data, to_start + i, ch);
            }
        }
    }
    return 0;
}

void
_PyUnicode_FastCopyCharacters(
    PyObject *to, Py_ssize_t to_start,
    PyObject *from, Py_ssize_t from_start, Py_ssize_t how_many)
{
    (void)_copy_characters(to, to_start, from, from_start, how_many, 0);
}

Py_ssize_t
PyUnicode_CopyCharacters(PyObject *to, Py_ssize_t to_start,
                         PyObject *from, Py_ssize_t from_start,
                         Py_ssize_t how_many)
{
    int err;

    if (!PyUnicode_Check(from) || !PyUnicode_Check(to)) {
        PyErr_BadInternalCall();
        return -1;
    }

    if (PyUnicode_READY(from) == -1)
        return -1;
    if (PyUnicode_READY(to) == -1)
        return -1;

    if ((size_t)from_start > (size_t)PyUnicode_GET_LENGTH(from)) {
        PyErr_SetString(PyExc_IndexError, "string index out of range");
        return -1;
    }
    if ((size_t)to_start > (size_t)PyUnicode_GET_LENGTH(to)) {
        PyErr_SetString(PyExc_IndexError, "string index out of range");
        return -1;
    }
    if (how_many < 0) {
        PyErr_SetString(PyExc_SystemError, "how_many cannot be negative");
        return -1;
    }
    how_many = Py_MIN(PyUnicode_GET_LENGTH(from)-from_start, how_many);
    if (to_start + how_many > PyUnicode_GET_LENGTH(to)) {
        PyErr_Format(PyExc_SystemError,
                     "Cannot write %zi characters at %zi "
                     "in a string of %zi characters",
                     how_many, to_start, PyUnicode_GET_LENGTH(to));
        return -1;
    }

    if (how_many == 0)
        return 0;

    if (unicode_check_modifiable(to))
        return -1;

    err = _copy_characters(to, to_start, from, from_start, how_many, 1);
    if (err) {
        PyErr_Format(PyExc_SystemError,
                     "Cannot copy %s characters "
                     "into a string of %s characters",
                     unicode_kind_name(from),
                     unicode_kind_name(to));
        return -1;
    }
    return how_many;
}

/* Find the maximum code point and count the number of surrogate pairs so a
   correct string length can be computed before converting a string to UCS4.
   This function counts single surrogates as a character and not as a pair.

   Return 0 on success, or -1 on error. */
static int
find_maxchar_surrogates(const wchar_t *begin, const wchar_t *end,
                        Py_UCS4 *maxchar, Py_ssize_t *num_surrogates)
{
    const wchar_t *iter;
    Py_UCS4 ch;

    assert(num_surrogates != NULL && maxchar != NULL);
    *num_surrogates = 0;
    *maxchar = 0;

    for (iter = begin; iter < end; ) {
#if SIZEOF_WCHAR_T == 2
        if (Py_UNICODE_IS_HIGH_SURROGATE(iter[0])
            && (iter+1) < end
            && Py_UNICODE_IS_LOW_SURROGATE(iter[1]))
        {
            ch = Py_UNICODE_JOIN_SURROGATES(iter[0], iter[1]);
            ++(*num_surrogates);
            iter += 2;
        }
        else
#endif
        {
            ch = *iter;
            iter++;
        }
        if (ch > *maxchar) {
            *maxchar = ch;
            if (*maxchar > MAX_UNICODE) {
                PyErr_Format(PyExc_ValueError,
                             "character U+%x is not in range [U+0000; U+%x]",
                             ch, MAX_UNICODE);
                return -1;
            }
        }
    }
    return 0;
}

int
_PyUnicode_Ready(PyObject *unicode)
{
    wchar_t *end;
    Py_UCS4 maxchar = 0;
    Py_ssize_t num_surrogates;
#if SIZEOF_WCHAR_T == 2
    Py_ssize_t length_wo_surrogates;
#endif

    /* _PyUnicode_Ready() is only intended for old-style API usage where
       strings were created using _PyObject_New() and where no canonical
       representation (the str field) has been set yet aka strings
       which are not yet ready. */
    assert(_PyUnicode_CHECK(unicode));
    assert(_PyUnicode_KIND(unicode) == PyUnicode_WCHAR_KIND);
    assert(_PyUnicode_WSTR(unicode) != NULL);
    assert(_PyUnicode_DATA_ANY(unicode) == NULL);
    assert(_PyUnicode_UTF8(unicode) == NULL);
    /* Actually, it should neither be interned nor be anything else: */
    assert(_PyUnicode_STATE(unicode).interned == SSTATE_NOT_INTERNED);

    end = _PyUnicode_WSTR(unicode) + _PyUnicode_WSTR_LENGTH(unicode);
    if (find_maxchar_surrogates(_PyUnicode_WSTR(unicode), end,
                                &maxchar, &num_surrogates) == -1)
        return -1;

    if (maxchar < 256) {
        _PyUnicode_DATA_ANY(unicode) = PyObject_Malloc(_PyUnicode_WSTR_LENGTH(unicode) + 1);
        if (!_PyUnicode_DATA_ANY(unicode)) {
            PyErr_NoMemory();
            return -1;
        }
        _PyUnicode_CONVERT_BYTES(wchar_t, unsigned char,
                                _PyUnicode_WSTR(unicode), end,
                                PyUnicode_1BYTE_DATA(unicode));
        PyUnicode_1BYTE_DATA(unicode)[_PyUnicode_WSTR_LENGTH(unicode)] = '\0';
        _PyUnicode_LENGTH(unicode) = _PyUnicode_WSTR_LENGTH(unicode);
        _PyUnicode_STATE(unicode).kind = PyUnicode_1BYTE_KIND;
        if (maxchar < 128) {
            _PyUnicode_STATE(unicode).ascii = 1;
            _PyUnicode_UTF8(unicode) = _PyUnicode_DATA_ANY(unicode);
            _PyUnicode_UTF8_LENGTH(unicode) = _PyUnicode_WSTR_LENGTH(unicode);
        }
        else {
            _PyUnicode_STATE(unicode).ascii = 0;
            _PyUnicode_UTF8(unicode) = NULL;
            _PyUnicode_UTF8_LENGTH(unicode) = 0;
        }
        PyObject_Free(_PyUnicode_WSTR(unicode));
        _PyUnicode_WSTR(unicode) = NULL;
        _PyUnicode_WSTR_LENGTH(unicode) = 0;
    }
    /* In this case we might have to convert down from 4-byte native
       wchar_t to 2-byte unicode. */
    else if (maxchar < 65536) {
        assert(num_surrogates == 0 &&
               "FindMaxCharAndNumSurrogatePairs() messed up");

#if SIZEOF_WCHAR_T == 2
        /* We can share representations and are done. */
        _PyUnicode_DATA_ANY(unicode) = _PyUnicode_WSTR(unicode);
        PyUnicode_2BYTE_DATA(unicode)[_PyUnicode_WSTR_LENGTH(unicode)] = '\0';
        _PyUnicode_LENGTH(unicode) = _PyUnicode_WSTR_LENGTH(unicode);
        _PyUnicode_STATE(unicode).kind = PyUnicode_2BYTE_KIND;
        _PyUnicode_UTF8(unicode) = NULL;
        _PyUnicode_UTF8_LENGTH(unicode) = 0;
#else
        /* sizeof(wchar_t) == 4 */
        _PyUnicode_DATA_ANY(unicode) = PyObject_Malloc(
            2 * (_PyUnicode_WSTR_LENGTH(unicode) + 1));
        if (!_PyUnicode_DATA_ANY(unicode)) {
            PyErr_NoMemory();
            return -1;
        }
        _PyUnicode_CONVERT_BYTES(wchar_t, Py_UCS2,
                                _PyUnicode_WSTR(unicode), end,
                                PyUnicode_2BYTE_DATA(unicode));
        PyUnicode_2BYTE_DATA(unicode)[_PyUnicode_WSTR_LENGTH(unicode)] = '\0';
        _PyUnicode_LENGTH(unicode) = _PyUnicode_WSTR_LENGTH(unicode);
        _PyUnicode_STATE(unicode).kind = PyUnicode_2BYTE_KIND;
        _PyUnicode_UTF8(unicode) = NULL;
        _PyUnicode_UTF8_LENGTH(unicode) = 0;
        PyObject_Free(_PyUnicode_WSTR(unicode));
        _PyUnicode_WSTR(unicode) = NULL;
        _PyUnicode_WSTR_LENGTH(unicode) = 0;
#endif
    }
    /* maxchar exceeds 16 bit, wee need 4 bytes for unicode characters */
    else {
#if SIZEOF_WCHAR_T == 2
        /* in case the native representation is 2-bytes, we need to allocate a
           new normalized 4-byte version. */
        length_wo_surrogates = _PyUnicode_WSTR_LENGTH(unicode) - num_surrogates;
        if (length_wo_surrogates > PY_SSIZE_T_MAX / 4 - 1) {
            PyErr_NoMemory();
            return -1;
        }
        _PyUnicode_DATA_ANY(unicode) = PyObject_Malloc(4 * (length_wo_surrogates + 1));
        if (!_PyUnicode_DATA_ANY(unicode)) {
            PyErr_NoMemory();
            return -1;
        }
        _PyUnicode_LENGTH(unicode) = length_wo_surrogates;
        _PyUnicode_STATE(unicode).kind = PyUnicode_4BYTE_KIND;
        _PyUnicode_UTF8(unicode) = NULL;
        _PyUnicode_UTF8_LENGTH(unicode) = 0;
        /* unicode_convert_wchar_to_ucs4() requires a ready string */
        _PyUnicode_STATE(unicode).ready = 1;
        unicode_convert_wchar_to_ucs4(_PyUnicode_WSTR(unicode), end, unicode);
        PyObject_Free(_PyUnicode_WSTR(unicode));
        _PyUnicode_WSTR(unicode) = NULL;
        _PyUnicode_WSTR_LENGTH(unicode) = 0;
#else
        assert(num_surrogates == 0);

        _PyUnicode_DATA_ANY(unicode) = _PyUnicode_WSTR(unicode);
        _PyUnicode_LENGTH(unicode) = _PyUnicode_WSTR_LENGTH(unicode);
        _PyUnicode_UTF8(unicode) = NULL;
        _PyUnicode_UTF8_LENGTH(unicode) = 0;
        _PyUnicode_STATE(unicode).kind = PyUnicode_4BYTE_KIND;
#endif
        PyUnicode_4BYTE_DATA(unicode)[_PyUnicode_LENGTH(unicode)] = '\0';
    }
    _PyUnicode_STATE(unicode).ready = 1;
    assert(_PyUnicode_CheckConsistency(unicode, 1));
    return 0;
}

static void
unicode_dealloc(PyObject *unicode)
{
    switch (PyUnicode_CHECK_INTERNED(unicode)) {
    case SSTATE_NOT_INTERNED:
        break;

    case SSTATE_INTERNED_MORTAL:
    {
#ifdef INTERNED_STRINGS
        /* Revive the dead object temporarily. PyDict_DelItem() removes two
           references (key and value) which were ignored by
           PyUnicode_InternInPlace(). Use refcnt=3 rather than refcnt=2
           to prevent calling unicode_dealloc() again. Adjust refcnt after
           PyDict_DelItem(). */
        assert(Py_REFCNT(unicode) == 0);
        Py_SET_REFCNT(unicode, 3);
        if (PyDict_DelItem(interned, unicode) != 0) {
            _PyErr_WriteUnraisableMsg("deletion of interned string failed",
                                      NULL);
        }
        assert(Py_REFCNT(unicode) == 1);
        Py_SET_REFCNT(unicode, 0);
#endif
        break;
    }

    case SSTATE_INTERNED_IMMORTAL:
        _PyObject_ASSERT_FAILED_MSG(unicode, "Immortal interned string died");
        break;

    default:
        Py_UNREACHABLE();
    }

    if (_PyUnicode_HAS_WSTR_MEMORY(unicode)) {
        PyObject_Free(_PyUnicode_WSTR(unicode));
    }
    if (_PyUnicode_HAS_UTF8_MEMORY(unicode)) {
        PyObject_Free(_PyUnicode_UTF8(unicode));
    }
    if (!PyUnicode_IS_COMPACT(unicode) && _PyUnicode_DATA_ANY(unicode)) {
        PyObject_Free(_PyUnicode_DATA_ANY(unicode));
    }

    Py_TYPE(unicode)->tp_free(unicode);
}

#ifdef Py_DEBUG
static int
unicode_is_singleton(PyObject *unicode)
{
    struct _Py_unicode_state *state = get_unicode_state();
    if (unicode == state->empty_string) {
        return 1;
    }
    PyASCIIObject *ascii = (PyASCIIObject *)unicode;
    if (ascii->state.kind != PyUnicode_WCHAR_KIND && ascii->length == 1)
    {
        Py_UCS4 ch = PyUnicode_READ_CHAR(unicode, 0);
        if (ch < 256 && state->latin1[ch] == unicode) {
            return 1;
        }
    }
    return 0;
}
#endif

static int
unicode_modifiable(PyObject *unicode)
{
    assert(_PyUnicode_CHECK(unicode));
    if (Py_REFCNT(unicode) != 1)
        return 0;
    if (_PyUnicode_HASH(unicode) != -1)
        return 0;
    if (PyUnicode_CHECK_INTERNED(unicode))
        return 0;
    if (!PyUnicode_CheckExact(unicode))
        return 0;
#ifdef Py_DEBUG
    /* singleton refcount is greater than 1 */
    assert(!unicode_is_singleton(unicode));
#endif
    return 1;
}

static int
unicode_resize(PyObject **p_unicode, Py_ssize_t length)
{
    PyObject *unicode;
    Py_ssize_t old_length;

    assert(p_unicode != NULL);
    unicode = *p_unicode;

    assert(unicode != NULL);
    assert(PyUnicode_Check(unicode));
    assert(0 <= length);

    if (_PyUnicode_KIND(unicode) == PyUnicode_WCHAR_KIND)
        old_length = PyUnicode_WSTR_LENGTH(unicode);
    else
        old_length = PyUnicode_GET_LENGTH(unicode);
    if (old_length == length)
        return 0;

    if (length == 0) {
        PyObject *empty = unicode_new_empty();
        Py_SETREF(*p_unicode, empty);
        return 0;
    }

    if (!unicode_modifiable(unicode)) {
        PyObject *copy = resize_copy(unicode, length);
        if (copy == NULL)
            return -1;
        Py_SETREF(*p_unicode, copy);
        return 0;
    }

    if (PyUnicode_IS_COMPACT(unicode)) {
        PyObject *new_unicode = resize_compact(unicode, length);
        if (new_unicode == NULL)
            return -1;
        *p_unicode = new_unicode;
        return 0;
    }
    return resize_inplace(unicode, length);
}

int
PyUnicode_Resize(PyObject **p_unicode, Py_ssize_t length)
{
    PyObject *unicode;
    if (p_unicode == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }
    unicode = *p_unicode;
    if (unicode == NULL || !PyUnicode_Check(unicode) || length < 0)
    {
        PyErr_BadInternalCall();
        return -1;
    }
    return unicode_resize(p_unicode, length);
}

/* Copy an ASCII or latin1 char* string into a Python Unicode string.

   WARNING: The function doesn't copy the terminating null character and
   doesn't check the maximum character (may write a latin1 character in an
   ASCII string). */
static void
unicode_write_cstr(PyObject *unicode, Py_ssize_t index,
                   const char *str, Py_ssize_t len)
{
    enum PyUnicode_Kind kind = PyUnicode_KIND(unicode);
    const void *data = PyUnicode_DATA(unicode);
    const char *end = str + len;

    assert(index + len <= PyUnicode_GET_LENGTH(unicode));
    switch (kind) {
    case PyUnicode_1BYTE_KIND: {
#ifdef Py_DEBUG
        if (PyUnicode_IS_ASCII(unicode)) {
            Py_UCS4 maxchar = ucs1lib_find_max_char(
                (const Py_UCS1*)str,
                (const Py_UCS1*)str + len);
            assert(maxchar < 128);
        }
#endif
        memcpy((char *) data + index, str, len);
        break;
    }
    case PyUnicode_2BYTE_KIND: {
        Py_UCS2 *start = (Py_UCS2 *)data + index;
        Py_UCS2 *ucs2 = start;

        for (; str < end; ++ucs2, ++str)
            *ucs2 = (Py_UCS2)*str;

        assert((ucs2 - start) <= PyUnicode_GET_LENGTH(unicode));
        break;
    }
    case PyUnicode_4BYTE_KIND: {
        Py_UCS4 *start = (Py_UCS4 *)data + index;
        Py_UCS4 *ucs4 = start;

        for (; str < end; ++ucs4, ++str)
            *ucs4 = (Py_UCS4)*str;

        assert((ucs4 - start) <= PyUnicode_GET_LENGTH(unicode));
        break;
    }
    default:
        Py_UNREACHABLE();
    }
}

static PyObject*
get_latin1_char(Py_UCS1 ch)
{
    struct _Py_unicode_state *state = get_unicode_state();

    PyObject *unicode = state->latin1[ch];
    if (unicode) {
        Py_INCREF(unicode);
        return unicode;
    }

    unicode = PyUnicode_New(1, ch);
    if (!unicode) {
        return NULL;
    }

    PyUnicode_1BYTE_DATA(unicode)[0] = ch;
    assert(_PyUnicode_CheckConsistency(unicode, 1));

    Py_INCREF(unicode);
    state->latin1[ch] = unicode;
    return unicode;
}

static PyObject*
unicode_char(Py_UCS4 ch)
{
    PyObject *unicode;

    assert(ch <= MAX_UNICODE);

    if (ch < 256) {
        return get_latin1_char(ch);
    }

    unicode = PyUnicode_New(1, ch);
    if (unicode == NULL)
        return NULL;

    assert(PyUnicode_KIND(unicode) != PyUnicode_1BYTE_KIND);
    if (PyUnicode_KIND(unicode) == PyUnicode_2BYTE_KIND) {
        PyUnicode_2BYTE_DATA(unicode)[0] = (Py_UCS2)ch;
    } else {
        assert(PyUnicode_KIND(unicode) == PyUnicode_4BYTE_KIND);
        PyUnicode_4BYTE_DATA(unicode)[0] = ch;
    }
    assert(_PyUnicode_CheckConsistency(unicode, 1));
    return unicode;
}

PyObject *
PyUnicode_FromUnicode(const Py_UNICODE *u, Py_ssize_t size)
{
    if (u == NULL) {
        if (size > 0) {
            if (PyErr_WarnEx(PyExc_DeprecationWarning,
                    "PyUnicode_FromUnicode(NULL, size) is deprecated; "
                    "use PyUnicode_New() instead", 1) < 0) {
                return NULL;
            }
        }
        return (PyObject*)_PyUnicode_New(size);
    }

    if (size < 0) {
        PyErr_BadInternalCall();
        return NULL;
    }

    return PyUnicode_FromWideChar(u, size);
}

PyObject *
PyUnicode_FromWideChar(const wchar_t *u, Py_ssize_t size)
{
    PyObject *unicode;
    Py_UCS4 maxchar = 0;
    Py_ssize_t num_surrogates;

    if (u == NULL && size != 0) {
        PyErr_BadInternalCall();
        return NULL;
    }

    if (size == -1) {
        size = wcslen(u);
    }

    /* If the Unicode data is known at construction time, we can apply
       some optimizations which share commonly used objects. */

    /* Optimization for empty strings */
    if (size == 0)
        _Py_RETURN_UNICODE_EMPTY();

#ifdef HAVE_NON_UNICODE_WCHAR_T_REPRESENTATION
    /* Oracle Solaris uses non-Unicode internal wchar_t form for
       non-Unicode locales and hence needs conversion to UCS-4 first. */
    if (_Py_LocaleUsesNonUnicodeWchar()) {
        wchar_t* converted = _Py_DecodeNonUnicodeWchar(u, size);
        if (!converted) {
            return NULL;
        }
        PyObject *unicode = _PyUnicode_FromUCS4(converted, size);
        PyMem_Free(converted);
        return unicode;
    }
#endif

    /* Single character Unicode objects in the Latin-1 range are
       shared when using this constructor */
    if (size == 1 && (Py_UCS4)*u < 256)
        return get_latin1_char((unsigned char)*u);

    /* If not empty and not single character, copy the Unicode data
       into the new object */
    if (find_maxchar_surrogates(u, u + size,
                                &maxchar, &num_surrogates) == -1)
        return NULL;

    unicode = PyUnicode_New(size - num_surrogates, maxchar);
    if (!unicode)
        return NULL;

    switch (PyUnicode_KIND(unicode)) {
    case PyUnicode_1BYTE_KIND:
        _PyUnicode_CONVERT_BYTES(Py_UNICODE, unsigned char,
                                u, u + size, PyUnicode_1BYTE_DATA(unicode));
        break;
    case PyUnicode_2BYTE_KIND:
#if Py_UNICODE_SIZE == 2
        memcpy(PyUnicode_2BYTE_DATA(unicode), u, size * 2);
#else
        _PyUnicode_CONVERT_BYTES(Py_UNICODE, Py_UCS2,
                                u, u + size, PyUnicode_2BYTE_DATA(unicode));
#endif
        break;
    case PyUnicode_4BYTE_KIND:
#if SIZEOF_WCHAR_T == 2
        /* This is the only case which has to process surrogates, thus
           a simple copy loop is not enough and we need a function. */
        unicode_convert_wchar_to_ucs4(u, u + size, unicode);
#else
        assert(num_surrogates == 0);
        memcpy(PyUnicode_4BYTE_DATA(unicode), u, size * 4);
#endif
        break;
    default:
        Py_UNREACHABLE();
    }

    return unicode_result(unicode);
}

PyObject *
PyUnicode_FromStringAndSize(const char *u, Py_ssize_t size)
{
    if (size < 0) {
        PyErr_SetString(PyExc_SystemError,
                        "Negative size passed to PyUnicode_FromStringAndSize");
        return NULL;
    }
    if (u != NULL) {
        return PyUnicode_DecodeUTF8Stateful(u, size, NULL, NULL);
    }
    else {
        if (size > 0) {
            if (PyErr_WarnEx(PyExc_DeprecationWarning,
                    "PyUnicode_FromStringAndSize(NULL, size) is deprecated; "
                    "use PyUnicode_New() instead", 1) < 0) {
                return NULL;
            }
        }
        return (PyObject *)_PyUnicode_New(size);
    }
}

PyObject *
PyUnicode_FromString(const char *u)
{
    size_t size = strlen(u);
    if (size > PY_SSIZE_T_MAX) {
        PyErr_SetString(PyExc_OverflowError, "input too long");
        return NULL;
    }
    return PyUnicode_DecodeUTF8Stateful(u, (Py_ssize_t)size, NULL, NULL);
}


PyObject *
_PyUnicode_FromId(_Py_Identifier *id)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    struct _Py_unicode_ids *ids = &interp->unicode.ids;

    Py_ssize_t index = _Py_atomic_size_get(&id->index);
    if (index < 0) {
        struct _Py_unicode_runtime_ids *rt_ids = &interp->runtime->unicode_ids;

        PyThread_acquire_lock(rt_ids->lock, WAIT_LOCK);
        // Check again to detect concurrent access. Another thread can have
        // initialized the index while this thread waited for the lock.
        index = _Py_atomic_size_get(&id->index);
        if (index < 0) {
            assert(rt_ids->next_index < PY_SSIZE_T_MAX);
            index = rt_ids->next_index;
            rt_ids->next_index++;
            _Py_atomic_size_set(&id->index, index);
        }
        PyThread_release_lock(rt_ids->lock);
    }
    assert(index >= 0);

    PyObject *obj;
    if (index < ids->size) {
        obj = ids->array[index];
        if (obj) {
            // Return a borrowed reference
            return obj;
        }
    }

    obj = PyUnicode_DecodeUTF8Stateful(id->string, strlen(id->string),
                                       NULL, NULL);
    if (!obj) {
        return NULL;
    }
    PyUnicode_InternInPlace(&obj);

    if (index >= ids->size) {
        // Overallocate to reduce the number of realloc
        Py_ssize_t new_size = Py_MAX(index * 2, 16);
        Py_ssize_t item_size = sizeof(ids->array[0]);
        PyObject **new_array = PyMem_Realloc(ids->array, new_size * item_size);
        if (new_array == NULL) {
            PyErr_NoMemory();
            return NULL;
        }
        memset(&new_array[ids->size], 0, (new_size - ids->size) * item_size);
        ids->array = new_array;
        ids->size = new_size;
    }

    // The array stores a strong reference
    ids->array[index] = obj;

    // Return a borrowed reference
    return obj;
}


static void
unicode_clear_identifiers(struct _Py_unicode_state *state)
{
    struct _Py_unicode_ids *ids = &state->ids;
    for (Py_ssize_t i=0; i < ids->size; i++) {
        Py_XDECREF(ids->array[i]);
    }
    ids->size = 0;
    PyMem_Free(ids->array);
    ids->array = NULL;
    // Don't reset _PyRuntime next_index: _Py_Identifier.id remains valid
    // after Py_Finalize().
}


/* Internal function, doesn't check maximum character */

PyObject*
_PyUnicode_FromASCII(const char *buffer, Py_ssize_t size)
{
    const unsigned char *s = (const unsigned char *)buffer;
    PyObject *unicode;
    if (size == 1) {
#ifdef Py_DEBUG
        assert((unsigned char)s[0] < 128);
#endif
        return get_latin1_char(s[0]);
    }
    unicode = PyUnicode_New(size, 127);
    if (!unicode)
        return NULL;
    memcpy(PyUnicode_1BYTE_DATA(unicode), s, size);
    assert(_PyUnicode_CheckConsistency(unicode, 1));
    return unicode;
}

static Py_UCS4
kind_maxchar_limit(unsigned int kind)
{
    switch (kind) {
    case PyUnicode_1BYTE_KIND:
        return 0x80;
    case PyUnicode_2BYTE_KIND:
        return 0x100;
    case PyUnicode_4BYTE_KIND:
        return 0x10000;
    default:
        Py_UNREACHABLE();
    }
}

static PyObject*
_PyUnicode_FromUCS1(const Py_UCS1* u, Py_ssize_t size)
{
    PyObject *res;
    unsigned char max_char;

    if (size == 0) {
        _Py_RETURN_UNICODE_EMPTY();
    }
    assert(size > 0);
    if (size == 1) {
        return get_latin1_char(u[0]);
    }

    max_char = ucs1lib_find_max_char(u, u + size);
    res = PyUnicode_New(size, max_char);
    if (!res)
        return NULL;
    memcpy(PyUnicode_1BYTE_DATA(res), u, size);
    assert(_PyUnicode_CheckConsistency(res, 1));
    return res;
}

static PyObject*
_PyUnicode_FromUCS2(const Py_UCS2 *u, Py_ssize_t size)
{
    PyObject *res;
    Py_UCS2 max_char;

    if (size == 0)
        _Py_RETURN_UNICODE_EMPTY();
    assert(size > 0);
    if (size == 1)
        return unicode_char(u[0]);

    max_char = ucs2lib_find_max_char(u, u + size);
    res = PyUnicode_New(size, max_char);
    if (!res)
        return NULL;
    if (max_char >= 256)
        memcpy(PyUnicode_2BYTE_DATA(res), u, sizeof(Py_UCS2)*size);
    else {
        _PyUnicode_CONVERT_BYTES(
            Py_UCS2, Py_UCS1, u, u + size, PyUnicode_1BYTE_DATA(res));
    }
    assert(_PyUnicode_CheckConsistency(res, 1));
    return res;
}

static PyObject*
_PyUnicode_FromUCS4(const Py_UCS4 *u, Py_ssize_t size)
{
    PyObject *res;
    Py_UCS4 max_char;

    if (size == 0)
        _Py_RETURN_UNICODE_EMPTY();
    assert(size > 0);
    if (size == 1)
        return unicode_char(u[0]);

    max_char = ucs4lib_find_max_char(u, u + size);
    res = PyUnicode_New(size, max_char);
    if (!res)
        return NULL;
    if (max_char < 256)
        _PyUnicode_CONVERT_BYTES(Py_UCS4, Py_UCS1, u, u + size,
                                 PyUnicode_1BYTE_DATA(res));
    else if (max_char < 0x10000)
        _PyUnicode_CONVERT_BYTES(Py_UCS4, Py_UCS2, u, u + size,
                                 PyUnicode_2BYTE_DATA(res));
    else
        memcpy(PyUnicode_4BYTE_DATA(res), u, sizeof(Py_UCS4)*size);
    assert(_PyUnicode_CheckConsistency(res, 1));
    return res;
}

PyObject*
PyUnicode_FromKindAndData(int kind, const void *buffer, Py_ssize_t size)
{
    if (size < 0) {
        PyErr_SetString(PyExc_ValueError, "size must be positive");
        return NULL;
    }
    switch (kind) {
    case PyUnicode_1BYTE_KIND:
        return _PyUnicode_FromUCS1(buffer, size);
    case PyUnicode_2BYTE_KIND:
        return _PyUnicode_FromUCS2(buffer, size);
    case PyUnicode_4BYTE_KIND:
        return _PyUnicode_FromUCS4(buffer, size);
    default:
        PyErr_SetString(PyExc_SystemError, "invalid kind");
        return NULL;
    }
}

Py_UCS4
_PyUnicode_FindMaxChar(PyObject *unicode, Py_ssize_t start, Py_ssize_t end)
{
    enum PyUnicode_Kind kind;
    const void *startptr, *endptr;

    assert(PyUnicode_IS_READY(unicode));
    assert(0 <= start);
    assert(end <= PyUnicode_GET_LENGTH(unicode));
    assert(start <= end);

    if (start == 0 && end == PyUnicode_GET_LENGTH(unicode))
        return PyUnicode_MAX_CHAR_VALUE(unicode);

    if (start == end)
        return 127;

    if (PyUnicode_IS_ASCII(unicode))
        return 127;

    kind = PyUnicode_KIND(unicode);
    startptr = PyUnicode_DATA(unicode);
    endptr = (char *)startptr + end * kind;
    startptr = (char *)startptr + start * kind;
    switch(kind) {
    case PyUnicode_1BYTE_KIND:
        return ucs1lib_find_max_char(startptr, endptr);
    case PyUnicode_2BYTE_KIND:
        return ucs2lib_find_max_char(startptr, endptr);
    case PyUnicode_4BYTE_KIND:
        return ucs4lib_find_max_char(startptr, endptr);
    default:
        Py_UNREACHABLE();
    }
}

/* Ensure that a string uses the most efficient storage, if it is not the
   case: create a new string with of the right kind. Write NULL into *p_unicode
   on error. */
static void
unicode_adjust_maxchar(PyObject **p_unicode)
{
    PyObject *unicode, *copy;
    Py_UCS4 max_char;
    Py_ssize_t len;
    unsigned int kind;

    assert(p_unicode != NULL);
    unicode = *p_unicode;
    assert(PyUnicode_IS_READY(unicode));
    if (PyUnicode_IS_ASCII(unicode))
        return;

    len = PyUnicode_GET_LENGTH(unicode);
    kind = PyUnicode_KIND(unicode);
    if (kind == PyUnicode_1BYTE_KIND) {
        const Py_UCS1 *u = PyUnicode_1BYTE_DATA(unicode);
        max_char = ucs1lib_find_max_char(u, u + len);
        if (max_char >= 128)
            return;
    }
    else if (kind == PyUnicode_2BYTE_KIND) {
        const Py_UCS2 *u = PyUnicode_2BYTE_DATA(unicode);
        max_char = ucs2lib_find_max_char(u, u + len);
        if (max_char >= 256)
            return;
    }
    else if (kind == PyUnicode_4BYTE_KIND) {
        const Py_UCS4 *u = PyUnicode_4BYTE_DATA(unicode);
        max_char = ucs4lib_find_max_char(u, u + len);
        if (max_char >= 0x10000)
            return;
    }
    else
        Py_UNREACHABLE();

    copy = PyUnicode_New(len, max_char);
    if (copy != NULL)
        _PyUnicode_FastCopyCharacters(copy, 0, unicode, 0, len);
    Py_DECREF(unicode);
    *p_unicode = copy;
}

PyObject*
_PyUnicode_Copy(PyObject *unicode)
{
    Py_ssize_t length;
    PyObject *copy;

    if (!PyUnicode_Check(unicode)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    if (PyUnicode_READY(unicode) == -1)
        return NULL;

    length = PyUnicode_GET_LENGTH(unicode);
    copy = PyUnicode_New(length, PyUnicode_MAX_CHAR_VALUE(unicode));
    if (!copy)
        return NULL;
    assert(PyUnicode_KIND(copy) == PyUnicode_KIND(unicode));

    memcpy(PyUnicode_DATA(copy), PyUnicode_DATA(unicode),
              length * PyUnicode_KIND(unicode));
    assert(_PyUnicode_CheckConsistency(copy, 1));
    return copy;
}


/* Widen Unicode objects to larger buffers. Don't write terminating null
   character. Return NULL on error. */

static void*
unicode_askind(unsigned int skind, void const *data, Py_ssize_t len, unsigned int kind)
{
    void *result;

    assert(skind < kind);
    switch (kind) {
    case PyUnicode_2BYTE_KIND:
        result = PyMem_New(Py_UCS2, len);
        if (!result)
            return PyErr_NoMemory();
        assert(skind == PyUnicode_1BYTE_KIND);
        _PyUnicode_CONVERT_BYTES(
            Py_UCS1, Py_UCS2,
            (const Py_UCS1 *)data,
            ((const Py_UCS1 *)data) + len,
            result);
        return result;
    case PyUnicode_4BYTE_KIND:
        result = PyMem_New(Py_UCS4, len);
        if (!result)
            return PyErr_NoMemory();
        if (skind == PyUnicode_2BYTE_KIND) {
            _PyUnicode_CONVERT_BYTES(
                Py_UCS2, Py_UCS4,
                (const Py_UCS2 *)data,
                ((const Py_UCS2 *)data) + len,
                result);
        }
        else {
            assert(skind == PyUnicode_1BYTE_KIND);
            _PyUnicode_CONVERT_BYTES(
                Py_UCS1, Py_UCS4,
                (const Py_UCS1 *)data,
                ((const Py_UCS1 *)data) + len,
                result);
        }
        return result;
    default:
        Py_UNREACHABLE();
        return NULL;
    }
}

static Py_UCS4*
as_ucs4(PyObject *string, Py_UCS4 *target, Py_ssize_t targetsize,
        int copy_null)
{
    int kind;
    const void *data;
    Py_ssize_t len, targetlen;
    if (PyUnicode_READY(string) == -1)
        return NULL;
    kind = PyUnicode_KIND(string);
    data = PyUnicode_DATA(string);
    len = PyUnicode_GET_LENGTH(string);
    targetlen = len;
    if (copy_null)
        targetlen++;
    if (!target) {
        target = PyMem_New(Py_UCS4, targetlen);
        if (!target) {
            PyErr_NoMemory();
            return NULL;
        }
    }
    else {
        if (targetsize < targetlen) {
            PyErr_Format(PyExc_SystemError,
                         "string is longer than the buffer");
            if (copy_null && 0 < targetsize)
                target[0] = 0;
            return NULL;
        }
    }
    if (kind == PyUnicode_1BYTE_KIND) {
        const Py_UCS1 *start = (const Py_UCS1 *) data;
        _PyUnicode_CONVERT_BYTES(Py_UCS1, Py_UCS4, start, start + len, target);
    }
    else if (kind == PyUnicode_2BYTE_KIND) {
        const Py_UCS2 *start = (const Py_UCS2 *) data;
        _PyUnicode_CONVERT_BYTES(Py_UCS2, Py_UCS4, start, start + len, target);
    }
    else if (kind == PyUnicode_4BYTE_KIND) {
        memcpy(target, data, len * sizeof(Py_UCS4));
    }
    else {
        Py_UNREACHABLE();
    }
    if (copy_null)
        target[len] = 0;
    return target;
}

Py_UCS4*
PyUnicode_AsUCS4(PyObject *string, Py_UCS4 *target, Py_ssize_t targetsize,
                 int copy_null)
{
    if (target == NULL || targetsize < 0) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return as_ucs4(string, target, targetsize, copy_null);
}

Py_UCS4*
PyUnicode_AsUCS4Copy(PyObject *string)
{
    return as_ucs4(string, NULL, 0, 1);
}

/* maximum number of characters required for output of %lld or %p.
   We need at most ceil(log10(256)*SIZEOF_LONG_LONG) digits,
   plus 1 for the sign.  53/22 is an upper bound for log10(256). */
#define MAX_LONG_LONG_CHARS (2 + (SIZEOF_LONG_LONG*53-1) / 22)

static int
unicode_fromformat_write_str(_PyUnicodeWriter *writer, PyObject *str,
                             Py_ssize_t width, Py_ssize_t precision)
{
    Py_ssize_t length, fill, arglen;
    Py_UCS4 maxchar;

    if (PyUnicode_READY(str) == -1)
        return -1;

    length = PyUnicode_GET_LENGTH(str);
    if ((precision == -1 || precision >= length)
        && width <= length)
        return _PyUnicodeWriter_WriteStr(writer, str);

    if (precision != -1)
        length = Py_MIN(precision, length);

    arglen = Py_MAX(length, width);
    if (PyUnicode_MAX_CHAR_VALUE(str) > writer->maxchar)
        maxchar = _PyUnicode_FindMaxChar(str, 0, length);
    else
        maxchar = writer->maxchar;

    if (_PyUnicodeWriter_Prepare(writer, arglen, maxchar) == -1)
        return -1;

    if (width > length) {
        fill = width - length;
        if (PyUnicode_Fill(writer->buffer, writer->pos, fill, ' ') == -1)
            return -1;
        writer->pos += fill;
    }

    _PyUnicode_FastCopyCharacters(writer->buffer, writer->pos,
                                  str, 0, length);
    writer->pos += length;
    return 0;
}

static int
unicode_fromformat_write_cstr(_PyUnicodeWriter *writer, const char *str,
                              Py_ssize_t width, Py_ssize_t precision)
{
    /* UTF-8 */
    Py_ssize_t length;
    PyObject *unicode;
    int res;

    if (precision == -1) {
        length = strlen(str);
    }
    else {
        length = 0;
        while (length < precision && str[length]) {
            length++;
        }
    }
    unicode = PyUnicode_DecodeUTF8Stateful(str, length, "replace", NULL);
    if (unicode == NULL)
        return -1;

    res = unicode_fromformat_write_str(writer, unicode, width, -1);
    Py_DECREF(unicode);
    return res;
}

static const char*
unicode_fromformat_arg(_PyUnicodeWriter *writer,
                       const char *f, va_list *vargs)
{
    const char *p;
    Py_ssize_t len;
    int zeropad;
    Py_ssize_t width;
    Py_ssize_t precision;
    int longflag;
    int longlongflag;
    int size_tflag;
    Py_ssize_t fill;

    p = f;
    f++;
    zeropad = 0;
    if (*f == '0') {
        zeropad = 1;
        f++;
    }

    /* parse the width.precision part, e.g. "%2.5s" => width=2, precision=5 */
    width = -1;
    if (Py_ISDIGIT((unsigned)*f)) {
        width = *f - '0';
        f++;
        while (Py_ISDIGIT((unsigned)*f)) {
            if (width > (PY_SSIZE_T_MAX - ((int)*f - '0')) / 10) {
                PyErr_SetString(PyExc_ValueError,
                                "width too big");
                return NULL;
            }
            width = (width * 10) + (*f - '0');
            f++;
        }
    }
    precision = -1;
    if (*f == '.') {
        f++;
        if (Py_ISDIGIT((unsigned)*f)) {
            precision = (*f - '0');
            f++;
            while (Py_ISDIGIT((unsigned)*f)) {
                if (precision > (PY_SSIZE_T_MAX - ((int)*f - '0')) / 10) {
                    PyErr_SetString(PyExc_ValueError,
                                    "precision too big");
                    return NULL;
                }
                precision = (precision * 10) + (*f - '0');
                f++;
            }
        }
        if (*f == '%') {
            /* "%.3%s" => f points to "3" */
            f--;
        }
    }
    if (*f == '\0') {
        /* bogus format "%.123" => go backward, f points to "3" */
        f--;
    }

    /* Handle %ld, %lu, %lld and %llu. */
    longflag = 0;
    longlongflag = 0;
    size_tflag = 0;
    if (*f == 'l') {
        if (f[1] == 'd' || f[1] == 'u' || f[1] == 'i') {
            longflag = 1;
            ++f;
        }
        else if (f[1] == 'l' &&
                 (f[2] == 'd' || f[2] == 'u' || f[2] == 'i')) {
            longlongflag = 1;
            f += 2;
        }
    }
    /* handle the size_t flag. */
    else if (*f == 'z' && (f[1] == 'd' || f[1] == 'u' || f[1] == 'i')) {
        size_tflag = 1;
        ++f;
    }

    if (f[1] == '\0')
        writer->overallocate = 0;

    switch (*f) {
    case 'c':
    {
        int ordinal = va_arg(*vargs, int);
        if (ordinal < 0 || ordinal > MAX_UNICODE) {
            PyErr_SetString(PyExc_OverflowError,
                            "character argument not in range(0x110000)");
            return NULL;
        }
        if (_PyUnicodeWriter_WriteCharInline(writer, ordinal) < 0)
            return NULL;
        break;
    }

    case 'i':
    case 'd':
    case 'u':
    case 'x':
    {
        /* used by sprintf */
        char buffer[MAX_LONG_LONG_CHARS];
        Py_ssize_t arglen;

        if (*f == 'u') {
            if (longflag) {
                len = sprintf(buffer, "%lu", va_arg(*vargs, unsigned long));
            }
            else if (longlongflag) {
                len = sprintf(buffer, "%llu", va_arg(*vargs, unsigned long long));
            }
            else if (size_tflag) {
                len = sprintf(buffer, "%zu", va_arg(*vargs, size_t));
            }
            else {
                len = sprintf(buffer, "%u", va_arg(*vargs, unsigned int));
            }
        }
        else if (*f == 'x') {
            len = sprintf(buffer, "%x", va_arg(*vargs, int));
        }
        else {
            if (longflag) {
                len = sprintf(buffer, "%li", va_arg(*vargs, long));
            }
            else if (longlongflag) {
                len = sprintf(buffer, "%lli", va_arg(*vargs, long long));
            }
            else if (size_tflag) {
                len = sprintf(buffer, "%zi", va_arg(*vargs, Py_ssize_t));
            }
            else {
                len = sprintf(buffer, "%i", va_arg(*vargs, int));
            }
        }
        assert(len >= 0);

        if (precision < len)
            precision = len;

        arglen = Py_MAX(precision, width);
        if (_PyUnicodeWriter_Prepare(writer, arglen, 127) == -1)
            return NULL;

        if (width > precision) {
            Py_UCS4 fillchar;
            fill = width - precision;
            fillchar = zeropad?'0':' ';
            if (PyUnicode_Fill(writer->buffer, writer->pos, fill, fillchar) == -1)
                return NULL;
            writer->pos += fill;
        }
        if (precision > len) {
            fill = precision - len;
            if (PyUnicode_Fill(writer->buffer, writer->pos, fill, '0') == -1)
                return NULL;
            writer->pos += fill;
        }

        if (_PyUnicodeWriter_WriteASCIIString(writer, buffer, len) < 0)
            return NULL;
        break;
    }

    case 'p':
    {
        char number[MAX_LONG_LONG_CHARS];

        len = sprintf(number, "%p", va_arg(*vargs, void*));
        assert(len >= 0);

        /* %p is ill-defined:  ensure leading 0x. */
        if (number[1] == 'X')
            number[1] = 'x';
        else if (number[1] != 'x') {
            memmove(number + 2, number,
                    strlen(number) + 1);
            number[0] = '0';
            number[1] = 'x';
            len += 2;
        }

        if (_PyUnicodeWriter_WriteASCIIString(writer, number, len) < 0)
            return NULL;
        break;
    }

    case 's':
    {
        /* UTF-8 */
        const char *s = va_arg(*vargs, const char*);
        if (unicode_fromformat_write_cstr(writer, s, width, precision) < 0)
            return NULL;
        break;
    }

    case 'U':
    {
        PyObject *obj = va_arg(*vargs, PyObject *);
        assert(obj && _PyUnicode_CHECK(obj));

        if (unicode_fromformat_write_str(writer, obj, width, precision) == -1)
            return NULL;
        break;
    }

    case 'V':
    {
        PyObject *obj = va_arg(*vargs, PyObject *);
        const char *str = va_arg(*vargs, const char *);
        if (obj) {
            assert(_PyUnicode_CHECK(obj));
            if (unicode_fromformat_write_str(writer, obj, width, precision) == -1)
                return NULL;
        }
        else {
            assert(str != NULL);
            if (unicode_fromformat_write_cstr(writer, str, width, precision) < 0)
                return NULL;
        }
        break;
    }

    case 'S':
    {
        PyObject *obj = va_arg(*vargs, PyObject *);
        PyObject *str;
        assert(obj);
        str = PyObject_Str(obj);
        if (!str)
            return NULL;
        if (unicode_fromformat_write_str(writer, str, width, precision) == -1) {
            Py_DECREF(str);
            return NULL;
        }
        Py_DECREF(str);
        break;
    }

    case 'R':
    {
        PyObject *obj = va_arg(*vargs, PyObject *);
        PyObject *repr;
        assert(obj);
        repr = PyObject_Repr(obj);
        if (!repr)
            return NULL;
        if (unicode_fromformat_write_str(writer, repr, width, precision) == -1) {
            Py_DECREF(repr);
            return NULL;
        }
        Py_DECREF(repr);
        break;
    }

    case 'A':
    {
        PyObject *obj = va_arg(*vargs, PyObject *);
        PyObject *ascii;
        assert(obj);
        ascii = PyObject_ASCII(obj);
        if (!ascii)
            return NULL;
        if (unicode_fromformat_write_str(writer, ascii, width, precision) == -1) {
            Py_DECREF(ascii);
            return NULL;
        }
        Py_DECREF(ascii);
        break;
    }

    case '%':
        if (_PyUnicodeWriter_WriteCharInline(writer, '%') < 0)
            return NULL;
        break;

    default:
        /* if we stumble upon an unknown formatting code, copy the rest
           of the format string to the output string. (we cannot just
           skip the code, since there's no way to know what's in the
           argument list) */
        len = strlen(p);
        if (_PyUnicodeWriter_WriteLatin1String(writer, p, len) == -1)
            return NULL;
        f = p+len;
        return f;
    }

    f++;
    return f;
}

PyObject *
PyUnicode_FromFormatV(const char *format, va_list vargs)
{
    va_list vargs2;
    const char *f;
    _PyUnicodeWriter writer;

    _PyUnicodeWriter_Init(&writer);
    writer.min_length = strlen(format) + 100;
    writer.overallocate = 1;

    // Copy varags to be able to pass a reference to a subfunction.
    va_copy(vargs2, vargs);

    for (f = format; *f; ) {
        if (*f == '%') {
            f = unicode_fromformat_arg(&writer, f, &vargs2);
            if (f == NULL)
                goto fail;
        }
        else {
            const char *p;
            Py_ssize_t len;

            p = f;
            do
            {
                if ((unsigned char)*p > 127) {
                    PyErr_Format(PyExc_ValueError,
                        "PyUnicode_FromFormatV() expects an ASCII-encoded format "
                        "string, got a non-ASCII byte: 0x%02x",
                        (unsigned char)*p);
                    goto fail;
                }
                p++;
            }
            while (*p != '\0' && *p != '%');
            len = p - f;

            if (*p == '\0')
                writer.overallocate = 0;

            if (_PyUnicodeWriter_WriteASCIIString(&writer, f, len) < 0)
                goto fail;

            f = p;
        }
    }
    va_end(vargs2);
    return _PyUnicodeWriter_Finish(&writer);

  fail:
    va_end(vargs2);
    _PyUnicodeWriter_Dealloc(&writer);
    return NULL;
}

PyObject *
PyUnicode_FromFormat(const char *format, ...)
{
    PyObject* ret;
    va_list vargs;

#ifdef HAVE_STDARG_PROTOTYPES
    va_start(vargs, format);
#else
    va_start(vargs);
#endif
    ret = PyUnicode_FromFormatV(format, vargs);
    va_end(vargs);
    return ret;
}

static Py_ssize_t
unicode_get_widechar_size(PyObject *unicode)
{
    Py_ssize_t res;

    assert(unicode != NULL);
    assert(_PyUnicode_CHECK(unicode));

#if USE_UNICODE_WCHAR_CACHE
    if (_PyUnicode_WSTR(unicode) != NULL) {
        return PyUnicode_WSTR_LENGTH(unicode);
    }
#endif /* USE_UNICODE_WCHAR_CACHE */
    assert(PyUnicode_IS_READY(unicode));

    res = _PyUnicode_LENGTH(unicode);
#if SIZEOF_WCHAR_T == 2
    if (PyUnicode_KIND(unicode) == PyUnicode_4BYTE_KIND) {
        const Py_UCS4 *s = PyUnicode_4BYTE_DATA(unicode);
        const Py_UCS4 *end = s + res;
        for (; s < end; ++s) {
            if (*s > 0xFFFF) {
                ++res;
            }
        }
    }
#endif
    return res;
}

static void
unicode_copy_as_widechar(PyObject *unicode, wchar_t *w, Py_ssize_t size)
{
    assert(unicode != NULL);
    assert(_PyUnicode_CHECK(unicode));

#if USE_UNICODE_WCHAR_CACHE
    const wchar_t *wstr = _PyUnicode_WSTR(unicode);
    if (wstr != NULL) {
        memcpy(w, wstr, size * sizeof(wchar_t));
        return;
    }
#else /* USE_UNICODE_WCHAR_CACHE */
    if (PyUnicode_KIND(unicode) == sizeof(wchar_t)) {
        memcpy(w, PyUnicode_DATA(unicode), size * sizeof(wchar_t));
        return;
    }
#endif /* USE_UNICODE_WCHAR_CACHE */
    assert(PyUnicode_IS_READY(unicode));

    if (PyUnicode_KIND(unicode) == PyUnicode_1BYTE_KIND) {
        const Py_UCS1 *s = PyUnicode_1BYTE_DATA(unicode);
        for (; size--; ++s, ++w) {
            *w = *s;
        }
    }
    else {
#if SIZEOF_WCHAR_T == 4
        assert(PyUnicode_KIND(unicode) == PyUnicode_2BYTE_KIND);
        const Py_UCS2 *s = PyUnicode_2BYTE_DATA(unicode);
        for (; size--; ++s, ++w) {
            *w = *s;
        }
#else
        assert(PyUnicode_KIND(unicode) == PyUnicode_4BYTE_KIND);
        const Py_UCS4 *s = PyUnicode_4BYTE_DATA(unicode);
        for (; size--; ++s, ++w) {
            Py_UCS4 ch = *s;
            if (ch > 0xFFFF) {
                assert(ch <= MAX_UNICODE);
                /* encode surrogate pair in this case */
                *w++ = Py_UNICODE_HIGH_SURROGATE(ch);
                if (!size--)
                    break;
                *w = Py_UNICODE_LOW_SURROGATE(ch);
            }
            else {
                *w = ch;
            }
        }
#endif
    }
}

#ifdef HAVE_WCHAR_H

/* Convert a Unicode object to a wide character string.

   - If w is NULL: return the number of wide characters (including the null
     character) required to convert the unicode object. Ignore size argument.

   - Otherwise: return the number of wide characters (excluding the null
     character) written into w. Write at most size wide characters (including
     the null character). */
Py_ssize_t
PyUnicode_AsWideChar(PyObject *unicode,
                     wchar_t *w,
                     Py_ssize_t size)
{
    Py_ssize_t res;

    if (unicode == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return -1;
    }

    res = unicode_get_widechar_size(unicode);
    if (w == NULL) {
        return res + 1;
    }

    if (size > res) {
        size = res + 1;
    }
    else {
        res = size;
    }
    unicode_copy_as_widechar(unicode, w, size);

#if HAVE_NON_UNICODE_WCHAR_T_REPRESENTATION
    /* Oracle Solaris uses non-Unicode internal wchar_t form for
       non-Unicode locales and hence needs conversion first. */
    if (_Py_LocaleUsesNonUnicodeWchar()) {
        if (_Py_EncodeNonUnicodeWchar_InPlace(w, size) < 0) {
            return -1;
        }
    }
#endif

    return res;
}

wchar_t*
PyUnicode_AsWideCharString(PyObject *unicode,
                           Py_ssize_t *size)
{
    wchar_t *buffer;
    Py_ssize_t buflen;

    if (unicode == NULL) {
        PyErr_BadInternalCall();
        return NULL;
    }
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return NULL;
    }

    buflen = unicode_get_widechar_size(unicode);
    buffer = (wchar_t *) PyMem_NEW(wchar_t, (buflen + 1));
    if (buffer == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    unicode_copy_as_widechar(unicode, buffer, buflen + 1);

#if HAVE_NON_UNICODE_WCHAR_T_REPRESENTATION
    /* Oracle Solaris uses non-Unicode internal wchar_t form for
       non-Unicode locales and hence needs conversion first. */
    if (_Py_LocaleUsesNonUnicodeWchar()) {
        if (_Py_EncodeNonUnicodeWchar_InPlace(buffer, (buflen + 1)) < 0) {
            return NULL;
        }
    }
#endif

    if (size != NULL) {
        *size = buflen;
    }
    else if (wcslen(buffer) != (size_t)buflen) {
        PyMem_Free(buffer);
        PyErr_SetString(PyExc_ValueError,
                        "embedded null character");
        return NULL;
    }
    return buffer;
}

#endif /* HAVE_WCHAR_H */

int
_PyUnicode_WideCharString_Converter(PyObject *obj, void *ptr)
{
    wchar_t **p = (wchar_t **)ptr;
    if (obj == NULL) {
#if !USE_UNICODE_WCHAR_CACHE
        PyMem_Free(*p);
#endif /* USE_UNICODE_WCHAR_CACHE */
        *p = NULL;
        return 1;
    }
    if (PyUnicode_Check(obj)) {
#if USE_UNICODE_WCHAR_CACHE
        *p = (wchar_t *)_PyUnicode_AsUnicode(obj);
        if (*p == NULL) {
            return 0;
        }
        return 1;
#else /* USE_UNICODE_WCHAR_CACHE */
        *p = PyUnicode_AsWideCharString(obj, NULL);
        if (*p == NULL) {
            return 0;
        }
        return Py_CLEANUP_SUPPORTED;
#endif /* USE_UNICODE_WCHAR_CACHE */
    }
    PyErr_Format(PyExc_TypeError,
                 "argument must be str, not %.50s",
                 Py_TYPE(obj)->tp_name);
    return 0;
}

int
_PyUnicode_WideCharString_Opt_Converter(PyObject *obj, void *ptr)
{
    wchar_t **p = (wchar_t **)ptr;
    if (obj == NULL) {
#if !USE_UNICODE_WCHAR_CACHE
        PyMem_Free(*p);
#endif /* USE_UNICODE_WCHAR_CACHE */
        *p = NULL;
        return 1;
    }
    if (obj == Py_None) {
        *p = NULL;
        return 1;
    }
    if (PyUnicode_Check(obj)) {
#if USE_UNICODE_WCHAR_CACHE
        *p = (wchar_t *)_PyUnicode_AsUnicode(obj);
        if (*p == NULL) {
            return 0;
        }
        return 1;
#else /* USE_UNICODE_WCHAR_CACHE */
        *p = PyUnicode_AsWideCharString(obj, NULL);
        if (*p == NULL) {
            return 0;
        }
        return Py_CLEANUP_SUPPORTED;
#endif /* USE_UNICODE_WCHAR_CACHE */
    }
    PyErr_Format(PyExc_TypeError,
                 "argument must be str or None, not %.50s",
                 Py_TYPE(obj)->tp_name);
    return 0;
}

PyObject *
PyUnicode_FromOrdinal(int ordinal)
{
    if (ordinal < 0 || ordinal > MAX_UNICODE) {
        PyErr_SetString(PyExc_ValueError,
                        "chr() arg not in range(0x110000)");
        return NULL;
    }

    return unicode_char((Py_UCS4)ordinal);
}

PyObject *
PyUnicode_FromObject(PyObject *obj)
{
    /* XXX Perhaps we should make this API an alias of
       PyObject_Str() instead ?! */
    if (PyUnicode_CheckExact(obj)) {
        if (PyUnicode_READY(obj) == -1)
            return NULL;
        Py_INCREF(obj);
        return obj;
    }
    if (PyUnicode_Check(obj)) {
        /* For a Unicode subtype that's not a Unicode object,
           return a true Unicode object with the same data. */
        return _PyUnicode_Copy(obj);
    }
    PyErr_Format(PyExc_TypeError,
                 "Can't convert '%.100s' object to str implicitly",
                 Py_TYPE(obj)->tp_name);
    return NULL;
}

PyObject *
PyUnicode_FromEncodedObject(PyObject *obj,
                            const char *encoding,
                            const char *errors)
{
    Py_buffer buffer;
    PyObject *v;

    if (obj == NULL) {
        PyErr_BadInternalCall();
        return NULL;
    }

    /* Decoding bytes objects is the most common case and should be fast */
    if (PyBytes_Check(obj)) {
        if (PyBytes_GET_SIZE(obj) == 0) {
            if (unicode_check_encoding_errors(encoding, errors) < 0) {
                return NULL;
            }
            _Py_RETURN_UNICODE_EMPTY();
        }
        return PyUnicode_Decode(
                PyBytes_AS_STRING(obj), PyBytes_GET_SIZE(obj),
                encoding, errors);
    }

    if (PyUnicode_Check(obj)) {
        PyErr_SetString(PyExc_TypeError,
                        "decoding str is not supported");
        return NULL;
    }

    /* Retrieve a bytes buffer view through the PEP 3118 buffer interface */
    if (PyObject_GetBuffer(obj, &buffer, PyBUF_SIMPLE) < 0) {
        PyErr_Format(PyExc_TypeError,
                     "decoding to str: need a bytes-like object, %.80s found",
                     Py_TYPE(obj)->tp_name);
        return NULL;
    }

    if (buffer.len == 0) {
        PyBuffer_Release(&buffer);
        if (unicode_check_encoding_errors(encoding, errors) < 0) {
            return NULL;
        }
        _Py_RETURN_UNICODE_EMPTY();
    }

    v = PyUnicode_Decode((char*) buffer.buf, buffer.len, encoding, errors);
    PyBuffer_Release(&buffer);
    return v;
}

/* Normalize an encoding name: similar to encodings.normalize_encoding(), but
   also convert to lowercase. Return 1 on success, or 0 on error (encoding is
   longer than lower_len-1). */
int
_Py_normalize_encoding(const char *encoding,
                       char *lower,
                       size_t lower_len)
{
    const char *e;
    char *l;
    char *l_end;
    int punct;

    assert(encoding != NULL);

    e = encoding;
    l = lower;
    l_end = &lower[lower_len - 1];
    punct = 0;
    while (1) {
        char c = *e;
        if (c == 0) {
            break;
        }

        if (Py_ISALNUM(c) || c == '.') {
            if (punct && l != lower) {
                if (l == l_end) {
                    return 0;
                }
                *l++ = '_';
            }
            punct = 0;

            if (l == l_end) {
                return 0;
            }
            *l++ = Py_TOLOWER(c);
        }
        else {
            punct = 1;
        }

        e++;
    }
    *l = '\0';
    return 1;
}

PyObject *
PyUnicode_Decode(const char *s,
                 Py_ssize_t size,
                 const char *encoding,
                 const char *errors)
{
    PyObject *buffer = NULL, *unicode;
    Py_buffer info;
    char buflower[11];   /* strlen("iso-8859-1\0") == 11, longest shortcut */

    if (unicode_check_encoding_errors(encoding, errors) < 0) {
        return NULL;
    }

    if (size == 0) {
        _Py_RETURN_UNICODE_EMPTY();
    }

    if (encoding == NULL) {
        return PyUnicode_DecodeUTF8Stateful(s, size, errors, NULL);
    }

    /* Shortcuts for common default encodings */
    if (_Py_normalize_encoding(encoding, buflower, sizeof(buflower))) {
        char *lower = buflower;

        /* Fast paths */
        if (lower[0] == 'u' && lower[1] == 't' && lower[2] == 'f') {
            lower += 3;
            if (*lower == '_') {
                /* Match "utf8" and "utf_8" */
                lower++;
            }

            if (lower[0] == '8' && lower[1] == 0) {
                return PyUnicode_DecodeUTF8Stateful(s, size, errors, NULL);
            }
            else if (lower[0] == '1' && lower[1] == '6' && lower[2] == 0) {
                return PyUnicode_DecodeUTF16(s, size, errors, 0);
            }
            else if (lower[0] == '3' && lower[1] == '2' && lower[2] == 0) {
                return PyUnicode_DecodeUTF32(s, size, errors, 0);
            }
        }
        else {
            if (strcmp(lower, "ascii") == 0
                || strcmp(lower, "us_ascii") == 0) {
                return PyUnicode_DecodeASCII(s, size, errors);
            }
    #ifdef MS_WINDOWS
            else if (strcmp(lower, "mbcs") == 0) {
                return PyUnicode_DecodeMBCS(s, size, errors);
            }
    #endif
            else if (strcmp(lower, "latin1") == 0
                     || strcmp(lower, "latin_1") == 0
                     || strcmp(lower, "iso_8859_1") == 0
                     || strcmp(lower, "iso8859_1") == 0) {
                return PyUnicode_DecodeLatin1(s, size, errors);
            }
        }
    }

    /* Decode via the codec registry */
    buffer = NULL;
    if (PyBuffer_FillInfo(&info, NULL, (void *)s, size, 1, PyBUF_FULL_RO) < 0)
        goto onError;
    buffer = PyMemoryView_FromBuffer(&info);
    if (buffer == NULL)
        goto onError;
    unicode = _PyCodec_DecodeText(buffer, encoding, errors);
    if (unicode == NULL)
        goto onError;
    if (!PyUnicode_Check(unicode)) {
        PyErr_Format(PyExc_TypeError,
                     "'%.400s' decoder returned '%.400s' instead of 'str'; "
                     "use codecs.decode() to decode to arbitrary types",
                     encoding,
                     Py_TYPE(unicode)->tp_name);
        Py_DECREF(unicode);
        goto onError;
    }
    Py_DECREF(buffer);
    return unicode_result(unicode);

  onError:
    Py_XDECREF(buffer);
    return NULL;
}

PyObject *
PyUnicode_AsDecodedObject(PyObject *unicode,
                          const char *encoding,
                          const char *errors)
{
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return NULL;
    }

    if (PyErr_WarnEx(PyExc_DeprecationWarning,
                     "PyUnicode_AsDecodedObject() is deprecated; "
                     "use PyCodec_Decode() to decode from str", 1) < 0)
        return NULL;

    if (encoding == NULL)
        encoding = PyUnicode_GetDefaultEncoding();

    /* Decode via the codec registry */
    return PyCodec_Decode(unicode, encoding, errors);
}

PyObject *
PyUnicode_AsDecodedUnicode(PyObject *unicode,
                           const char *encoding,
                           const char *errors)
{
    PyObject *v;

    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        goto onError;
    }

    if (PyErr_WarnEx(PyExc_DeprecationWarning,
                     "PyUnicode_AsDecodedUnicode() is deprecated; "
                     "use PyCodec_Decode() to decode from str to str", 1) < 0)
        return NULL;

    if (encoding == NULL)
        encoding = PyUnicode_GetDefaultEncoding();

    /* Decode via the codec registry */
    v = PyCodec_Decode(unicode, encoding, errors);
    if (v == NULL)
        goto onError;
    if (!PyUnicode_Check(v)) {
        PyErr_Format(PyExc_TypeError,
                     "'%.400s' decoder returned '%.400s' instead of 'str'; "
                     "use codecs.decode() to decode to arbitrary types",
                     encoding,
                     Py_TYPE(unicode)->tp_name);
        Py_DECREF(v);
        goto onError;
    }
    return unicode_result(v);

  onError:
    return NULL;
}

PyObject *
PyUnicode_Encode(const Py_UNICODE *s,
                 Py_ssize_t size,
                 const char *encoding,
                 const char *errors)
{
    PyObject *v, *unicode;

    unicode = PyUnicode_FromWideChar(s, size);
    if (unicode == NULL)
        return NULL;
    v = PyUnicode_AsEncodedString(unicode, encoding, errors);
    Py_DECREF(unicode);
    return v;
}

PyObject *
PyUnicode_AsEncodedObject(PyObject *unicode,
                          const char *encoding,
                          const char *errors)
{
    PyObject *v;

    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        goto onError;
    }

    if (PyErr_WarnEx(PyExc_DeprecationWarning,
                     "PyUnicode_AsEncodedObject() is deprecated; "
                     "use PyUnicode_AsEncodedString() to encode from str to bytes "
                     "or PyCodec_Encode() for generic encoding", 1) < 0)
        return NULL;

    if (encoding == NULL)
        encoding = PyUnicode_GetDefaultEncoding();

    /* Encode via the codec registry */
    v = PyCodec_Encode(unicode, encoding, errors);
    if (v == NULL)
        goto onError;
    return v;

  onError:
    return NULL;
}


static PyObject *
unicode_encode_locale(PyObject *unicode, _Py_error_handler error_handler,
                      int current_locale)
{
    Py_ssize_t wlen;
    wchar_t *wstr = PyUnicode_AsWideCharString(unicode, &wlen);
    if (wstr == NULL) {
        return NULL;
    }

    if ((size_t)wlen != wcslen(wstr)) {
        PyErr_SetString(PyExc_ValueError, "embedded null character");
        PyMem_Free(wstr);
        return NULL;
    }

    char *str;
    size_t error_pos;
    const char *reason;
    int res = _Py_EncodeLocaleEx(wstr, &str, &error_pos, &reason,
                                 current_locale, error_handler);
    PyMem_Free(wstr);

    if (res != 0) {
        if (res == -2) {
            PyObject *exc;
            exc = PyObject_CallFunction(PyExc_UnicodeEncodeError, "sOnns",
                    "locale", unicode,
                    (Py_ssize_t)error_pos,
                    (Py_ssize_t)(error_pos+1),
                    reason);
            if (exc != NULL) {
                PyCodec_StrictErrors(exc);
                Py_DECREF(exc);
            }
        }
        else if (res == -3) {
            PyErr_SetString(PyExc_ValueError, "unsupported error handler");
        }
        else {
            PyErr_NoMemory();
        }
        return NULL;
    }

    PyObject *bytes = PyBytes_FromString(str);
    PyMem_RawFree(str);
    return bytes;
}

PyObject *
PyUnicode_EncodeLocale(PyObject *unicode, const char *errors)
{
    _Py_error_handler error_handler = _Py_GetErrorHandler(errors);
    return unicode_encode_locale(unicode, error_handler, 1);
}

PyObject *
PyUnicode_EncodeFSDefault(PyObject *unicode)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    struct _Py_unicode_fs_codec *fs_codec = &interp->unicode.fs_codec;
    if (fs_codec->utf8) {
        return unicode_encode_utf8(unicode,
                                   fs_codec->error_handler,
                                   fs_codec->errors);
    }
#ifndef _Py_FORCE_UTF8_FS_ENCODING
    else if (fs_codec->encoding) {
        return PyUnicode_AsEncodedString(unicode,
                                         fs_codec->encoding,
                                         fs_codec->errors);
    }
#endif
    else {
        /* Before _PyUnicode_InitEncodings() is called, the Python codec
           machinery is not ready and so cannot be used:
           use wcstombs() in this case. */
        const PyConfig *config = _PyInterpreterState_GetConfig(interp);
        const wchar_t *filesystem_errors = config->filesystem_errors;
        assert(filesystem_errors != NULL);
        _Py_error_handler errors = get_error_handler_wide(filesystem_errors);
        assert(errors != _Py_ERROR_UNKNOWN);
#ifdef _Py_FORCE_UTF8_FS_ENCODING
        return unicode_encode_utf8(unicode, errors, NULL);
#else
        return unicode_encode_locale(unicode, errors, 0);
#endif
    }
}

PyObject *
PyUnicode_AsEncodedString(PyObject *unicode,
                          const char *encoding,
                          const char *errors)
{
    PyObject *v;
    char buflower[11];   /* strlen("iso_8859_1\0") == 11, longest shortcut */

    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return NULL;
    }

    if (unicode_check_encoding_errors(encoding, errors) < 0) {
        return NULL;
    }

    if (encoding == NULL) {
        return _PyUnicode_AsUTF8String(unicode, errors);
    }

    /* Shortcuts for common default encodings */
    if (_Py_normalize_encoding(encoding, buflower, sizeof(buflower))) {
        char *lower = buflower;

        /* Fast paths */
        if (lower[0] == 'u' && lower[1] == 't' && lower[2] == 'f') {
            lower += 3;
            if (*lower == '_') {
                /* Match "utf8" and "utf_8" */
                lower++;
            }

            if (lower[0] == '8' && lower[1] == 0) {
                return _PyUnicode_AsUTF8String(unicode, errors);
            }
            else if (lower[0] == '1' && lower[1] == '6' && lower[2] == 0) {
                return _PyUnicode_EncodeUTF16(unicode, errors, 0);
            }
            else if (lower[0] == '3' && lower[1] == '2' && lower[2] == 0) {
                return _PyUnicode_EncodeUTF32(unicode, errors, 0);
            }
        }
        else {
            if (strcmp(lower, "ascii") == 0
                || strcmp(lower, "us_ascii") == 0) {
                return _PyUnicode_AsASCIIString(unicode, errors);
            }
#ifdef MS_WINDOWS
            else if (strcmp(lower, "mbcs") == 0) {
                return PyUnicode_EncodeCodePage(CP_ACP, unicode, errors);
            }
#endif
            else if (strcmp(lower, "latin1") == 0 ||
                     strcmp(lower, "latin_1") == 0 ||
                     strcmp(lower, "iso_8859_1") == 0 ||
                     strcmp(lower, "iso8859_1") == 0) {
                return _PyUnicode_AsLatin1String(unicode, errors);
            }
        }
    }

    /* Encode via the codec registry */
    v = _PyCodec_EncodeText(unicode, encoding, errors);
    if (v == NULL)
        return NULL;

    /* The normal path */
    if (PyBytes_Check(v))
        return v;

    /* If the codec returns a buffer, raise a warning and convert to bytes */
    if (PyByteArray_Check(v)) {
        int error;
        PyObject *b;

        error = PyErr_WarnFormat(PyExc_RuntimeWarning, 1,
            "encoder %s returned bytearray instead of bytes; "
            "use codecs.encode() to encode to arbitrary types",
            encoding);
        if (error) {
            Py_DECREF(v);
            return NULL;
        }

        b = PyBytes_FromStringAndSize(PyByteArray_AS_STRING(v),
                                      PyByteArray_GET_SIZE(v));
        Py_DECREF(v);
        return b;
    }

    PyErr_Format(PyExc_TypeError,
                 "'%.400s' encoder returned '%.400s' instead of 'bytes'; "
                 "use codecs.encode() to encode to arbitrary types",
                 encoding,
                 Py_TYPE(v)->tp_name);
    Py_DECREF(v);
    return NULL;
}

PyObject *
PyUnicode_AsEncodedUnicode(PyObject *unicode,
                           const char *encoding,
                           const char *errors)
{
    PyObject *v;

    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        goto onError;
    }

    if (PyErr_WarnEx(PyExc_DeprecationWarning,
                     "PyUnicode_AsEncodedUnicode() is deprecated; "
                     "use PyCodec_Encode() to encode from str to str", 1) < 0)
        return NULL;

    if (encoding == NULL)
        encoding = PyUnicode_GetDefaultEncoding();

    /* Encode via the codec registry */
    v = PyCodec_Encode(unicode, encoding, errors);
    if (v == NULL)
        goto onError;
    if (!PyUnicode_Check(v)) {
        PyErr_Format(PyExc_TypeError,
                     "'%.400s' encoder returned '%.400s' instead of 'str'; "
                     "use codecs.encode() to encode to arbitrary types",
                     encoding,
                     Py_TYPE(v)->tp_name);
        Py_DECREF(v);
        goto onError;
    }
    return v;

  onError:
    return NULL;
}

static PyObject*
unicode_decode_locale(const char *str, Py_ssize_t len,
                      _Py_error_handler errors, int current_locale)
{
    if (str[len] != '\0' || (size_t)len != strlen(str))  {
        PyErr_SetString(PyExc_ValueError, "embedded null byte");
        return NULL;
    }

    wchar_t *wstr;
    size_t wlen;
    const char *reason;
    int res = _Py_DecodeLocaleEx(str, &wstr, &wlen, &reason,
                                 current_locale, errors);
    if (res != 0) {
        if (res == -2) {
            PyObject *exc;
            exc = PyObject_CallFunction(PyExc_UnicodeDecodeError, "sy#nns",
                                        "locale", str, len,
                                        (Py_ssize_t)wlen,
                                        (Py_ssize_t)(wlen + 1),
                                        reason);
            if (exc != NULL) {
                PyCodec_StrictErrors(exc);
                Py_DECREF(exc);
            }
        }
        else if (res == -3) {
            PyErr_SetString(PyExc_ValueError, "unsupported error handler");
        }
        else {
            PyErr_NoMemory();
        }
        return NULL;
    }

    PyObject *unicode = PyUnicode_FromWideChar(wstr, wlen);
    PyMem_RawFree(wstr);
    return unicode;
}

PyObject*
PyUnicode_DecodeLocaleAndSize(const char *str, Py_ssize_t len,
                              const char *errors)
{
    _Py_error_handler error_handler = _Py_GetErrorHandler(errors);
    return unicode_decode_locale(str, len, error_handler, 1);
}

PyObject*
PyUnicode_DecodeLocale(const char *str, const char *errors)
{
    Py_ssize_t size = (Py_ssize_t)strlen(str);
    _Py_error_handler error_handler = _Py_GetErrorHandler(errors);
    return unicode_decode_locale(str, size, error_handler, 1);
}


PyObject*
PyUnicode_DecodeFSDefault(const char *s) {
    Py_ssize_t size = (Py_ssize_t)strlen(s);
    return PyUnicode_DecodeFSDefaultAndSize(s, size);
}

PyObject*
PyUnicode_DecodeFSDefaultAndSize(const char *s, Py_ssize_t size)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    struct _Py_unicode_fs_codec *fs_codec = &interp->unicode.fs_codec;
    if (fs_codec->utf8) {
        return unicode_decode_utf8(s, size,
                                   fs_codec->error_handler,
                                   fs_codec->errors,
                                   NULL);
    }
#ifndef _Py_FORCE_UTF8_FS_ENCODING
    else if (fs_codec->encoding) {
        return PyUnicode_Decode(s, size,
                                fs_codec->encoding,
                                fs_codec->errors);
    }
#endif
    else {
        /* Before _PyUnicode_InitEncodings() is called, the Python codec
           machinery is not ready and so cannot be used:
           use mbstowcs() in this case. */
        const PyConfig *config = _PyInterpreterState_GetConfig(interp);
        const wchar_t *filesystem_errors = config->filesystem_errors;
        assert(filesystem_errors != NULL);
        _Py_error_handler errors = get_error_handler_wide(filesystem_errors);
        assert(errors != _Py_ERROR_UNKNOWN);
#ifdef _Py_FORCE_UTF8_FS_ENCODING
        return unicode_decode_utf8(s, size, errors, NULL, NULL);
#else
        return unicode_decode_locale(s, size, errors, 0);
#endif
    }
}


int
PyUnicode_FSConverter(PyObject* arg, void* addr)
{
    PyObject *path = NULL;
    PyObject *output = NULL;
    Py_ssize_t size;
    const char *data;
    if (arg == NULL) {
        Py_DECREF(*(PyObject**)addr);
        *(PyObject**)addr = NULL;
        return 1;
    }
    path = PyOS_FSPath(arg);
    if (path == NULL) {
        return 0;
    }
    if (PyBytes_Check(path)) {
        output = path;
    }
    else {  // PyOS_FSPath() guarantees its returned value is bytes or str.
        output = PyUnicode_EncodeFSDefault(path);
        Py_DECREF(path);
        if (!output) {
            return 0;
        }
        assert(PyBytes_Check(output));
    }

    size = PyBytes_GET_SIZE(output);
    data = PyBytes_AS_STRING(output);
    if ((size_t)size != strlen(data)) {
        PyErr_SetString(PyExc_ValueError, "embedded null byte");
        Py_DECREF(output);
        return 0;
    }
    *(PyObject**)addr = output;
    return Py_CLEANUP_SUPPORTED;
}


int
PyUnicode_FSDecoder(PyObject* arg, void* addr)
{
    int is_buffer = 0;
    PyObject *path = NULL;
    PyObject *output = NULL;
    if (arg == NULL) {
        Py_DECREF(*(PyObject**)addr);
        *(PyObject**)addr = NULL;
        return 1;
    }

    is_buffer = PyObject_CheckBuffer(arg);
    if (!is_buffer) {
        path = PyOS_FSPath(arg);
        if (path == NULL) {
            return 0;
        }
    }
    else {
        path = arg;
        Py_INCREF(arg);
    }

    if (PyUnicode_Check(path)) {
        output = path;
    }
    else if (PyBytes_Check(path) || is_buffer) {
        PyObject *path_bytes = NULL;

        if (!PyBytes_Check(path) &&
            PyErr_WarnFormat(PyExc_DeprecationWarning, 1,
            "path should be string, bytes, or os.PathLike, not %.200s",
            Py_TYPE(arg)->tp_name)) {
                Py_DECREF(path);
            return 0;
        }
        path_bytes = PyBytes_FromObject(path);
        Py_DECREF(path);
        if (!path_bytes) {
            return 0;
        }
        output = PyUnicode_DecodeFSDefaultAndSize(PyBytes_AS_STRING(path_bytes),
                                                  PyBytes_GET_SIZE(path_bytes));
        Py_DECREF(path_bytes);
        if (!output) {
            return 0;
        }
    }
    else {
        PyErr_Format(PyExc_TypeError,
                     "path should be string, bytes, or os.PathLike, not %.200s",
                     Py_TYPE(arg)->tp_name);
        Py_DECREF(path);
        return 0;
    }
    if (PyUnicode_READY(output) == -1) {
        Py_DECREF(output);
        return 0;
    }
    if (findchar(PyUnicode_DATA(output), PyUnicode_KIND(output),
                 PyUnicode_GET_LENGTH(output), 0, 1) >= 0) {
        PyErr_SetString(PyExc_ValueError, "embedded null character");
        Py_DECREF(output);
        return 0;
    }
    *(PyObject**)addr = output;
    return Py_CLEANUP_SUPPORTED;
}


static int unicode_fill_utf8(PyObject *unicode);

const char *
PyUnicode_AsUTF8AndSize(PyObject *unicode, Py_ssize_t *psize)
{
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return NULL;
    }
    if (PyUnicode_READY(unicode) == -1)
        return NULL;

    if (PyUnicode_UTF8(unicode) == NULL) {
        if (unicode_fill_utf8(unicode) == -1) {
            return NULL;
        }
    }

    if (psize)
        *psize = PyUnicode_UTF8_LENGTH(unicode);
    return PyUnicode_UTF8(unicode);
}

const char *
PyUnicode_AsUTF8(PyObject *unicode)
{
    return PyUnicode_AsUTF8AndSize(unicode, NULL);
}

Py_UNICODE *
PyUnicode_AsUnicodeAndSize(PyObject *unicode, Py_ssize_t *size)
{
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return NULL;
    }
    Py_UNICODE *w = _PyUnicode_WSTR(unicode);
    if (w == NULL) {
        /* Non-ASCII compact unicode object */
        assert(_PyUnicode_KIND(unicode) != PyUnicode_WCHAR_KIND);
        assert(PyUnicode_IS_READY(unicode));

        Py_ssize_t wlen = unicode_get_widechar_size(unicode);
        if ((size_t)wlen > PY_SSIZE_T_MAX / sizeof(wchar_t) - 1) {
            PyErr_NoMemory();
            return NULL;
        }
        w = (wchar_t *) PyObject_Malloc(sizeof(wchar_t) * (wlen + 1));
        if (w == NULL) {
            PyErr_NoMemory();
            return NULL;
        }
        unicode_copy_as_widechar(unicode, w, wlen + 1);
        _PyUnicode_WSTR(unicode) = w;
        if (!PyUnicode_IS_COMPACT_ASCII(unicode)) {
            _PyUnicode_WSTR_LENGTH(unicode) = wlen;
        }
    }
    if (size != NULL)
        *size = PyUnicode_WSTR_LENGTH(unicode);
    return w;
}

/* Deprecated APIs */

_Py_COMP_DIAG_PUSH
_Py_COMP_DIAG_IGNORE_DEPR_DECLS

Py_UNICODE *
PyUnicode_AsUnicode(PyObject *unicode)
{
    return PyUnicode_AsUnicodeAndSize(unicode, NULL);
}

const Py_UNICODE *
_PyUnicode_AsUnicode(PyObject *unicode)
{
    Py_ssize_t size;
    const Py_UNICODE *wstr;

    wstr = PyUnicode_AsUnicodeAndSize(unicode, &size);
    if (wstr && wcslen(wstr) != (size_t)size) {
        PyErr_SetString(PyExc_ValueError, "embedded null character");
        return NULL;
    }
    return wstr;
}


Py_ssize_t
PyUnicode_GetSize(PyObject *unicode)
{
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        goto onError;
    }
    if (_PyUnicode_WSTR(unicode) == NULL) {
        if (PyUnicode_AsUnicode(unicode) == NULL)
            goto onError;
    }
    return PyUnicode_WSTR_LENGTH(unicode);

  onError:
    return -1;
}

_Py_COMP_DIAG_POP

Py_ssize_t
PyUnicode_GetLength(PyObject *unicode)
{
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return -1;
    }
    if (PyUnicode_READY(unicode) == -1)
        return -1;
    return PyUnicode_GET_LENGTH(unicode);
}

Py_UCS4
PyUnicode_ReadChar(PyObject *unicode, Py_ssize_t index)
{
    const void *data;
    int kind;

    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return (Py_UCS4)-1;
    }
    if (PyUnicode_READY(unicode) == -1) {
        return (Py_UCS4)-1;
    }
    if (index < 0 || index >= PyUnicode_GET_LENGTH(unicode)) {
        PyErr_SetString(PyExc_IndexError, "string index out of range");
        return (Py_UCS4)-1;
    }
    data = PyUnicode_DATA(unicode);
    kind = PyUnicode_KIND(unicode);
    return PyUnicode_READ(kind, data, index);
}

int
PyUnicode_WriteChar(PyObject *unicode, Py_ssize_t index, Py_UCS4 ch)
{
    if (!PyUnicode_Check(unicode) || !PyUnicode_IS_COMPACT(unicode)) {
        PyErr_BadArgument();
        return -1;
    }
    assert(PyUnicode_IS_READY(unicode));
    if (index < 0 || index >= PyUnicode_GET_LENGTH(unicode)) {
        PyErr_SetString(PyExc_IndexError, "string index out of range");
        return -1;
    }
    if (unicode_check_modifiable(unicode))
        return -1;
    if (ch > PyUnicode_MAX_CHAR_VALUE(unicode)) {
        PyErr_SetString(PyExc_ValueError, "character out of range");
        return -1;
    }
    PyUnicode_WRITE(PyUnicode_KIND(unicode), PyUnicode_DATA(unicode),
                    index, ch);
    return 0;
}

const char *
PyUnicode_GetDefaultEncoding(void)
{
    return "utf-8";
}

/* create or adjust a UnicodeDecodeError */
static void
make_decode_exception(PyObject **exceptionObject,
                      const char *encoding,
                      const char *input, Py_ssize_t length,
                      Py_ssize_t startpos, Py_ssize_t endpos,
                      const char *reason)
{
    if (*exceptionObject == NULL) {
        *exceptionObject = PyUnicodeDecodeError_Create(
            encoding, input, length, startpos, endpos, reason);
    }
    else {
        if (PyUnicodeDecodeError_SetStart(*exceptionObject, startpos))
            goto onError;
        if (PyUnicodeDecodeError_SetEnd(*exceptionObject, endpos))
            goto onError;
        if (PyUnicodeDecodeError_SetReason(*exceptionObject, reason))
            goto onError;
    }
    return;

onError:
    Py_CLEAR(*exceptionObject);
}

#ifdef MS_WINDOWS
static int
widechar_resize(wchar_t **buf, Py_ssize_t *size, Py_ssize_t newsize)
{
    if (newsize > *size) {
        wchar_t *newbuf = *buf;
        if (PyMem_Resize(newbuf, wchar_t, newsize) == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        *buf = newbuf;
    }
    *size = newsize;
    return 0;
}

/* error handling callback helper:
   build arguments, call the callback and check the arguments,
   if no exception occurred, copy the replacement to the output
   and adjust various state variables.
   return 0 on success, -1 on error
*/

static int
unicode_decode_call_errorhandler_wchar(
    const char *errors, PyObject **errorHandler,
    const char *encoding, const char *reason,
    const char **input, const char **inend, Py_ssize_t *startinpos,
    Py_ssize_t *endinpos, PyObject **exceptionObject, const char **inptr,
    wchar_t **buf, Py_ssize_t *bufsize, Py_ssize_t *outpos)
{
    static const char *argparse = "Un;decoding error handler must return (str, int) tuple";

    PyObject *restuple = NULL;
    PyObject *repunicode = NULL;
    Py_ssize_t outsize;
    Py_ssize_t insize;
    Py_ssize_t requiredsize;
    Py_ssize_t newpos;
    PyObject *inputobj = NULL;
    Py_ssize_t repwlen;

    if (*errorHandler == NULL) {
        *errorHandler = PyCodec_LookupError(errors);
        if (*errorHandler == NULL)
            goto onError;
    }

    make_decode_exception(exceptionObject,
        encoding,
        *input, *inend - *input,
        *startinpos, *endinpos,
        reason);
    if (*exceptionObject == NULL)
        goto onError;

    restuple = PyObject_CallOneArg(*errorHandler, *exceptionObject);
    if (restuple == NULL)
        goto onError;
    if (!PyTuple_Check(restuple)) {
        PyErr_SetString(PyExc_TypeError, &argparse[3]);
        goto onError;
    }
    if (!PyArg_ParseTuple(restuple, argparse, &repunicode, &newpos))
        goto onError;

    /* Copy back the bytes variables, which might have been modified by the
       callback */
    inputobj = PyUnicodeDecodeError_GetObject(*exceptionObject);
    if (!inputobj)
        goto onError;
    *input = PyBytes_AS_STRING(inputobj);
    insize = PyBytes_GET_SIZE(inputobj);
    *inend = *input + insize;
    /* we can DECREF safely, as the exception has another reference,
       so the object won't go away. */
    Py_DECREF(inputobj);

    if (newpos<0)
        newpos = insize+newpos;
    if (newpos<0 || newpos>insize) {
        PyErr_Format(PyExc_IndexError, "position %zd from error handler out of bounds", newpos);
        goto onError;
    }

#if USE_UNICODE_WCHAR_CACHE
_Py_COMP_DIAG_PUSH
_Py_COMP_DIAG_IGNORE_DEPR_DECLS
    repwlen = PyUnicode_GetSize(repunicode);
    if (repwlen < 0)
        goto onError;
_Py_COMP_DIAG_POP
#else /* USE_UNICODE_WCHAR_CACHE */
    repwlen = PyUnicode_AsWideChar(repunicode, NULL, 0);
    if (repwlen < 0)
        goto onError;
    repwlen--;
#endif /* USE_UNICODE_WCHAR_CACHE */
    /* need more space? (at least enough for what we
       have+the replacement+the rest of the string (starting
       at the new input position), so we won't have to check space
       when there are no errors in the rest of the string) */
    requiredsize = *outpos;
    if (requiredsize > PY_SSIZE_T_MAX - repwlen)
        goto overflow;
    requiredsize += repwlen;
    if (requiredsize > PY_SSIZE_T_MAX - (insize - newpos))
        goto overflow;
    requiredsize += insize - newpos;
    outsize = *bufsize;
    if (requiredsize > outsize) {
        if (outsize <= PY_SSIZE_T_MAX/2 && requiredsize < 2*outsize)
            requiredsize = 2*outsize;
        if (widechar_resize(buf, bufsize, requiredsize) < 0) {
            goto onError;
        }
    }
    PyUnicode_AsWideChar(repunicode, *buf + *outpos, repwlen);
    *outpos += repwlen;
    *endinpos = newpos;
    *inptr = *input + newpos;

    /* we made it! */
    Py_DECREF(restuple);
    return 0;

  overflow:
    PyErr_SetString(PyExc_OverflowError,
                    "decoded result is too long for a Python string");

  onError:
    Py_XDECREF(restuple);
    return -1;
}
#endif   /* MS_WINDOWS */

static int
unicode_decode_call_errorhandler_writer(
    const char *errors, PyObject **errorHandler,
    const char *encoding, const char *reason,
    const char **input, const char **inend, Py_ssize_t *startinpos,
    Py_ssize_t *endinpos, PyObject **exceptionObject, const char **inptr,
    _PyUnicodeWriter *writer /* PyObject **output, Py_ssize_t *outpos */)
{
    static const char *argparse = "Un;decoding error handler must return (str, int) tuple";

    PyObject *restuple = NULL;
    PyObject *repunicode = NULL;
    Py_ssize_t insize;
    Py_ssize_t newpos;
    Py_ssize_t replen;
    Py_ssize_t remain;
    PyObject *inputobj = NULL;
    int need_to_grow = 0;
    const char *new_inptr;

    if (*errorHandler == NULL) {
        *errorHandler = PyCodec_LookupError(errors);
        if (*errorHandler == NULL)
            goto onError;
    }

    make_decode_exception(exceptionObject,
        encoding,
        *input, *inend - *input,
        *startinpos, *endinpos,
        reason);
    if (*exceptionObject == NULL)
        goto onError;

    restuple = PyObject_CallOneArg(*errorHandler, *exceptionObject);
    if (restuple == NULL)
        goto onError;
    if (!PyTuple_Check(restuple)) {
        PyErr_SetString(PyExc_TypeError, &argparse[3]);
        goto onError;
    }
    if (!PyArg_ParseTuple(restuple, argparse, &repunicode, &newpos))
        goto onError;

    /* Copy back the bytes variables, which might have been modified by the
       callback */
    inputobj = PyUnicodeDecodeError_GetObject(*exceptionObject);
    if (!inputobj)
        goto onError;
    remain = *inend - *input - *endinpos;
    *input = PyBytes_AS_STRING(inputobj);
    insize = PyBytes_GET_SIZE(inputobj);
    *inend = *input + insize;
    /* we can DECREF safely, as the exception has another reference,
       so the object won't go away. */
    Py_DECREF(inputobj);

    if (newpos<0)
        newpos = insize+newpos;
    if (newpos<0 || newpos>insize) {
        PyErr_Format(PyExc_IndexError, "position %zd from error handler out of bounds", newpos);
        goto onError;
    }

    replen = PyUnicode_GET_LENGTH(repunicode);
    if (replen > 1) {
        writer->min_length += replen - 1;
        need_to_grow = 1;
    }
    new_inptr = *input + newpos;
    if (*inend - new_inptr > remain) {
        /* We don't know the decoding algorithm here so we make the worst
           assumption that one byte decodes to one unicode character.
           If unfortunately one byte could decode to more unicode characters,
           the decoder may write out-of-bound then.  Is it possible for the
           algorithms using this function? */
        writer->min_length += *inend - new_inptr - remain;
        need_to_grow = 1;
    }
    if (need_to_grow) {
        writer->overallocate = 1;
        if (_PyUnicodeWriter_Prepare(writer, writer->min_length - writer->pos,
                            PyUnicode_MAX_CHAR_VALUE(repunicode)) == -1)
            goto onError;
    }
    if (_PyUnicodeWriter_WriteStr(writer, repunicode) == -1)
        goto onError;

    *endinpos = newpos;
    *inptr = new_inptr;

    /* we made it! */
    Py_DECREF(restuple);
    return 0;

  onError:
    Py_XDECREF(restuple);
    return -1;
}

/* --- UTF-7 Codec -------------------------------------------------------- */

/* See RFC2152 for details.  We encode conservatively and decode liberally. */

/* Three simple macros defining base-64. */

/* Is c a base-64 character? */

#define IS_BASE64(c) \
    (((c) >= 'A' && (c) <= 'Z') ||     \
     ((c) >= 'a' && (c) <= 'z') ||     \
     ((c) >= '0' && (c) <= '9') ||     \
     (c) == '+' || (c) == '/')

/* given that c is a base-64 character, what is its base-64 value? */

#define FROM_BASE64(c)                                                  \
    (((c) >= 'A' && (c) <= 'Z') ? (c) - 'A' :                           \
     ((c) >= 'a' && (c) <= 'z') ? (c) - 'a' + 26 :                      \
     ((c) >= '0' && (c) <= '9') ? (c) - '0' + 52 :                      \
     (c) == '+' ? 62 : 63)

/* What is the base-64 character of the bottom 6 bits of n? */

#define TO_BASE64(n)  \
    ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[(n) & 0x3f])

/* DECODE_DIRECT: this byte encountered in a UTF-7 string should be
 * decoded as itself.  We are permissive on decoding; the only ASCII
 * byte not decoding to itself is the + which begins a base64
 * string. */

#define DECODE_DIRECT(c)                                \
    ((c) <= 127 && (c) != '+')

/* The UTF-7 encoder treats ASCII characters differently according to
 * whether they are Set D, Set O, Whitespace, or special (i.e. none of
 * the above).  See RFC2152.  This array identifies these different
 * sets:
 * 0 : "Set D"
 *     alphanumeric and '(),-./:?
 * 1 : "Set O"
 *     !"#$%&*;<=>@[]^_`{|}
 * 2 : "whitespace"
 *     ht nl cr sp
 * 3 : special (must be base64 encoded)
 *     everything else (i.e. +\~ and non-printing codes 0-8 11-12 14-31 127)
 */

static
char utf7_category[128] = {
/* nul soh stx etx eot enq ack bel bs  ht  nl  vt  np  cr  so  si  */
    3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  2,  3,  3,  2,  3,  3,
/* dle dc1 dc2 dc3 dc4 nak syn etb can em  sub esc fs  gs  rs  us  */
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
/* sp   !   "   #   $   %   &   '   (   )   *   +   ,   -   .   /  */
    2,  1,  1,  1,  1,  1,  1,  0,  0,  0,  1,  3,  0,  0,  0,  0,
/*  0   1   2   3   4   5   6   7   8   9   :   ;   <   =   >   ?  */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  0,
/*  @   A   B   C   D   E   F   G   H   I   J   K   L   M   N   O  */
    1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
/*  P   Q   R   S   T   U   V   W   X   Y   Z   [   \   ]   ^   _  */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  3,  1,  1,  1,
/*  `   a   b   c   d   e   f   g   h   i   j   k   l   m   n   o  */
    1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
/*  p   q   r   s   t   u   v   w   x   y   z   {   |   }   ~  del */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  3,  3,
};

/* ENCODE_DIRECT: this character should be encoded as itself.  The
 * answer depends on whether we are encoding set O as itself, and also
 * on whether we are encoding whitespace as itself.  RFC2152 makes it
 * clear that the answers to these questions vary between
 * applications, so this code needs to be flexible.  */

#define ENCODE_DIRECT(c, directO, directWS)             \
    ((c) < 128 && (c) > 0 &&                            \
     ((utf7_category[(c)] == 0) ||                      \
      (directWS && (utf7_category[(c)] == 2)) ||        \
      (directO && (utf7_category[(c)] == 1))))

PyObject *
PyUnicode_DecodeUTF7(const char *s,
                     Py_ssize_t size,
                     const char *errors)
{
    return PyUnicode_DecodeUTF7Stateful(s, size, errors, NULL);
}

/* The decoder.  The only state we preserve is our read position,
 * i.e. how many characters we have consumed.  So if we end in the
 * middle of a shift sequence we have to back off the read position
 * and the output to the beginning of the sequence, otherwise we lose
 * all the shift state (seen bits, number of bits seen, high
 * surrogate). */

PyObject *
PyUnicode_DecodeUTF7Stateful(const char *s,
                             Py_ssize_t size,
                             const char *errors,
                             Py_ssize_t *consumed)
{
    const char *starts = s;
    Py_ssize_t startinpos;
    Py_ssize_t endinpos;
    const char *e;
    _PyUnicodeWriter writer;
    const char *errmsg = "";
    int inShift = 0;
    Py_ssize_t shiftOutStart;
    unsigned int base64bits = 0;
    unsigned long base64buffer = 0;
    Py_UCS4 surrogate = 0;
    PyObject *errorHandler = NULL;
    PyObject *exc = NULL;

    if (size == 0) {
        if (consumed)
            *consumed = 0;
        _Py_RETURN_UNICODE_EMPTY();
    }

    /* Start off assuming it's all ASCII. Widen later as necessary. */
    _PyUnicodeWriter_Init(&writer);
    writer.min_length = size;

    shiftOutStart = 0;
    e = s + size;

    while (s < e) {
        Py_UCS4 ch;
      restart:
        ch = (unsigned char) *s;

        if (inShift) { /* in a base-64 section */
            if (IS_BASE64(ch)) { /* consume a base-64 character */
                base64buffer = (base64buffer << 6) | FROM_BASE64(ch);
                base64bits += 6;
                s++;
                if (base64bits >= 16) {
                    /* we have enough bits for a UTF-16 value */
                    Py_UCS4 outCh = (Py_UCS4)(base64buffer >> (base64bits-16));
                    base64bits -= 16;
                    base64buffer &= (1 << base64bits) - 1; /* clear high bits */
                    assert(outCh <= 0xffff);
                    if (surrogate) {
                        /* expecting a second surrogate */
                        if (Py_UNICODE_IS_LOW_SURROGATE(outCh)) {
                            Py_UCS4 ch2 = Py_UNICODE_JOIN_SURROGATES(surrogate, outCh);
                            if (_PyUnicodeWriter_WriteCharInline(&writer, ch2) < 0)
                                goto onError;
                            surrogate = 0;
                            continue;
                        }
                        else {
                            if (_PyUnicodeWriter_WriteCharInline(&writer, surrogate) < 0)
                                goto onError;
                            surrogate = 0;
                        }
                    }
                    if (Py_UNICODE_IS_HIGH_SURROGATE(outCh)) {
                        /* first surrogate */
                        surrogate = outCh;
                    }
                    else {
                        if (_PyUnicodeWriter_WriteCharInline(&writer, outCh) < 0)
                            goto onError;
                    }
                }
            }
            else { /* now leaving a base-64 section */
                inShift = 0;
                if (base64bits > 0) { /* left-over bits */
                    if (base64bits >= 6) {
                        /* We've seen at least one base-64 character */
                        s++;
                        errmsg = "partial character in shift sequence";
                        goto utf7Error;
                    }
                    else {
                        /* Some bits remain; they should be zero */
                        if (base64buffer != 0) {
                            s++;
                            errmsg = "non-zero padding bits in shift sequence";
                            goto utf7Error;
                        }
                    }
                }
                if (surrogate && DECODE_DIRECT(ch)) {
                    if (_PyUnicodeWriter_WriteCharInline(&writer, surrogate) < 0)
                        goto onError;
                }
                surrogate = 0;
                if (ch == '-') {
                    /* '-' is absorbed; other terminating
                       characters are preserved */
                    s++;
                }
            }
        }
        else if ( ch == '+' ) {
            startinpos = s-starts;
            s++; /* consume '+' */
            if (s < e && *s == '-') { /* '+-' encodes '+' */
                s++;
                if (_PyUnicodeWriter_WriteCharInline(&writer, '+') < 0)
                    goto onError;
            }
            else if (s < e && !IS_BASE64(*s)) {
                s++;
                errmsg = "ill-formed sequence";
                goto utf7Error;
            }
            else { /* begin base64-encoded section */
                inShift = 1;
                surrogate = 0;
                shiftOutStart = writer.pos;
                base64bits = 0;
                base64buffer = 0;
            }
        }
        else if (DECODE_DIRECT(ch)) { /* character decodes as itself */
            s++;
            if (_PyUnicodeWriter_WriteCharInline(&writer, ch) < 0)
                goto onError;
        }
        else {
            startinpos = s-starts;
            s++;
            errmsg = "unexpected special character";
            goto utf7Error;
        }
        continue;
utf7Error:
        endinpos = s-starts;
        if (unicode_decode_call_errorhandler_writer(
                errors, &errorHandler,
                "utf7", errmsg,
                &starts, &e, &startinpos, &endinpos, &exc, &s,
                &writer))
            goto onError;
    }

    /* end of string */

    if (inShift && !consumed) { /* in shift sequence, no more to follow */
        /* if we're in an inconsistent state, that's an error */
        inShift = 0;
        if (surrogate ||
                (base64bits >= 6) ||
                (base64bits > 0 && base64buffer != 0)) {
            endinpos = size;
            if (unicode_decode_call_errorhandler_writer(
                    errors, &errorHandler,
                    "utf7", "unterminated shift sequence",
                    &starts, &e, &startinpos, &endinpos, &exc, &s,
                    &writer))
                goto onError;
            if (s < e)
                goto restart;
        }
    }

    /* return state */
    if (consumed) {
        if (inShift) {
            *consumed = startinpos;
            if (writer.pos != shiftOutStart && writer.maxchar > 127) {
                PyObject *result = PyUnicode_FromKindAndData(
                        writer.kind, writer.data, shiftOutStart);
                Py_XDECREF(errorHandler);
                Py_XDECREF(exc);
                _PyUnicodeWriter_Dealloc(&writer);
                return result;
            }
            writer.pos = shiftOutStart; /* back off output */
        }
        else {
            *consumed = s-starts;
        }
    }

    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    return _PyUnicodeWriter_Finish(&writer);

  onError:
    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    _PyUnicodeWriter_Dealloc(&writer);
    return NULL;
}


PyObject *
_PyUnicode_EncodeUTF7(PyObject *str,
                      int base64SetO,
                      int base64WhiteSpace,
                      const char *errors)
{
    int kind;
    const void *data;
    Py_ssize_t len;
    PyObject *v;
    int inShift = 0;
    Py_ssize_t i;
    unsigned int base64bits = 0;
    unsigned long base64buffer = 0;
    char * out;
    const char * start;

    if (PyUnicode_READY(str) == -1)
        return NULL;
    kind = PyUnicode_KIND(str);
    data = PyUnicode_DATA(str);
    len = PyUnicode_GET_LENGTH(str);

    if (len == 0)
        return PyBytes_FromStringAndSize(NULL, 0);

    /* It might be possible to tighten this worst case */
    if (len > PY_SSIZE_T_MAX / 8)
        return PyErr_NoMemory();
    v = PyBytes_FromStringAndSize(NULL, len * 8);
    if (v == NULL)
        return NULL;

    start = out = PyBytes_AS_STRING(v);
    for (i = 0; i < len; ++i) {
        Py_UCS4 ch = PyUnicode_READ(kind, data, i);

        if (inShift) {
            if (ENCODE_DIRECT(ch, !base64SetO, !base64WhiteSpace)) {
                /* shifting out */
                if (base64bits) { /* output remaining bits */
                    *out++ = TO_BASE64(base64buffer << (6-base64bits));
                    base64buffer = 0;
                    base64bits = 0;
                }
                inShift = 0;
                /* Characters not in the BASE64 set implicitly unshift the sequence
                   so no '-' is required, except if the character is itself a '-' */
                if (IS_BASE64(ch) || ch == '-') {
                    *out++ = '-';
                }
                *out++ = (char) ch;
            }
            else {
                goto encode_char;
            }
        }
        else { /* not in a shift sequence */
            if (ch == '+') {
                *out++ = '+';
                        *out++ = '-';
            }
            else if (ENCODE_DIRECT(ch, !base64SetO, !base64WhiteSpace)) {
                *out++ = (char) ch;
            }
            else {
                *out++ = '+';
                inShift = 1;
                goto encode_char;
            }
        }
        continue;
encode_char:
        if (ch >= 0x10000) {
            assert(ch <= MAX_UNICODE);

            /* code first surrogate */
            base64bits += 16;
            base64buffer = (base64buffer << 16) | Py_UNICODE_HIGH_SURROGATE(ch);
            while (base64bits >= 6) {
                *out++ = TO_BASE64(base64buffer >> (base64bits-6));
                base64bits -= 6;
            }
            /* prepare second surrogate */
            ch = Py_UNICODE_LOW_SURROGATE(ch);
        }
        base64bits += 16;
        base64buffer = (base64buffer << 16) | ch;
        while (base64bits >= 6) {
            *out++ = TO_BASE64(base64buffer >> (base64bits-6));
            base64bits -= 6;
        }
    }
    if (base64bits)
        *out++= TO_BASE64(base64buffer << (6-base64bits) );
    if (inShift)
        *out++ = '-';
    if (_PyBytes_Resize(&v, out - start) < 0)
        return NULL;
    return v;
}
PyObject *
PyUnicode_EncodeUTF7(const Py_UNICODE *s,
                     Py_ssize_t size,
                     int base64SetO,
                     int base64WhiteSpace,
                     const char *errors)
{
    PyObject *result;
    PyObject *tmp = PyUnicode_FromWideChar(s, size);
    if (tmp == NULL)
        return NULL;
    result = _PyUnicode_EncodeUTF7(tmp, base64SetO,
                                   base64WhiteSpace, errors);
    Py_DECREF(tmp);
    return result;
}

#undef IS_BASE64
#undef FROM_BASE64
#undef TO_BASE64
#undef DECODE_DIRECT
#undef ENCODE_DIRECT

/* --- UTF-8 Codec -------------------------------------------------------- */

PyObject *
PyUnicode_DecodeUTF8(const char *s,
                     Py_ssize_t size,
                     const char *errors)
{
    return PyUnicode_DecodeUTF8Stateful(s, size, errors, NULL);
}

#include "stringlib/asciilib.h"
#include "stringlib/codecs.h"
#include "stringlib/undef.h"

#include "stringlib/ucs1lib.h"
#include "stringlib/codecs.h"
#include "stringlib/undef.h"

#include "stringlib/ucs2lib.h"
#include "stringlib/codecs.h"
#include "stringlib/undef.h"

#include "stringlib/ucs4lib.h"
#include "stringlib/codecs.h"
#include "stringlib/undef.h"

/* Mask to quickly check whether a C 'size_t' contains a
   non-ASCII, UTF8-encoded char. */
#if (SIZEOF_SIZE_T == 8)
# define ASCII_CHAR_MASK 0x8080808080808080ULL
#elif (SIZEOF_SIZE_T == 4)
# define ASCII_CHAR_MASK 0x80808080U
#else
# error C 'size_t' size should be either 4 or 8!
#endif

static Py_ssize_t
ascii_decode(const char *start, const char *end, Py_UCS1 *dest)
{
    const char *p = start;

#if SIZEOF_SIZE_T <= SIZEOF_VOID_P
    assert(_Py_IS_ALIGNED(dest, ALIGNOF_SIZE_T));
    if (_Py_IS_ALIGNED(p, ALIGNOF_SIZE_T)) {
        /* Fast path, see in STRINGLIB(utf8_decode) for
           an explanation. */
        /* Help allocation */
        const char *_p = p;
        Py_UCS1 * q = dest;
        while (_p + SIZEOF_SIZE_T <= end) {
            size_t value = *(const size_t *) _p;
            if (value & ASCII_CHAR_MASK)
                break;
            *((size_t *)q) = value;
            _p += SIZEOF_SIZE_T;
            q += SIZEOF_SIZE_T;
        }
        p = _p;
        while (p < end) {
            if ((unsigned char)*p & 0x80)
                break;
            *q++ = *p++;
        }
        return p - start;
    }
#endif
    while (p < end) {
        /* Fast path, see in STRINGLIB(utf8_decode) in stringlib/codecs.h
           for an explanation. */
        if (_Py_IS_ALIGNED(p, ALIGNOF_SIZE_T)) {
            /* Help allocation */
            const char *_p = p;
            while (_p + SIZEOF_SIZE_T <= end) {
                size_t value = *(const size_t *) _p;
                if (value & ASCII_CHAR_MASK)
                    break;
                _p += SIZEOF_SIZE_T;
            }
            p = _p;
            if (_p == end)
                break;
        }
        if ((unsigned char)*p & 0x80)
            break;
        ++p;
    }
    memcpy(dest, start, p - start);
    return p - start;
}

static PyObject *
unicode_decode_utf8(const char *s, Py_ssize_t size,
                    _Py_error_handler error_handler, const char *errors,
                    Py_ssize_t *consumed)
{
    if (size == 0) {
        if (consumed)
            *consumed = 0;
        _Py_RETURN_UNICODE_EMPTY();
    }

    /* ASCII is equivalent to the first 128 ordinals in Unicode. */
    if (size == 1 && (unsigned char)s[0] < 128) {
        if (consumed) {
            *consumed = 1;
        }
        return get_latin1_char((unsigned char)s[0]);
    }

    const char *starts = s;
    const char *end = s + size;

    // fast path: try ASCII string.
    PyObject *u = PyUnicode_New(size, 127);
    if (u == NULL) {
        return NULL;
    }
    s += ascii_decode(s, end, PyUnicode_1BYTE_DATA(u));
    if (s == end) {
        return u;
    }

    // Use _PyUnicodeWriter after fast path is failed.
    _PyUnicodeWriter writer;
    _PyUnicodeWriter_InitWithBuffer(&writer, u);
    writer.pos = s - starts;

    Py_ssize_t startinpos, endinpos;
    const char *errmsg = "";
    PyObject *error_handler_obj = NULL;
    PyObject *exc = NULL;

    while (s < end) {
        Py_UCS4 ch;
        int kind = writer.kind;

        if (kind == PyUnicode_1BYTE_KIND) {
            if (PyUnicode_IS_ASCII(writer.buffer))
                ch = asciilib_utf8_decode(&s, end, writer.data, &writer.pos);
            else
                ch = ucs1lib_utf8_decode(&s, end, writer.data, &writer.pos);
        } else if (kind == PyUnicode_2BYTE_KIND) {
            ch = ucs2lib_utf8_decode(&s, end, writer.data, &writer.pos);
        } else {
            assert(kind == PyUnicode_4BYTE_KIND);
            ch = ucs4lib_utf8_decode(&s, end, writer.data, &writer.pos);
        }

        switch (ch) {
        case 0:
            if (s == end || consumed)
                goto End;
            errmsg = "unexpected end of data";
            startinpos = s - starts;
            endinpos = end - starts;
            break;
        case 1:
            errmsg = "invalid start byte";
            startinpos = s - starts;
            endinpos = startinpos + 1;
            break;
        case 2:
            if (consumed && (unsigned char)s[0] == 0xED && end - s == 2
                && (unsigned char)s[1] >= 0xA0 && (unsigned char)s[1] <= 0xBF)
            {
                /* Truncated surrogate code in range D800-DFFF */
                goto End;
            }
            /* fall through */
        case 3:
        case 4:
            errmsg = "invalid continuation byte";
            startinpos = s - starts;
            endinpos = startinpos + ch - 1;
            break;
        default:
            if (_PyUnicodeWriter_WriteCharInline(&writer, ch) < 0)
                goto onError;
            continue;
        }

        if (error_handler == _Py_ERROR_UNKNOWN)
            error_handler = _Py_GetErrorHandler(errors);

        switch (error_handler) {
        case _Py_ERROR_IGNORE:
            s += (endinpos - startinpos);
            break;

        case _Py_ERROR_REPLACE:
            if (_PyUnicodeWriter_WriteCharInline(&writer, 0xfffd) < 0)
                goto onError;
            s += (endinpos - startinpos);
            break;

        case _Py_ERROR_SURROGATEESCAPE:
        {
            Py_ssize_t i;

            if (_PyUnicodeWriter_PrepareKind(&writer, PyUnicode_2BYTE_KIND) < 0)
                goto onError;
            for (i=startinpos; i<endinpos; i++) {
                ch = (Py_UCS4)(unsigned char)(starts[i]);
                PyUnicode_WRITE(writer.kind, writer.data, writer.pos,
                                ch + 0xdc00);
                writer.pos++;
            }
            s += (endinpos - startinpos);
            break;
        }

        default:
            if (unicode_decode_call_errorhandler_writer(
                    errors, &error_handler_obj,
                    "utf-8", errmsg,
                    &starts, &end, &startinpos, &endinpos, &exc, &s,
                    &writer))
                goto onError;
        }
    }

End:
    if (consumed)
        *consumed = s - starts;

    Py_XDECREF(error_handler_obj);
    Py_XDECREF(exc);
    return _PyUnicodeWriter_Finish(&writer);

onError:
    Py_XDECREF(error_handler_obj);
    Py_XDECREF(exc);
    _PyUnicodeWriter_Dealloc(&writer);
    return NULL;
}


PyObject *
PyUnicode_DecodeUTF8Stateful(const char *s,
                             Py_ssize_t size,
                             const char *errors,
                             Py_ssize_t *consumed)
{
    return unicode_decode_utf8(s, size, _Py_ERROR_UNKNOWN, errors, consumed);
}


/* UTF-8 decoder: use surrogateescape error handler if 'surrogateescape' is
   non-zero, use strict error handler otherwise.

   On success, write a pointer to a newly allocated wide character string into
   *wstr (use PyMem_RawFree() to free the memory) and write the output length
   (in number of wchar_t units) into *wlen (if wlen is set).

   On memory allocation failure, return -1.

   On decoding error (if surrogateescape is zero), return -2. If wlen is
   non-NULL, write the start of the illegal byte sequence into *wlen. If reason
   is not NULL, write the decoding error message into *reason. */
int
_Py_DecodeUTF8Ex(const char *s, Py_ssize_t size, wchar_t **wstr, size_t *wlen,
                 const char **reason, _Py_error_handler errors)
{
    const char *orig_s = s;
    const char *e;
    wchar_t *unicode;
    Py_ssize_t outpos;

    int surrogateescape = 0;
    int surrogatepass = 0;
    switch (errors)
    {
    case _Py_ERROR_STRICT:
        break;
    case _Py_ERROR_SURROGATEESCAPE:
        surrogateescape = 1;
        break;
    case _Py_ERROR_SURROGATEPASS:
        surrogatepass = 1;
        break;
    default:
        return -3;
    }

    /* Note: size will always be longer than the resulting Unicode
       character count */
    if (PY_SSIZE_T_MAX / (Py_ssize_t)sizeof(wchar_t) - 1 < size) {
        return -1;
    }

    unicode = PyMem_RawMalloc((size + 1) * sizeof(wchar_t));
    if (!unicode) {
        return -1;
    }

    /* Unpack UTF-8 encoded data */
    e = s + size;
    outpos = 0;
    while (s < e) {
        Py_UCS4 ch;
#if SIZEOF_WCHAR_T == 4
        ch = ucs4lib_utf8_decode(&s, e, (Py_UCS4 *)unicode, &outpos);
#else
        ch = ucs2lib_utf8_decode(&s, e, (Py_UCS2 *)unicode, &outpos);
#endif
        if (ch > 0xFF) {
#if SIZEOF_WCHAR_T == 4
            Py_UNREACHABLE();
#else
            assert(ch > 0xFFFF && ch <= MAX_UNICODE);
            /* write a surrogate pair */
            unicode[outpos++] = (wchar_t)Py_UNICODE_HIGH_SURROGATE(ch);
            unicode[outpos++] = (wchar_t)Py_UNICODE_LOW_SURROGATE(ch);
#endif
        }
        else {
            if (!ch && s == e) {
                break;
            }

            if (surrogateescape) {
                unicode[outpos++] = 0xDC00 + (unsigned char)*s++;
            }
            else {
                /* Is it a valid three-byte code? */
                if (surrogatepass
                    && (e - s) >= 3
                    && (s[0] & 0xf0) == 0xe0
                    && (s[1] & 0xc0) == 0x80
                    && (s[2] & 0xc0) == 0x80)
                {
                    ch = ((s[0] & 0x0f) << 12) + ((s[1] & 0x3f) << 6) + (s[2] & 0x3f);
                    s += 3;
                    unicode[outpos++] = ch;
                }
                else {
                    PyMem_RawFree(unicode );
                    if (reason != NULL) {
                        switch (ch) {
                        case 0:
                            *reason = "unexpected end of data";
                            break;
                        case 1:
                            *reason = "invalid start byte";
                            break;
                        /* 2, 3, 4 */
                        default:
                            *reason = "invalid continuation byte";
                            break;
                        }
                    }
                    if (wlen != NULL) {
                        *wlen = s - orig_s;
                    }
                    return -2;
                }
            }
        }
    }
    unicode[outpos] = L'\0';
    if (wlen) {
        *wlen = outpos;
    }
    *wstr = unicode;
    return 0;
}


wchar_t*
_Py_DecodeUTF8_surrogateescape(const char *arg, Py_ssize_t arglen,
                               size_t *wlen)
{
    wchar_t *wstr;
    int res = _Py_DecodeUTF8Ex(arg, arglen,
                               &wstr, wlen,
                               NULL, _Py_ERROR_SURROGATEESCAPE);
    if (res != 0) {
        /* _Py_DecodeUTF8Ex() must support _Py_ERROR_SURROGATEESCAPE */
        assert(res != -3);
        if (wlen) {
            *wlen = (size_t)res;
        }
        return NULL;
    }
    return wstr;
}


/* UTF-8 encoder using the surrogateescape error handler .

   On success, return 0 and write the newly allocated character string (use
   PyMem_Free() to free the memory) into *str.

   On encoding failure, return -2 and write the position of the invalid
   surrogate character into *error_pos (if error_pos is set) and the decoding
   error message into *reason (if reason is set).

   On memory allocation failure, return -1. */
int
_Py_EncodeUTF8Ex(const wchar_t *text, char **str, size_t *error_pos,
                 const char **reason, int raw_malloc, _Py_error_handler errors)
{
    const Py_ssize_t max_char_size = 4;
    Py_ssize_t len = wcslen(text);

    assert(len >= 0);

    int surrogateescape = 0;
    int surrogatepass = 0;
    switch (errors)
    {
    case _Py_ERROR_STRICT:
        break;
    case _Py_ERROR_SURROGATEESCAPE:
        surrogateescape = 1;
        break;
    case _Py_ERROR_SURROGATEPASS:
        surrogatepass = 1;
        break;
    default:
        return -3;
    }

    if (len > PY_SSIZE_T_MAX / max_char_size - 1) {
        return -1;
    }
    char *bytes;
    if (raw_malloc) {
        bytes = PyMem_RawMalloc((len + 1) * max_char_size);
    }
    else {
        bytes = PyMem_Malloc((len + 1) * max_char_size);
    }
    if (bytes == NULL) {
        return -1;
    }

    char *p = bytes;
    Py_ssize_t i;
    for (i = 0; i < len; ) {
        Py_ssize_t ch_pos = i;
        Py_UCS4 ch = text[i];
        i++;
#if Py_UNICODE_SIZE == 2
        if (Py_UNICODE_IS_HIGH_SURROGATE(ch)
            && i < len
            && Py_UNICODE_IS_LOW_SURROGATE(text[i]))
        {
            ch = Py_UNICODE_JOIN_SURROGATES(ch, text[i]);
            i++;
        }
#endif

        if (ch < 0x80) {
            /* Encode ASCII */
            *p++ = (char) ch;

        }
        else if (ch < 0x0800) {
            /* Encode Latin-1 */
            *p++ = (char)(0xc0 | (ch >> 6));
            *p++ = (char)(0x80 | (ch & 0x3f));
        }
        else if (Py_UNICODE_IS_SURROGATE(ch) && !surrogatepass) {
            /* surrogateescape error handler */
            if (!surrogateescape || !(0xDC80 <= ch && ch <= 0xDCFF)) {
                if (error_pos != NULL) {
                    *error_pos = (size_t)ch_pos;
                }
                if (reason != NULL) {
                    *reason = "encoding error";
                }
                if (raw_malloc) {
                    PyMem_RawFree(bytes);
                }
                else {
                    PyMem_Free(bytes);
                }
                return -2;
            }
            *p++ = (char)(ch & 0xff);
        }
        else if (ch < 0x10000) {
            *p++ = (char)(0xe0 | (ch >> 12));
            *p++ = (char)(0x80 | ((ch >> 6) & 0x3f));
            *p++ = (char)(0x80 | (ch & 0x3f));
        }
        else {  /* ch >= 0x10000 */
            assert(ch <= MAX_UNICODE);
            /* Encode UCS4 Unicode ordinals */
            *p++ = (char)(0xf0 | (ch >> 18));
            *p++ = (char)(0x80 | ((ch >> 12) & 0x3f));
            *p++ = (char)(0x80 | ((ch >> 6) & 0x3f));
            *p++ = (char)(0x80 | (ch & 0x3f));
        }
    }
    *p++ = '\0';

    size_t final_size = (p - bytes);
    char *bytes2;
    if (raw_malloc) {
        bytes2 = PyMem_RawRealloc(bytes, final_size);
    }
    else {
        bytes2 = PyMem_Realloc(bytes, final_size);
    }
    if (bytes2 == NULL) {
        if (error_pos != NULL) {
            *error_pos = (size_t)-1;
        }
        if (raw_malloc) {
            PyMem_RawFree(bytes);
        }
        else {
            PyMem_Free(bytes);
        }
        return -1;
    }
    *str = bytes2;
    return 0;
}


/* Primary internal function which creates utf8 encoded bytes objects.

   Allocation strategy:  if the string is short, convert into a stack buffer
   and allocate exactly as much space needed at the end.  Else allocate the
   maximum possible needed (4 result bytes per Unicode character), and return
   the excess memory at the end.
*/
static PyObject *
unicode_encode_utf8(PyObject *unicode, _Py_error_handler error_handler,
                    const char *errors)
{
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return NULL;
    }

    if (PyUnicode_READY(unicode) == -1)
        return NULL;

    if (PyUnicode_UTF8(unicode))
        return PyBytes_FromStringAndSize(PyUnicode_UTF8(unicode),
                                         PyUnicode_UTF8_LENGTH(unicode));

    enum PyUnicode_Kind kind = PyUnicode_KIND(unicode);
    const void *data = PyUnicode_DATA(unicode);
    Py_ssize_t size = PyUnicode_GET_LENGTH(unicode);

    _PyBytesWriter writer;
    char *end;

    switch (kind) {
    default:
        Py_UNREACHABLE();
    case PyUnicode_1BYTE_KIND:
        /* the string cannot be ASCII, or PyUnicode_UTF8() would be set */
        assert(!PyUnicode_IS_ASCII(unicode));
        end = ucs1lib_utf8_encoder(&writer, unicode, data, size, error_handler, errors);
        break;
    case PyUnicode_2BYTE_KIND:
        end = ucs2lib_utf8_encoder(&writer, unicode, data, size, error_handler, errors);
        break;
    case PyUnicode_4BYTE_KIND:
        end = ucs4lib_utf8_encoder(&writer, unicode, data, size, error_handler, errors);
        break;
    }

    if (end == NULL) {
        _PyBytesWriter_Dealloc(&writer);
        return NULL;
    }
    return _PyBytesWriter_Finish(&writer, end);
}

static int
unicode_fill_utf8(PyObject *unicode)
{
    /* the string cannot be ASCII, or PyUnicode_UTF8() would be set */
    assert(!PyUnicode_IS_ASCII(unicode));

    enum PyUnicode_Kind kind = PyUnicode_KIND(unicode);
    const void *data = PyUnicode_DATA(unicode);
    Py_ssize_t size = PyUnicode_GET_LENGTH(unicode);

    _PyBytesWriter writer;
    char *end;

    switch (kind) {
    default:
        Py_UNREACHABLE();
    case PyUnicode_1BYTE_KIND:
        end = ucs1lib_utf8_encoder(&writer, unicode, data, size,
                                   _Py_ERROR_STRICT, NULL);
        break;
    case PyUnicode_2BYTE_KIND:
        end = ucs2lib_utf8_encoder(&writer, unicode, data, size,
                                   _Py_ERROR_STRICT, NULL);
        break;
    case PyUnicode_4BYTE_KIND:
        end = ucs4lib_utf8_encoder(&writer, unicode, data, size,
                                   _Py_ERROR_STRICT, NULL);
        break;
    }
    if (end == NULL) {
        _PyBytesWriter_Dealloc(&writer);
        return -1;
    }

    const char *start = writer.use_small_buffer ? writer.small_buffer :
                    PyBytes_AS_STRING(writer.buffer);
    Py_ssize_t len = end - start;

    char *cache = PyObject_Malloc(len + 1);
    if (cache == NULL) {
        _PyBytesWriter_Dealloc(&writer);
        PyErr_NoMemory();
        return -1;
    }
    _PyUnicode_UTF8(unicode) = cache;
    _PyUnicode_UTF8_LENGTH(unicode) = len;
    memcpy(cache, start, len);
    cache[len] = '\0';
    _PyBytesWriter_Dealloc(&writer);
    return 0;
}

PyObject *
_PyUnicode_AsUTF8String(PyObject *unicode, const char *errors)
{
    return unicode_encode_utf8(unicode, _Py_ERROR_UNKNOWN, errors);
}


PyObject *
PyUnicode_EncodeUTF8(const Py_UNICODE *s,
                     Py_ssize_t size,
                     const char *errors)
{
    PyObject *v, *unicode;

    unicode = PyUnicode_FromWideChar(s, size);
    if (unicode == NULL)
        return NULL;
    v = _PyUnicode_AsUTF8String(unicode, errors);
    Py_DECREF(unicode);
    return v;
}

PyObject *
PyUnicode_AsUTF8String(PyObject *unicode)
{
    return _PyUnicode_AsUTF8String(unicode, NULL);
}

/* --- UTF-32 Codec ------------------------------------------------------- */

PyObject *
PyUnicode_DecodeUTF32(const char *s,
                      Py_ssize_t size,
                      const char *errors,
                      int *byteorder)
{
    return PyUnicode_DecodeUTF32Stateful(s, size, errors, byteorder, NULL);
}

PyObject *
PyUnicode_DecodeUTF32Stateful(const char *s,
                              Py_ssize_t size,
                              const char *errors,
                              int *byteorder,
                              Py_ssize_t *consumed)
{
    const char *starts = s;
    Py_ssize_t startinpos;
    Py_ssize_t endinpos;
    _PyUnicodeWriter writer;
    const unsigned char *q, *e;
    int le, bo = 0;       /* assume native ordering by default */
    const char *encoding;
    const char *errmsg = "";
    PyObject *errorHandler = NULL;
    PyObject *exc = NULL;

    q = (const unsigned char *)s;
    e = q + size;

    if (byteorder)
        bo = *byteorder;

    /* Check for BOM marks (U+FEFF) in the input and adjust current
       byte order setting accordingly. In native mode, the leading BOM
       mark is skipped, in all other modes, it is copied to the output
       stream as-is (giving a ZWNBSP character). */
    if (bo == 0 && size >= 4) {
        Py_UCS4 bom = ((unsigned int)q[3] << 24) | (q[2] << 16) | (q[1] << 8) | q[0];
        if (bom == 0x0000FEFF) {
            bo = -1;
            q += 4;
        }
        else if (bom == 0xFFFE0000) {
            bo = 1;
            q += 4;
        }
        if (byteorder)
            *byteorder = bo;
    }

    if (q == e) {
        if (consumed)
            *consumed = size;
        _Py_RETURN_UNICODE_EMPTY();
    }

#ifdef WORDS_BIGENDIAN
    le = bo < 0;
#else
    le = bo <= 0;
#endif
    encoding = le ? "utf-32-le" : "utf-32-be";

    _PyUnicodeWriter_Init(&writer);
    writer.min_length = (e - q + 3) / 4;
    if (_PyUnicodeWriter_Prepare(&writer, writer.min_length, 127) == -1)
        goto onError;

    while (1) {
        Py_UCS4 ch = 0;
        Py_UCS4 maxch = PyUnicode_MAX_CHAR_VALUE(writer.buffer);

        if (e - q >= 4) {
            enum PyUnicode_Kind kind = writer.kind;
            void *data = writer.data;
            const unsigned char *last = e - 4;
            Py_ssize_t pos = writer.pos;
            if (le) {
                do {
                    ch = ((unsigned int)q[3] << 24) | (q[2] << 16) | (q[1] << 8) | q[0];
                    if (ch > maxch)
                        break;
                    if (kind != PyUnicode_1BYTE_KIND &&
                        Py_UNICODE_IS_SURROGATE(ch))
                        break;
                    PyUnicode_WRITE(kind, data, pos++, ch);
                    q += 4;
                } while (q <= last);
            }
            else {
                do {
                    ch = ((unsigned int)q[0] << 24) | (q[1] << 16) | (q[2] << 8) | q[3];
                    if (ch > maxch)
                        break;
                    if (kind != PyUnicode_1BYTE_KIND &&
                        Py_UNICODE_IS_SURROGATE(ch))
                        break;
                    PyUnicode_WRITE(kind, data, pos++, ch);
                    q += 4;
                } while (q <= last);
            }
            writer.pos = pos;
        }

        if (Py_UNICODE_IS_SURROGATE(ch)) {
            errmsg = "code point in surrogate code point range(0xd800, 0xe000)";
            startinpos = ((const char *)q) - starts;
            endinpos = startinpos + 4;
        }
        else if (ch <= maxch) {
            if (q == e || consumed)
                break;
            /* remaining bytes at the end? (size should be divisible by 4) */
            errmsg = "truncated data";
            startinpos = ((const char *)q) - starts;
            endinpos = ((const char *)e) - starts;
        }
        else {
            if (ch < 0x110000) {
                if (_PyUnicodeWriter_WriteCharInline(&writer, ch) < 0)
                    goto onError;
                q += 4;
                continue;
            }
            errmsg = "code point not in range(0x110000)";
            startinpos = ((const char *)q) - starts;
            endinpos = startinpos + 4;
        }

        /* The remaining input chars are ignored if the callback
           chooses to skip the input */
        if (unicode_decode_call_errorhandler_writer(
                errors, &errorHandler,
                encoding, errmsg,
                &starts, (const char **)&e, &startinpos, &endinpos, &exc, (const char **)&q,
                &writer))
            goto onError;
    }

    if (consumed)
        *consumed = (const char *)q-starts;

    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    return _PyUnicodeWriter_Finish(&writer);

  onError:
    _PyUnicodeWriter_Dealloc(&writer);
    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    return NULL;
}

PyObject *
_PyUnicode_EncodeUTF32(PyObject *str,
                       const char *errors,
                       int byteorder)
{
    enum PyUnicode_Kind kind;
    const void *data;
    Py_ssize_t len;
    PyObject *v;
    uint32_t *out;
#if PY_LITTLE_ENDIAN
    int native_ordering = byteorder <= 0;
#else
    int native_ordering = byteorder >= 0;
#endif
    const char *encoding;
    Py_ssize_t nsize, pos;
    PyObject *errorHandler = NULL;
    PyObject *exc = NULL;
    PyObject *rep = NULL;

    if (!PyUnicode_Check(str)) {
        PyErr_BadArgument();
        return NULL;
    }
    if (PyUnicode_READY(str) == -1)
        return NULL;
    kind = PyUnicode_KIND(str);
    data = PyUnicode_DATA(str);
    len = PyUnicode_GET_LENGTH(str);

    if (len > PY_SSIZE_T_MAX / 4 - (byteorder == 0))
        return PyErr_NoMemory();
    nsize = len + (byteorder == 0);
    v = PyBytes_FromStringAndSize(NULL, nsize * 4);
    if (v == NULL)
        return NULL;

    /* output buffer is 4-bytes aligned */
    assert(_Py_IS_ALIGNED(PyBytes_AS_STRING(v), 4));
    out = (uint32_t *)PyBytes_AS_STRING(v);
    if (byteorder == 0)
        *out++ = 0xFEFF;
    if (len == 0)
        goto done;

    if (byteorder == -1)
        encoding = "utf-32-le";
    else if (byteorder == 1)
        encoding = "utf-32-be";
    else
        encoding = "utf-32";

    if (kind == PyUnicode_1BYTE_KIND) {
        ucs1lib_utf32_encode((const Py_UCS1 *)data, len, &out, native_ordering);
        goto done;
    }

    pos = 0;
    while (pos < len) {
        Py_ssize_t newpos, repsize, moreunits;

        if (kind == PyUnicode_2BYTE_KIND) {
            pos += ucs2lib_utf32_encode((const Py_UCS2 *)data + pos, len - pos,
                                        &out, native_ordering);
        }
        else {
            assert(kind == PyUnicode_4BYTE_KIND);
            pos += ucs4lib_utf32_encode((const Py_UCS4 *)data + pos, len - pos,
                                        &out, native_ordering);
        }
        if (pos == len)
            break;

        rep = unicode_encode_call_errorhandler(
                errors, &errorHandler,
                encoding, "surrogates not allowed",
                str, &exc, pos, pos + 1, &newpos);
        if (!rep)
            goto error;

        if (PyBytes_Check(rep)) {
            repsize = PyBytes_GET_SIZE(rep);
            if (repsize & 3) {
                raise_encode_exception(&exc, encoding,
                                       str, pos, pos + 1,
                                       "surrogates not allowed");
                goto error;
            }
            moreunits = repsize / 4;
        }
        else {
            assert(PyUnicode_Check(rep));
            if (PyUnicode_READY(rep) < 0)
                goto error;
            moreunits = repsize = PyUnicode_GET_LENGTH(rep);
            if (!PyUnicode_IS_ASCII(rep)) {
                raise_encode_exception(&exc, encoding,
                                       str, pos, pos + 1,
                                       "surrogates not allowed");
                goto error;
            }
        }
        moreunits += pos - newpos;
        pos = newpos;

        /* four bytes are reserved for each surrogate */
        if (moreunits > 0) {
            Py_ssize_t outpos = out - (uint32_t*) PyBytes_AS_STRING(v);
            if (moreunits >= (PY_SSIZE_T_MAX - PyBytes_GET_SIZE(v)) / 4) {
                /* integer overflow */
                PyErr_NoMemory();
                goto error;
            }
            if (_PyBytes_Resize(&v, PyBytes_GET_SIZE(v) + 4 * moreunits) < 0)
                goto error;
            out = (uint32_t*) PyBytes_AS_STRING(v) + outpos;
        }

        if (PyBytes_Check(rep)) {
            memcpy(out, PyBytes_AS_STRING(rep), repsize);
            out += repsize / 4;
        } else /* rep is unicode */ {
            assert(PyUnicode_KIND(rep) == PyUnicode_1BYTE_KIND);
            ucs1lib_utf32_encode(PyUnicode_1BYTE_DATA(rep), repsize,
                                 &out, native_ordering);
        }

        Py_CLEAR(rep);
    }

    /* Cut back to size actually needed. This is necessary for, for example,
       encoding of a string containing isolated surrogates and the 'ignore'
       handler is used. */
    nsize = (unsigned char*) out - (unsigned char*) PyBytes_AS_STRING(v);
    if (nsize != PyBytes_GET_SIZE(v))
      _PyBytes_Resize(&v, nsize);
    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
  done:
    return v;
  error:
    Py_XDECREF(rep);
    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    Py_XDECREF(v);
    return NULL;
}

PyObject *
PyUnicode_EncodeUTF32(const Py_UNICODE *s,
                      Py_ssize_t size,
                      const char *errors,
                      int byteorder)
{
    PyObject *result;
    PyObject *tmp = PyUnicode_FromWideChar(s, size);
    if (tmp == NULL)
        return NULL;
    result = _PyUnicode_EncodeUTF32(tmp, errors, byteorder);
    Py_DECREF(tmp);
    return result;
}

PyObject *
PyUnicode_AsUTF32String(PyObject *unicode)
{
    return _PyUnicode_EncodeUTF32(unicode, NULL, 0);
}

/* --- UTF-16 Codec ------------------------------------------------------- */

PyObject *
PyUnicode_DecodeUTF16(const char *s,
                      Py_ssize_t size,
                      const char *errors,
                      int *byteorder)
{
    return PyUnicode_DecodeUTF16Stateful(s, size, errors, byteorder, NULL);
}

PyObject *
PyUnicode_DecodeUTF16Stateful(const char *s,
                              Py_ssize_t size,
                              const char *errors,
                              int *byteorder,
                              Py_ssize_t *consumed)
{
    const char *starts = s;
    Py_ssize_t startinpos;
    Py_ssize_t endinpos;
    _PyUnicodeWriter writer;
    const unsigned char *q, *e;
    int bo = 0;       /* assume native ordering by default */
    int native_ordering;
    const char *errmsg = "";
    PyObject *errorHandler = NULL;
    PyObject *exc = NULL;
    const char *encoding;

    q = (const unsigned char *)s;
    e = q + size;

    if (byteorder)
        bo = *byteorder;

    /* Check for BOM marks (U+FEFF) in the input and adjust current
       byte order setting accordingly. In native mode, the leading BOM
       mark is skipped, in all other modes, it is copied to the output
       stream as-is (giving a ZWNBSP character). */
    if (bo == 0 && size >= 2) {
        const Py_UCS4 bom = (q[1] << 8) | q[0];
        if (bom == 0xFEFF) {
            q += 2;
            bo = -1;
        }
        else if (bom == 0xFFFE) {
            q += 2;
            bo = 1;
        }
        if (byteorder)
            *byteorder = bo;
    }

    if (q == e) {
        if (consumed)
            *consumed = size;
        _Py_RETURN_UNICODE_EMPTY();
    }

#if PY_LITTLE_ENDIAN
    native_ordering = bo <= 0;
    encoding = bo <= 0 ? "utf-16-le" : "utf-16-be";
#else
    native_ordering = bo >= 0;
    encoding = bo >= 0 ? "utf-16-be" : "utf-16-le";
#endif

    /* Note: size will always be longer than the resulting Unicode
       character count normally.  Error handler will take care of
       resizing when needed. */
    _PyUnicodeWriter_Init(&writer);
    writer.min_length = (e - q + 1) / 2;
    if (_PyUnicodeWriter_Prepare(&writer, writer.min_length, 127) == -1)
        goto onError;

    while (1) {
        Py_UCS4 ch = 0;
        if (e - q >= 2) {
            int kind = writer.kind;
            if (kind == PyUnicode_1BYTE_KIND) {
                if (PyUnicode_IS_ASCII(writer.buffer))
                    ch = asciilib_utf16_decode(&q, e,
                            (Py_UCS1*)writer.data, &writer.pos,
                            native_ordering);
                else
                    ch = ucs1lib_utf16_decode(&q, e,
                            (Py_UCS1*)writer.data, &writer.pos,
                            native_ordering);
            } else if (kind == PyUnicode_2BYTE_KIND) {
                ch = ucs2lib_utf16_decode(&q, e,
                        (Py_UCS2*)writer.data, &writer.pos,
                        native_ordering);
            } else {
                assert(kind == PyUnicode_4BYTE_KIND);
                ch = ucs4lib_utf16_decode(&q, e,
                        (Py_UCS4*)writer.data, &writer.pos,
                        native_ordering);
            }
        }

        switch (ch)
        {
        case 0:
            /* remaining byte at the end? (size should be even) */
            if (q == e || consumed)
                goto End;
            errmsg = "truncated data";
            startinpos = ((const char *)q) - starts;
            endinpos = ((const char *)e) - starts;
            break;
            /* The remaining input chars are ignored if the callback
               chooses to skip the input */
        case 1:
            q -= 2;
            if (consumed)
                goto End;
            errmsg = "unexpected end of data";
            startinpos = ((const char *)q) - starts;
            endinpos = ((const char *)e) - starts;
            break;
        case 2:
            errmsg = "illegal encoding";
            startinpos = ((const char *)q) - 2 - starts;
            endinpos = startinpos + 2;
            break;
        case 3:
            errmsg = "illegal UTF-16 surrogate";
            startinpos = ((const char *)q) - 4 - starts;
            endinpos = startinpos + 2;
            break;
        default:
            if (_PyUnicodeWriter_WriteCharInline(&writer, ch) < 0)
                goto onError;
            continue;
        }

        if (unicode_decode_call_errorhandler_writer(
                errors,
                &errorHandler,
                encoding, errmsg,
                &starts,
                (const char **)&e,
                &startinpos,
                &endinpos,
                &exc,
                (const char **)&q,
                &writer))
            goto onError;
    }

End:
    if (consumed)
        *consumed = (const char *)q-starts;

    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    return _PyUnicodeWriter_Finish(&writer);

  onError:
    _PyUnicodeWriter_Dealloc(&writer);
    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    return NULL;
}

PyObject *
_PyUnicode_EncodeUTF16(PyObject *str,
                       const char *errors,
                       int byteorder)
{
    enum PyUnicode_Kind kind;
    const void *data;
    Py_ssize_t len;
    PyObject *v;
    unsigned short *out;
    Py_ssize_t pairs;
#if PY_BIG_ENDIAN
    int native_ordering = byteorder >= 0;
#else
    int native_ordering = byteorder <= 0;
#endif
    const char *encoding;
    Py_ssize_t nsize, pos;
    PyObject *errorHandler = NULL;
    PyObject *exc = NULL;
    PyObject *rep = NULL;

    if (!PyUnicode_Check(str)) {
        PyErr_BadArgument();
        return NULL;
    }
    if (PyUnicode_READY(str) == -1)
        return NULL;
    kind = PyUnicode_KIND(str);
    data = PyUnicode_DATA(str);
    len = PyUnicode_GET_LENGTH(str);

    pairs = 0;
    if (kind == PyUnicode_4BYTE_KIND) {
        const Py_UCS4 *in = (const Py_UCS4 *)data;
        const Py_UCS4 *end = in + len;
        while (in < end) {
            if (*in++ >= 0x10000) {
                pairs++;
            }
        }
    }
    if (len > PY_SSIZE_T_MAX / 2 - pairs - (byteorder == 0)) {
        return PyErr_NoMemory();
    }
    nsize = len + pairs + (byteorder == 0);
    v = PyBytes_FromStringAndSize(NULL, nsize * 2);
    if (v == NULL) {
        return NULL;
    }

    /* output buffer is 2-bytes aligned */
    assert(_Py_IS_ALIGNED(PyBytes_AS_STRING(v), 2));
    out = (unsigned short *)PyBytes_AS_STRING(v);
    if (byteorder == 0) {
        *out++ = 0xFEFF;
    }
    if (len == 0) {
        goto done;
    }

    if (kind == PyUnicode_1BYTE_KIND) {
        ucs1lib_utf16_encode((const Py_UCS1 *)data, len, &out, native_ordering);
        goto done;
    }

    if (byteorder < 0) {
        encoding = "utf-16-le";
    }
    else if (byteorder > 0) {
        encoding = "utf-16-be";
    }
    else {
        encoding = "utf-16";
    }

    pos = 0;
    while (pos < len) {
        Py_ssize_t newpos, repsize, moreunits;

        if (kind == PyUnicode_2BYTE_KIND) {
            pos += ucs2lib_utf16_encode((const Py_UCS2 *)data + pos, len - pos,
                                        &out, native_ordering);
        }
        else {
            assert(kind == PyUnicode_4BYTE_KIND);
            pos += ucs4lib_utf16_encode((const Py_UCS4 *)data + pos, len - pos,
                                        &out, native_ordering);
        }
        if (pos == len)
            break;

        rep = unicode_encode_call_errorhandler(
                errors, &errorHandler,
                encoding, "surrogates not allowed",
                str, &exc, pos, pos + 1, &newpos);
        if (!rep)
            goto error;

        if (PyBytes_Check(rep)) {
            repsize = PyBytes_GET_SIZE(rep);
            if (repsize & 1) {
                raise_encode_exception(&exc, encoding,
                                       str, pos, pos + 1,
                                       "surrogates not allowed");
                goto error;
            }
            moreunits = repsize / 2;
        }
        else {
            assert(PyUnicode_Check(rep));
            if (PyUnicode_READY(rep) < 0)
                goto error;
            moreunits = repsize = PyUnicode_GET_LENGTH(rep);
            if (!PyUnicode_IS_ASCII(rep)) {
                raise_encode_exception(&exc, encoding,
                                       str, pos, pos + 1,
                                       "surrogates not allowed");
                goto error;
            }
        }
        moreunits += pos - newpos;
        pos = newpos;

        /* two bytes are reserved for each surrogate */
        if (moreunits > 0) {
            Py_ssize_t outpos = out - (unsigned short*) PyBytes_AS_STRING(v);
            if (moreunits >= (PY_SSIZE_T_MAX - PyBytes_GET_SIZE(v)) / 2) {
                /* integer overflow */
                PyErr_NoMemory();
                goto error;
            }
            if (_PyBytes_Resize(&v, PyBytes_GET_SIZE(v) + 2 * moreunits) < 0)
                goto error;
            out = (unsigned short*) PyBytes_AS_STRING(v) + outpos;
        }

        if (PyBytes_Check(rep)) {
            memcpy(out, PyBytes_AS_STRING(rep), repsize);
            out += repsize / 2;
        } else /* rep is unicode */ {
            assert(PyUnicode_KIND(rep) == PyUnicode_1BYTE_KIND);
            ucs1lib_utf16_encode(PyUnicode_1BYTE_DATA(rep), repsize,
                                 &out, native_ordering);
        }

        Py_CLEAR(rep);
    }

    /* Cut back to size actually needed. This is necessary for, for example,
    encoding of a string containing isolated surrogates and the 'ignore' handler
    is used. */
    nsize = (unsigned char*) out - (unsigned char*) PyBytes_AS_STRING(v);
    if (nsize != PyBytes_GET_SIZE(v))
      _PyBytes_Resize(&v, nsize);
    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
  done:
    return v;
  error:
    Py_XDECREF(rep);
    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    Py_XDECREF(v);
    return NULL;
#undef STORECHAR
}

PyObject *
PyUnicode_EncodeUTF16(const Py_UNICODE *s,
                      Py_ssize_t size,
                      const char *errors,
                      int byteorder)
{
    PyObject *result;
    PyObject *tmp = PyUnicode_FromWideChar(s, size);
    if (tmp == NULL)
        return NULL;
    result = _PyUnicode_EncodeUTF16(tmp, errors, byteorder);
    Py_DECREF(tmp);
    return result;
}

PyObject *
PyUnicode_AsUTF16String(PyObject *unicode)
{
    return _PyUnicode_EncodeUTF16(unicode, NULL, 0);
}

/* --- Unicode Escape Codec ----------------------------------------------- */

static _PyUnicode_Name_CAPI *ucnhash_capi = NULL;

PyObject *
_PyUnicode_DecodeUnicodeEscapeInternal(const char *s,
                               Py_ssize_t size,
                               const char *errors,
                               Py_ssize_t *consumed,
                               const char **first_invalid_escape)
{
    const char *starts = s;
    _PyUnicodeWriter writer;
    const char *end;
    PyObject *errorHandler = NULL;
    PyObject *exc = NULL;

    // so we can remember if we've seen an invalid escape char or not
    *first_invalid_escape = NULL;

    if (size == 0) {
        if (consumed) {
            *consumed = 0;
        }
        _Py_RETURN_UNICODE_EMPTY();
    }
    /* Escaped strings will always be longer than the resulting
       Unicode string, so we start with size here and then reduce the
       length after conversion to the true value.
       (but if the error callback returns a long replacement string
       we'll have to allocate more space) */
    _PyUnicodeWriter_Init(&writer);
    writer.min_length = size;
    if (_PyUnicodeWriter_Prepare(&writer, size, 127) < 0) {
        goto onError;
    }

    end = s + size;
    while (s < end) {
        unsigned char c = (unsigned char) *s++;
        Py_UCS4 ch;
        int count;
        const char *message;

#define WRITE_ASCII_CHAR(ch)                                                  \
            do {                                                              \
                assert(ch <= 127);                                            \
                assert(writer.pos < writer.size);                             \
                PyUnicode_WRITE(writer.kind, writer.data, writer.pos++, ch);  \
            } while(0)

#define WRITE_CHAR(ch)                                                        \
            do {                                                              \
                if (ch <= writer.maxchar) {                                   \
                    assert(writer.pos < writer.size);                         \
                    PyUnicode_WRITE(writer.kind, writer.data, writer.pos++, ch); \
                }                                                             \
                else if (_PyUnicodeWriter_WriteCharInline(&writer, ch) < 0) { \
                    goto onError;                                             \
                }                                                             \
            } while(0)

        /* Non-escape characters are interpreted as Unicode ordinals */
        if (c != '\\') {
            WRITE_CHAR(c);
            continue;
        }

        Py_ssize_t startinpos = s - starts - 1;
        /* \ - Escapes */
        if (s >= end) {
            message = "\\ at end of string";
            goto incomplete;
        }
        c = (unsigned char) *s++;

        assert(writer.pos < writer.size);
        switch (c) {

            /* \x escapes */
        case '\n': continue;
        case '\\': WRITE_ASCII_CHAR('\\'); continue;
        case '\'': WRITE_ASCII_CHAR('\''); continue;
        case '\"': WRITE_ASCII_CHAR('\"'); continue;
        case 'b': WRITE_ASCII_CHAR('\b'); continue;
        /* FF */
        case 'f': WRITE_ASCII_CHAR('\014'); continue;
        case 't': WRITE_ASCII_CHAR('\t'); continue;
        case 'n': WRITE_ASCII_CHAR('\n'); continue;
        case 'r': WRITE_ASCII_CHAR('\r'); continue;
        /* VT */
        case 'v': WRITE_ASCII_CHAR('\013'); continue;
        /* BEL, not classic C */
        case 'a': WRITE_ASCII_CHAR('\007'); continue;

            /* \OOO (octal) escapes */
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7':
            ch = c - '0';
            if (s < end && '0' <= *s && *s <= '7') {
                ch = (ch<<3) + *s++ - '0';
                if (s < end && '0' <= *s && *s <= '7') {
                    ch = (ch<<3) + *s++ - '0';
                }
            }
            WRITE_CHAR(ch);
            continue;

            /* hex escapes */
            /* \xXX */
        case 'x':
            count = 2;
            message = "truncated \\xXX escape";
            goto hexescape;

            /* \uXXXX */
        case 'u':
            count = 4;
            message = "truncated \\uXXXX escape";
            goto hexescape;

            /* \UXXXXXXXX */
        case 'U':
            count = 8;
            message = "truncated \\UXXXXXXXX escape";
        hexescape:
            for (ch = 0; count; ++s, --count) {
                if (s >= end) {
                    goto incomplete;
                }
                c = (unsigned char)*s;
                ch <<= 4;
                if (c >= '0' && c <= '9') {
                    ch += c - '0';
                }
                else if (c >= 'a' && c <= 'f') {
                    ch += c - ('a' - 10);
                }
                else if (c >= 'A' && c <= 'F') {
                    ch += c - ('A' - 10);
                }
                else {
                    goto error;
                }
            }

            /* when we get here, ch is a 32-bit unicode character */
            if (ch > MAX_UNICODE) {
                message = "illegal Unicode character";
                goto error;
            }

            WRITE_CHAR(ch);
            continue;

            /* \N{name} */
        case 'N':
            if (ucnhash_capi == NULL) {
                /* load the unicode data module */
                ucnhash_capi = (_PyUnicode_Name_CAPI *)PyCapsule_Import(
                                                PyUnicodeData_CAPSULE_NAME, 1);
                if (ucnhash_capi == NULL) {
                    PyErr_SetString(
                        PyExc_UnicodeError,
                        "\\N escapes not supported (can't load unicodedata module)"
                        );
                    goto onError;
                }
            }

            message = "malformed \\N character escape";
            if (s >= end) {
                goto incomplete;
            }
            if (*s == '{') {
                const char *start = ++s;
                size_t namelen;
                /* look for the closing brace */
                while (s < end && *s != '}')
                    s++;
                if (s >= end) {
                    goto incomplete;
                }
                namelen = s - start;
                if (namelen) {
                    /* found a name.  look it up in the unicode database */
                    s++;
                    ch = 0xffffffff; /* in case 'getcode' messes up */
                    if (namelen <= INT_MAX &&
                        ucnhash_capi->getcode(start, (int)namelen,
                                              &ch, 0)) {
                        assert(ch <= MAX_UNICODE);
                        WRITE_CHAR(ch);
                        continue;
                    }
                    message = "unknown Unicode character name";
                }
            }
            goto error;

        default:
            if (*first_invalid_escape == NULL) {
                *first_invalid_escape = s-1; /* Back up one char, since we've
                                                already incremented s. */
            }
            WRITE_ASCII_CHAR('\\');
            WRITE_CHAR(c);
            continue;
        }

      incomplete:
        if (consumed) {
            *consumed = startinpos;
            break;
        }
      error:;
        Py_ssize_t endinpos = s-starts;
        writer.min_length = end - s + writer.pos;
        if (unicode_decode_call_errorhandler_writer(
                errors, &errorHandler,
                "unicodeescape", message,
                &starts, &end, &startinpos, &endinpos, &exc, &s,
                &writer)) {
            goto onError;
        }
        assert(end - s <= writer.size - writer.pos);

#undef WRITE_ASCII_CHAR
#undef WRITE_CHAR
    }

    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    return _PyUnicodeWriter_Finish(&writer);

  onError:
    _PyUnicodeWriter_Dealloc(&writer);
    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    return NULL;
}

PyObject *
_PyUnicode_DecodeUnicodeEscapeStateful(const char *s,
                              Py_ssize_t size,
                              const char *errors,
                              Py_ssize_t *consumed)
{
    const char *first_invalid_escape;
    PyObject *result = _PyUnicode_DecodeUnicodeEscapeInternal(s, size, errors,
                                                      consumed,
                                                      &first_invalid_escape);
    if (result == NULL)
        return NULL;
    if (first_invalid_escape != NULL) {
        if (PyErr_WarnFormat(PyExc_DeprecationWarning, 1,
                             "invalid escape sequence '\\%c'",
                             (unsigned char)*first_invalid_escape) < 0) {
            Py_DECREF(result);
            return NULL;
        }
    }
    return result;
}

PyObject *
PyUnicode_DecodeUnicodeEscape(const char *s,
                              Py_ssize_t size,
                              const char *errors)
{
    return _PyUnicode_DecodeUnicodeEscapeStateful(s, size, errors, NULL);
}

/* Return a Unicode-Escape string version of the Unicode object. */

PyObject *
PyUnicode_AsUnicodeEscapeString(PyObject *unicode)
{
    Py_ssize_t i, len;
    PyObject *repr;
    char *p;
    enum PyUnicode_Kind kind;
    const void *data;
    Py_ssize_t expandsize;

    /* Initial allocation is based on the longest-possible character
       escape.

       For UCS1 strings it's '\xxx', 4 bytes per source character.
       For UCS2 strings it's '\uxxxx', 6 bytes per source character.
       For UCS4 strings it's '\U00xxxxxx', 10 bytes per source character.
    */

    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return NULL;
    }
    if (PyUnicode_READY(unicode) == -1) {
        return NULL;
    }

    len = PyUnicode_GET_LENGTH(unicode);
    if (len == 0) {
        return PyBytes_FromStringAndSize(NULL, 0);
    }

    kind = PyUnicode_KIND(unicode);
    data = PyUnicode_DATA(unicode);
    /* 4 byte characters can take up 10 bytes, 2 byte characters can take up 6
       bytes, and 1 byte characters 4. */
    expandsize = kind * 2 + 2;
    if (len > PY_SSIZE_T_MAX / expandsize) {
        return PyErr_NoMemory();
    }
    repr = PyBytes_FromStringAndSize(NULL, expandsize * len);
    if (repr == NULL) {
        return NULL;
    }

    p = PyBytes_AS_STRING(repr);
    for (i = 0; i < len; i++) {
        Py_UCS4 ch = PyUnicode_READ(kind, data, i);

        /* U+0000-U+00ff range */
        if (ch < 0x100) {
            if (ch >= ' ' && ch < 127) {
                if (ch != '\\') {
                    /* Copy printable US ASCII as-is */
                    *p++ = (char) ch;
                }
                /* Escape backslashes */
                else {
                    *p++ = '\\';
                    *p++ = '\\';
                }
            }

            /* Map special whitespace to '\t', \n', '\r' */
            else if (ch == '\t') {
                *p++ = '\\';
                *p++ = 't';
            }
            else if (ch == '\n') {
                *p++ = '\\';
                *p++ = 'n';
            }
            else if (ch == '\r') {
                *p++ = '\\';
                *p++ = 'r';
            }

            /* Map non-printable US ASCII and 8-bit characters to '\xHH' */
            else {
                *p++ = '\\';
                *p++ = 'x';
                *p++ = Py_hexdigits[(ch >> 4) & 0x000F];
                *p++ = Py_hexdigits[ch & 0x000F];
            }
        }
        /* U+0100-U+ffff range: Map 16-bit characters to '\uHHHH' */
        else if (ch < 0x10000) {
            *p++ = '\\';
            *p++ = 'u';
            *p++ = Py_hexdigits[(ch >> 12) & 0x000F];
            *p++ = Py_hexdigits[(ch >> 8) & 0x000F];
            *p++ = Py_hexdigits[(ch >> 4) & 0x000F];
            *p++ = Py_hexdigits[ch & 0x000F];
        }
        /* U+010000-U+10ffff range: Map 21-bit characters to '\U00HHHHHH' */
        else {

            /* Make sure that the first two digits are zero */
            assert(ch <= MAX_UNICODE && MAX_UNICODE <= 0x10ffff);
            *p++ = '\\';
            *p++ = 'U';
            *p++ = '0';
            *p++ = '0';
            *p++ = Py_hexdigits[(ch >> 20) & 0x0000000F];
            *p++ = Py_hexdigits[(ch >> 16) & 0x0000000F];
            *p++ = Py_hexdigits[(ch >> 12) & 0x0000000F];
            *p++ = Py_hexdigits[(ch >> 8) & 0x0000000F];
            *p++ = Py_hexdigits[(ch >> 4) & 0x0000000F];
            *p++ = Py_hexdigits[ch & 0x0000000F];
        }
    }

    assert(p - PyBytes_AS_STRING(repr) > 0);
    if (_PyBytes_Resize(&repr, p - PyBytes_AS_STRING(repr)) < 0) {
        return NULL;
    }
    return repr;
}

PyObject *
PyUnicode_EncodeUnicodeEscape(const Py_UNICODE *s,
                              Py_ssize_t size)
{
    PyObject *result;
    PyObject *tmp = PyUnicode_FromWideChar(s, size);
    if (tmp == NULL) {
        return NULL;
    }

    result = PyUnicode_AsUnicodeEscapeString(tmp);
    Py_DECREF(tmp);
    return result;
}

/* --- Raw Unicode Escape Codec ------------------------------------------- */

PyObject *
_PyUnicode_DecodeRawUnicodeEscapeStateful(const char *s,
                                          Py_ssize_t size,
                                          const char *errors,
                                          Py_ssize_t *consumed)
{
    const char *starts = s;
    _PyUnicodeWriter writer;
    const char *end;
    PyObject *errorHandler = NULL;
    PyObject *exc = NULL;

    if (size == 0) {
        if (consumed) {
            *consumed = 0;
        }
        _Py_RETURN_UNICODE_EMPTY();
    }

    /* Escaped strings will always be longer than the resulting
       Unicode string, so we start with size here and then reduce the
       length after conversion to the true value. (But decoding error
       handler might have to resize the string) */
    _PyUnicodeWriter_Init(&writer);
    writer.min_length = size;
    if (_PyUnicodeWriter_Prepare(&writer, size, 127) < 0) {
        goto onError;
    }

    end = s + size;
    while (s < end) {
        unsigned char c = (unsigned char) *s++;
        Py_UCS4 ch;
        int count;
        const char *message;

#define WRITE_CHAR(ch)                                                        \
            do {                                                              \
                if (ch <= writer.maxchar) {                                   \
                    assert(writer.pos < writer.size);                         \
                    PyUnicode_WRITE(writer.kind, writer.data, writer.pos++, ch); \
                }                                                             \
                else if (_PyUnicodeWriter_WriteCharInline(&writer, ch) < 0) { \
                    goto onError;                                             \
                }                                                             \
            } while(0)

        /* Non-escape characters are interpreted as Unicode ordinals */
        if (c != '\\' || (s >= end && !consumed)) {
            WRITE_CHAR(c);
            continue;
        }

        Py_ssize_t startinpos = s - starts - 1;
        /* \ - Escapes */
        if (s >= end) {
            assert(consumed);
            // Set message to silent compiler warning.
            // Actually it is never used.
            message = "\\ at end of string";
            goto incomplete;
        }

        c = (unsigned char) *s++;
        if (c == 'u') {
            count = 4;
            message = "truncated \\uXXXX escape";
        }
        else if (c == 'U') {
            count = 8;
            message = "truncated \\UXXXXXXXX escape";
        }
        else {
            assert(writer.pos < writer.size);
            PyUnicode_WRITE(writer.kind, writer.data, writer.pos++, '\\');
            WRITE_CHAR(c);
            continue;
        }

        /* \uHHHH with 4 hex digits, \U00HHHHHH with 8 */
        for (ch = 0; count; ++s, --count) {
            if (s >= end) {
                goto incomplete;
            }
            c = (unsigned char)*s;
            ch <<= 4;
            if (c >= '0' && c <= '9') {
                ch += c - '0';
            }
            else if (c >= 'a' && c <= 'f') {
                ch += c - ('a' - 10);
            }
            else if (c >= 'A' && c <= 'F') {
                ch += c - ('A' - 10);
            }
            else {
                goto error;
            }
        }
        if (ch > MAX_UNICODE) {
            message = "\\Uxxxxxxxx out of range";
            goto error;
        }
        WRITE_CHAR(ch);
        continue;

      incomplete:
        if (consumed) {
            *consumed = startinpos;
            break;
        }
      error:;
        Py_ssize_t endinpos = s-starts;
        writer.min_length = end - s + writer.pos;
        if (unicode_decode_call_errorhandler_writer(
                errors, &errorHandler,
                "rawunicodeescape", message,
                &starts, &end, &startinpos, &endinpos, &exc, &s,
                &writer)) {
            goto onError;
        }
        assert(end - s <= writer.size - writer.pos);

#undef WRITE_CHAR
    }
    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    return _PyUnicodeWriter_Finish(&writer);

  onError:
    _PyUnicodeWriter_Dealloc(&writer);
    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    return NULL;
}

PyObject *
PyUnicode_DecodeRawUnicodeEscape(const char *s,
                                 Py_ssize_t size,
                                 const char *errors)
{
    return _PyUnicode_DecodeRawUnicodeEscapeStateful(s, size, errors, NULL);
}


PyObject *
PyUnicode_AsRawUnicodeEscapeString(PyObject *unicode)
{
    PyObject *repr;
    char *p;
    Py_ssize_t expandsize, pos;
    int kind;
    const void *data;
    Py_ssize_t len;

    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return NULL;
    }
    if (PyUnicode_READY(unicode) == -1) {
        return NULL;
    }
    kind = PyUnicode_KIND(unicode);
    data = PyUnicode_DATA(unicode);
    len = PyUnicode_GET_LENGTH(unicode);
    if (kind == PyUnicode_1BYTE_KIND) {
        return PyBytes_FromStringAndSize(data, len);
    }

    /* 4 byte characters can take up 10 bytes, 2 byte characters can take up 6
       bytes, and 1 byte characters 4. */
    expandsize = kind * 2 + 2;

    if (len > PY_SSIZE_T_MAX / expandsize) {
        return PyErr_NoMemory();
    }
    repr = PyBytes_FromStringAndSize(NULL, expandsize * len);
    if (repr == NULL) {
        return NULL;
    }
    if (len == 0) {
        return repr;
    }

    p = PyBytes_AS_STRING(repr);
    for (pos = 0; pos < len; pos++) {
        Py_UCS4 ch = PyUnicode_READ(kind, data, pos);

        /* U+0000-U+00ff range: Copy 8-bit characters as-is */
        if (ch < 0x100) {
            *p++ = (char) ch;
        }
        /* U+0100-U+ffff range: Map 16-bit characters to '\uHHHH' */
        else if (ch < 0x10000) {
            *p++ = '\\';
            *p++ = 'u';
            *p++ = Py_hexdigits[(ch >> 12) & 0xf];
            *p++ = Py_hexdigits[(ch >> 8) & 0xf];
            *p++ = Py_hexdigits[(ch >> 4) & 0xf];
            *p++ = Py_hexdigits[ch & 15];
        }
        /* U+010000-U+10ffff range: Map 32-bit characters to '\U00HHHHHH' */
        else {
            assert(ch <= MAX_UNICODE && MAX_UNICODE <= 0x10ffff);
            *p++ = '\\';
            *p++ = 'U';
            *p++ = '0';
            *p++ = '0';
            *p++ = Py_hexdigits[(ch >> 20) & 0xf];
            *p++ = Py_hexdigits[(ch >> 16) & 0xf];
            *p++ = Py_hexdigits[(ch >> 12) & 0xf];
            *p++ = Py_hexdigits[(ch >> 8) & 0xf];
            *p++ = Py_hexdigits[(ch >> 4) & 0xf];
            *p++ = Py_hexdigits[ch & 15];
        }
    }

    assert(p > PyBytes_AS_STRING(repr));
    if (_PyBytes_Resize(&repr, p - PyBytes_AS_STRING(repr)) < 0) {
        return NULL;
    }
    return repr;
}

PyObject *
PyUnicode_EncodeRawUnicodeEscape(const Py_UNICODE *s,
                                 Py_ssize_t size)
{
    PyObject *result;
    PyObject *tmp = PyUnicode_FromWideChar(s, size);
    if (tmp == NULL)
        return NULL;
    result = PyUnicode_AsRawUnicodeEscapeString(tmp);
    Py_DECREF(tmp);
    return result;
}

/* --- Latin-1 Codec ------------------------------------------------------ */

PyObject *
PyUnicode_DecodeLatin1(const char *s,
                       Py_ssize_t size,
                       const char *errors)
{
    /* Latin-1 is equivalent to the first 256 ordinals in Unicode. */
    return _PyUnicode_FromUCS1((const unsigned char*)s, size);
}

/* create or adjust a UnicodeEncodeError */
static void
make_encode_exception(PyObject **exceptionObject,
                      const char *encoding,
                      PyObject *unicode,
                      Py_ssize_t startpos, Py_ssize_t endpos,
                      const char *reason)
{
    if (*exceptionObject == NULL) {
        *exceptionObject = PyObject_CallFunction(
            PyExc_UnicodeEncodeError, "sOnns",
            encoding, unicode, startpos, endpos, reason);
    }
    else {
        if (PyUnicodeEncodeError_SetStart(*exceptionObject, startpos))
            goto onError;
        if (PyUnicodeEncodeError_SetEnd(*exceptionObject, endpos))
            goto onError;
        if (PyUnicodeEncodeError_SetReason(*exceptionObject, reason))
            goto onError;
        return;
      onError:
        Py_CLEAR(*exceptionObject);
    }
}

/* raises a UnicodeEncodeError */
static void
raise_encode_exception(PyObject **exceptionObject,
                       const char *encoding,
                       PyObject *unicode,
                       Py_ssize_t startpos, Py_ssize_t endpos,
                       const char *reason)
{
    make_encode_exception(exceptionObject,
                          encoding, unicode, startpos, endpos, reason);
    if (*exceptionObject != NULL)
        PyCodec_StrictErrors(*exceptionObject);
}

/* error handling callback helper:
   build arguments, call the callback and check the arguments,
   put the result into newpos and return the replacement string, which
   has to be freed by the caller */
static PyObject *
unicode_encode_call_errorhandler(const char *errors,
                                 PyObject **errorHandler,
                                 const char *encoding, const char *reason,
                                 PyObject *unicode, PyObject **exceptionObject,
                                 Py_ssize_t startpos, Py_ssize_t endpos,
                                 Py_ssize_t *newpos)
{
    static const char *argparse = "On;encoding error handler must return (str/bytes, int) tuple";
    Py_ssize_t len;
    PyObject *restuple;
    PyObject *resunicode;

    if (*errorHandler == NULL) {
        *errorHandler = PyCodec_LookupError(errors);
        if (*errorHandler == NULL)
            return NULL;
    }

    if (PyUnicode_READY(unicode) == -1)
        return NULL;
    len = PyUnicode_GET_LENGTH(unicode);

    make_encode_exception(exceptionObject,
                          encoding, unicode, startpos, endpos, reason);
    if (*exceptionObject == NULL)
        return NULL;

    restuple = PyObject_CallOneArg(*errorHandler, *exceptionObject);
    if (restuple == NULL)
        return NULL;
    if (!PyTuple_Check(restuple)) {
        PyErr_SetString(PyExc_TypeError, &argparse[3]);
        Py_DECREF(restuple);
        return NULL;
    }
    if (!PyArg_ParseTuple(restuple, argparse,
                          &resunicode, newpos)) {
        Py_DECREF(restuple);
        return NULL;
    }
    if (!PyUnicode_Check(resunicode) && !PyBytes_Check(resunicode)) {
        PyErr_SetString(PyExc_TypeError, &argparse[3]);
        Py_DECREF(restuple);
        return NULL;
    }
    if (*newpos<0)
        *newpos = len + *newpos;
    if (*newpos<0 || *newpos>len) {
        PyErr_Format(PyExc_IndexError, "position %zd from error handler out of bounds", *newpos);
        Py_DECREF(restuple);
        return NULL;
    }
    Py_INCREF(resunicode);
    Py_DECREF(restuple);
    return resunicode;
}

static PyObject *
unicode_encode_ucs1(PyObject *unicode,
                    const char *errors,
                    const Py_UCS4 limit)
{
    /* input state */
    Py_ssize_t pos=0, size;
    int kind;
    const void *data;
    /* pointer into the output */
    char *str;
    const char *encoding = (limit == 256) ? "latin-1" : "ascii";
    const char *reason = (limit == 256) ? "ordinal not in range(256)" : "ordinal not in range(128)";
    PyObject *error_handler_obj = NULL;
    PyObject *exc = NULL;
    _Py_error_handler error_handler = _Py_ERROR_UNKNOWN;
    PyObject *rep = NULL;
    /* output object */
    _PyBytesWriter writer;

    if (PyUnicode_READY(unicode) == -1)
        return NULL;
    size = PyUnicode_GET_LENGTH(unicode);
    kind = PyUnicode_KIND(unicode);
    data = PyUnicode_DATA(unicode);
    /* allocate enough for a simple encoding without
       replacements, if we need more, we'll resize */
    if (size == 0)
        return PyBytes_FromStringAndSize(NULL, 0);

    _PyBytesWriter_Init(&writer);
    str = _PyBytesWriter_Alloc(&writer, size);
    if (str == NULL)
        return NULL;

    while (pos < size) {
        Py_UCS4 ch = PyUnicode_READ(kind, data, pos);

        /* can we encode this? */
        if (ch < limit) {
            /* no overflow check, because we know that the space is enough */
            *str++ = (char)ch;
            ++pos;
        }
        else {
            Py_ssize_t newpos, i;
            /* startpos for collecting unencodable chars */
            Py_ssize_t collstart = pos;
            Py_ssize_t collend = collstart + 1;
            /* find all unecodable characters */

            while ((collend < size) && (PyUnicode_READ(kind, data, collend) >= limit))
                ++collend;

            /* Only overallocate the buffer if it's not the last write */
            writer.overallocate = (collend < size);

            /* cache callback name lookup (if not done yet, i.e. it's the first error) */
            if (error_handler == _Py_ERROR_UNKNOWN)
                error_handler = _Py_GetErrorHandler(errors);

            switch (error_handler) {
            case _Py_ERROR_STRICT:
                raise_encode_exception(&exc, encoding, unicode, collstart, collend, reason);
                goto onError;

            case _Py_ERROR_REPLACE:
                memset(str, '?', collend - collstart);
                str += (collend - collstart);
                /* fall through */
            case _Py_ERROR_IGNORE:
                pos = collend;
                break;

            case _Py_ERROR_BACKSLASHREPLACE:
                /* subtract preallocated bytes */
                writer.min_size -= (collend - collstart);
                str = backslashreplace(&writer, str,
                                       unicode, collstart, collend);
                if (str == NULL)
                    goto onError;
                pos = collend;
                break;

            case _Py_ERROR_XMLCHARREFREPLACE:
                /* subtract preallocated bytes */
                writer.min_size -= (collend - collstart);
                str = xmlcharrefreplace(&writer, str,
                                        unicode, collstart, collend);
                if (str == NULL)
                    goto onError;
                pos = collend;
                break;

            case _Py_ERROR_SURROGATEESCAPE:
                for (i = collstart; i < collend; ++i) {
                    ch = PyUnicode_READ(kind, data, i);
                    if (ch < 0xdc80 || 0xdcff < ch) {
                        /* Not a UTF-8b surrogate */
                        break;
                    }
                    *str++ = (char)(ch - 0xdc00);
                    ++pos;
                }
                if (i >= collend)
                    break;
                collstart = pos;
                assert(collstart != collend);
                /* fall through */

            default:
                rep = unicode_encode_call_errorhandler(errors, &error_handler_obj,
                                                       encoding, reason, unicode, &exc,
                                                       collstart, collend, &newpos);
                if (rep == NULL)
                    goto onError;

                if (newpos < collstart) {
                    writer.overallocate = 1;
                    str = _PyBytesWriter_Prepare(&writer, str,
                                                 collstart - newpos);
                    if (str == NULL)
                        goto onError;
                }
                else {
                    /* subtract preallocated bytes */
                    writer.min_size -= newpos - collstart;
                    /* Only overallocate the buffer if it's not the last write */
                    writer.overallocate = (newpos < size);
                }

                if (PyBytes_Check(rep)) {
                    /* Directly copy bytes result to output. */
                    str = _PyBytesWriter_WriteBytes(&writer, str,
                                                    PyBytes_AS_STRING(rep),
                                                    PyBytes_GET_SIZE(rep));
                }
                else {
                    assert(PyUnicode_Check(rep));

                    if (PyUnicode_READY(rep) < 0)
                        goto onError;

                    if (limit == 256 ?
                        PyUnicode_KIND(rep) != PyUnicode_1BYTE_KIND :
                        !PyUnicode_IS_ASCII(rep))
                    {
                        /* Not all characters are smaller than limit */
                        raise_encode_exception(&exc, encoding, unicode,
                                               collstart, collend, reason);
                        goto onError;
                    }
                    assert(PyUnicode_KIND(rep) == PyUnicode_1BYTE_KIND);
                    str = _PyBytesWriter_WriteBytes(&writer, str,
                                                    PyUnicode_DATA(rep),
                                                    PyUnicode_GET_LENGTH(rep));
                }
                if (str == NULL)
                    goto onError;

                pos = newpos;
                Py_CLEAR(rep);
            }

            /* If overallocation was disabled, ensure that it was the last
               write. Otherwise, we missed an optimization */
            assert(writer.overallocate || pos == size);
        }
    }

    Py_XDECREF(error_handler_obj);
    Py_XDECREF(exc);
    return _PyBytesWriter_Finish(&writer, str);

  onError:
    Py_XDECREF(rep);
    _PyBytesWriter_Dealloc(&writer);
    Py_XDECREF(error_handler_obj);
    Py_XDECREF(exc);
    return NULL;
}

/* Deprecated */
PyObject *
PyUnicode_EncodeLatin1(const Py_UNICODE *p,
                       Py_ssize_t size,
                       const char *errors)
{
    PyObject *result;
    PyObject *unicode = PyUnicode_FromWideChar(p, size);
    if (unicode == NULL)
        return NULL;
    result = unicode_encode_ucs1(unicode, errors, 256);
    Py_DECREF(unicode);
    return result;
}

PyObject *
_PyUnicode_AsLatin1String(PyObject *unicode, const char *errors)
{
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return NULL;
    }
    if (PyUnicode_READY(unicode) == -1)
        return NULL;
    /* Fast path: if it is a one-byte string, construct
       bytes object directly. */
    if (PyUnicode_KIND(unicode) == PyUnicode_1BYTE_KIND)
        return PyBytes_FromStringAndSize(PyUnicode_DATA(unicode),
                                         PyUnicode_GET_LENGTH(unicode));
    /* Non-Latin-1 characters present. Defer to above function to
       raise the exception. */
    return unicode_encode_ucs1(unicode, errors, 256);
}

PyObject*
PyUnicode_AsLatin1String(PyObject *unicode)
{
    return _PyUnicode_AsLatin1String(unicode, NULL);
}

/* --- 7-bit ASCII Codec -------------------------------------------------- */

PyObject *
PyUnicode_DecodeASCII(const char *s,
                      Py_ssize_t size,
                      const char *errors)
{
    const char *starts = s;
    const char *e = s + size;
    PyObject *error_handler_obj = NULL;
    PyObject *exc = NULL;
    _Py_error_handler error_handler = _Py_ERROR_UNKNOWN;

    if (size == 0)
        _Py_RETURN_UNICODE_EMPTY();

    /* ASCII is equivalent to the first 128 ordinals in Unicode. */
    if (size == 1 && (unsigned char)s[0] < 128) {
        return get_latin1_char((unsigned char)s[0]);
    }

    // Shortcut for simple case
    PyObject *u = PyUnicode_New(size, 127);
    if (u == NULL) {
        return NULL;
    }
    Py_ssize_t outpos = ascii_decode(s, e, PyUnicode_1BYTE_DATA(u));
    if (outpos == size) {
        return u;
    }

    _PyUnicodeWriter writer;
    _PyUnicodeWriter_InitWithBuffer(&writer, u);
    writer.pos = outpos;

    s += outpos;
    int kind = writer.kind;
    void *data = writer.data;
    Py_ssize_t startinpos, endinpos;

    while (s < e) {
        unsigned char c = (unsigned char)*s;
        if (c < 128) {
            PyUnicode_WRITE(kind, data, writer.pos, c);
            writer.pos++;
            ++s;
            continue;
        }

        /* byte outsize range 0x00..0x7f: call the error handler */

        if (error_handler == _Py_ERROR_UNKNOWN)
            error_handler = _Py_GetErrorHandler(errors);

        switch (error_handler)
        {
        case _Py_ERROR_REPLACE:
        case _Py_ERROR_SURROGATEESCAPE:
            /* Fast-path: the error handler only writes one character,
               but we may switch to UCS2 at the first write */
            if (_PyUnicodeWriter_PrepareKind(&writer, PyUnicode_2BYTE_KIND) < 0)
                goto onError;
            kind = writer.kind;
            data = writer.data;

            if (error_handler == _Py_ERROR_REPLACE)
                PyUnicode_WRITE(kind, data, writer.pos, 0xfffd);
            else
                PyUnicode_WRITE(kind, data, writer.pos, c + 0xdc00);
            writer.pos++;
            ++s;
            break;

        case _Py_ERROR_IGNORE:
            ++s;
            break;

        default:
            startinpos = s-starts;
            endinpos = startinpos + 1;
            if (unicode_decode_call_errorhandler_writer(
                    errors, &error_handler_obj,
                    "ascii", "ordinal not in range(128)",
                    &starts, &e, &startinpos, &endinpos, &exc, &s,
                    &writer))
                goto onError;
            kind = writer.kind;
            data = writer.data;
        }
    }
    Py_XDECREF(error_handler_obj);
    Py_XDECREF(exc);
    return _PyUnicodeWriter_Finish(&writer);

  onError:
    _PyUnicodeWriter_Dealloc(&writer);
    Py_XDECREF(error_handler_obj);
    Py_XDECREF(exc);
    return NULL;
}

/* Deprecated */
PyObject *
PyUnicode_EncodeASCII(const Py_UNICODE *p,
                      Py_ssize_t size,
                      const char *errors)
{
    PyObject *result;
    PyObject *unicode = PyUnicode_FromWideChar(p, size);
    if (unicode == NULL)
        return NULL;
    result = unicode_encode_ucs1(unicode, errors, 128);
    Py_DECREF(unicode);
    return result;
}

PyObject *
_PyUnicode_AsASCIIString(PyObject *unicode, const char *errors)
{
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return NULL;
    }
    if (PyUnicode_READY(unicode) == -1)
        return NULL;
    /* Fast path: if it is an ASCII-only string, construct bytes object
       directly. Else defer to above function to raise the exception. */
    if (PyUnicode_IS_ASCII(unicode))
        return PyBytes_FromStringAndSize(PyUnicode_DATA(unicode),
                                         PyUnicode_GET_LENGTH(unicode));
    return unicode_encode_ucs1(unicode, errors, 128);
}

PyObject *
PyUnicode_AsASCIIString(PyObject *unicode)
{
    return _PyUnicode_AsASCIIString(unicode, NULL);
}

#ifdef MS_WINDOWS

/* --- MBCS codecs for Windows -------------------------------------------- */

#if SIZEOF_INT < SIZEOF_SIZE_T
#define NEED_RETRY
#endif

/* INT_MAX is the theoretical largest chunk (or INT_MAX / 2 when
   transcoding from UTF-16), but INT_MAX / 4 performs better in
   both cases also and avoids partial characters overrunning the
   length limit in MultiByteToWideChar on Windows */
#define DECODING_CHUNK_SIZE (INT_MAX/4)

#ifndef WC_ERR_INVALID_CHARS
#  define WC_ERR_INVALID_CHARS 0x0080
#endif

static const char*
code_page_name(UINT code_page, PyObject **obj)
{
    *obj = NULL;
    if (code_page == CP_ACP)
        return "mbcs";
    if (code_page == CP_UTF7)
        return "CP_UTF7";
    if (code_page == CP_UTF8)
        return "CP_UTF8";

    *obj = PyBytes_FromFormat("cp%u", code_page);
    if (*obj == NULL)
        return NULL;
    return PyBytes_AS_STRING(*obj);
}

static DWORD
decode_code_page_flags(UINT code_page)
{
    if (code_page == CP_UTF7) {
        /* The CP_UTF7 decoder only supports flags=0 */
        return 0;
    }
    else
        return MB_ERR_INVALID_CHARS;
}

/*
 * Decode a byte string from a Windows code page into unicode object in strict
 * mode.
 *
 * Returns consumed size if succeed, returns -2 on decode error, or raise an
 * OSError and returns -1 on other error.
 */
static int
decode_code_page_strict(UINT code_page,
                        wchar_t **buf,
                        Py_ssize_t *bufsize,
                        const char *in,
                        int insize)
{
    DWORD flags = MB_ERR_INVALID_CHARS;
    wchar_t *out;
    DWORD outsize;

    /* First get the size of the result */
    assert(insize > 0);
    while ((outsize = MultiByteToWideChar(code_page, flags,
                                          in, insize, NULL, 0)) <= 0)
    {
        if (!flags || GetLastError() != ERROR_INVALID_FLAGS) {
            goto error;
        }
        /* For some code pages (e.g. UTF-7) flags must be set to 0. */
        flags = 0;
    }

    /* Extend a wchar_t* buffer */
    Py_ssize_t n = *bufsize;   /* Get the current length */
    if (widechar_resize(buf, bufsize, n + outsize) < 0) {
        return -1;
    }
    out = *buf + n;

    /* Do the conversion */
    outsize = MultiByteToWideChar(code_page, flags, in, insize, out, outsize);
    if (outsize <= 0)
        goto error;
    return insize;

error:
    if (GetLastError() == ERROR_NO_UNICODE_TRANSLATION)
        return -2;
    PyErr_SetFromWindowsErr(0);
    return -1;
}

/*
 * Decode a byte string from a code page into unicode object with an error
 * handler.
 *
 * Returns consumed size if succeed, or raise an OSError or
 * UnicodeDecodeError exception and returns -1 on error.
 */
static int
decode_code_page_errors(UINT code_page,
                        wchar_t **buf,
                        Py_ssize_t *bufsize,
                        const char *in, const int size,
                        const char *errors, int final)
{
    const char *startin = in;
    const char *endin = in + size;
    DWORD flags = MB_ERR_INVALID_CHARS;
    /* Ideally, we should get reason from FormatMessage. This is the Windows
       2000 English version of the message. */
    const char *reason = "No mapping for the Unicode character exists "
                         "in the target code page.";
    /* each step cannot decode more than 1 character, but a character can be
       represented as a surrogate pair */
    wchar_t buffer[2], *out;
    int insize;
    Py_ssize_t outsize;
    PyObject *errorHandler = NULL;
    PyObject *exc = NULL;
    PyObject *encoding_obj = NULL;
    const char *encoding;
    DWORD err;
    int ret = -1;

    assert(size > 0);

    encoding = code_page_name(code_page, &encoding_obj);
    if (encoding == NULL)
        return -1;

    if ((errors == NULL || strcmp(errors, "strict") == 0) && final) {
        /* The last error was ERROR_NO_UNICODE_TRANSLATION, then we raise a
           UnicodeDecodeError. */
        make_decode_exception(&exc, encoding, in, size, 0, 0, reason);
        if (exc != NULL) {
            PyCodec_StrictErrors(exc);
            Py_CLEAR(exc);
        }
        goto error;
    }

    /* Extend a wchar_t* buffer */
    Py_ssize_t n = *bufsize;   /* Get the current length */
    if (size > (PY_SSIZE_T_MAX - n) / (Py_ssize_t)Py_ARRAY_LENGTH(buffer)) {
        PyErr_NoMemory();
        goto error;
    }
    if (widechar_resize(buf, bufsize, n + size * Py_ARRAY_LENGTH(buffer)) < 0) {
        goto error;
    }
    out = *buf + n;

    /* Decode the byte string character per character */
    while (in < endin)
    {
        /* Decode a character */
        insize = 1;
        do
        {
            outsize = MultiByteToWideChar(code_page, flags,
                                          in, insize,
                                          buffer, Py_ARRAY_LENGTH(buffer));
            if (outsize > 0)
                break;
            err = GetLastError();
            if (err == ERROR_INVALID_FLAGS && flags) {
                /* For some code pages (e.g. UTF-7) flags must be set to 0. */
                flags = 0;
                continue;
            }
            if (err != ERROR_NO_UNICODE_TRANSLATION
                && err != ERROR_INSUFFICIENT_BUFFER)
            {
                PyErr_SetFromWindowsErr(0);
                goto error;
            }
            insize++;
        }
        /* 4=maximum length of a UTF-8 sequence */
        while (insize <= 4 && (in + insize) <= endin);

        if (outsize <= 0) {
            Py_ssize_t startinpos, endinpos, outpos;

            /* last character in partial decode? */
            if (in + insize >= endin && !final)
                break;

            startinpos = in - startin;
            endinpos = startinpos + 1;
            outpos = out - *buf;
            if (unicode_decode_call_errorhandler_wchar(
                    errors, &errorHandler,
                    encoding, reason,
                    &startin, &endin, &startinpos, &endinpos, &exc, &in,
                    buf, bufsize, &outpos))
            {
                goto error;
            }
            out = *buf + outpos;
        }
        else {
            in += insize;
            memcpy(out, buffer, outsize * sizeof(wchar_t));
            out += outsize;
        }
    }

    /* Shrink the buffer */
    assert(out - *buf <= *bufsize);
    *bufsize = out - *buf;
    /* (in - startin) <= size and size is an int */
    ret = Py_SAFE_DOWNCAST(in - startin, Py_ssize_t, int);

error:
    Py_XDECREF(encoding_obj);
    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    return ret;
}

static PyObject *
decode_code_page_stateful(int code_page,
                          const char *s, Py_ssize_t size,
                          const char *errors, Py_ssize_t *consumed)
{
    wchar_t *buf = NULL;
    Py_ssize_t bufsize = 0;
    int chunk_size, final, converted, done;

    if (code_page < 0) {
        PyErr_SetString(PyExc_ValueError, "invalid code page number");
        return NULL;
    }
    if (size < 0) {
        PyErr_BadInternalCall();
        return NULL;
    }

    if (consumed)
        *consumed = 0;

    do
    {
#ifdef NEED_RETRY
        if (size > DECODING_CHUNK_SIZE) {
            chunk_size = DECODING_CHUNK_SIZE;
            final = 0;
            done = 0;
        }
        else
#endif
        {
            chunk_size = (int)size;
            final = (consumed == NULL);
            done = 1;
        }

        if (chunk_size == 0 && done) {
            if (buf != NULL)
                break;
            _Py_RETURN_UNICODE_EMPTY();
        }

        converted = decode_code_page_strict(code_page, &buf, &bufsize,
                                            s, chunk_size);
        if (converted == -2)
            converted = decode_code_page_errors(code_page, &buf, &bufsize,
                                                s, chunk_size,
                                                errors, final);
        assert(converted != 0 || done);

        if (converted < 0) {
            PyMem_Free(buf);
            return NULL;
        }

        if (consumed)
            *consumed += converted;

        s += converted;
        size -= converted;
    } while (!done);

    PyObject *v = PyUnicode_FromWideChar(buf, bufsize);
    PyMem_Free(buf);
    return v;
}

PyObject *
PyUnicode_DecodeCodePageStateful(int code_page,
                                 const char *s,
                                 Py_ssize_t size,
                                 const char *errors,
                                 Py_ssize_t *consumed)
{
    return decode_code_page_stateful(code_page, s, size, errors, consumed);
}

PyObject *
PyUnicode_DecodeMBCSStateful(const char *s,
                             Py_ssize_t size,
                             const char *errors,
                             Py_ssize_t *consumed)
{
    return decode_code_page_stateful(CP_ACP, s, size, errors, consumed);
}

PyObject *
PyUnicode_DecodeMBCS(const char *s,
                     Py_ssize_t size,
                     const char *errors)
{
    return PyUnicode_DecodeMBCSStateful(s, size, errors, NULL);
}

static DWORD
encode_code_page_flags(UINT code_page, const char *errors)
{
    if (code_page == CP_UTF8) {
        return WC_ERR_INVALID_CHARS;
    }
    else if (code_page == CP_UTF7) {
        /* CP_UTF7 only supports flags=0 */
        return 0;
    }
    else {
        if (errors != NULL && strcmp(errors, "replace") == 0)
            return 0;
        else
            return WC_NO_BEST_FIT_CHARS;
    }
}

/*
 * Encode a Unicode string to a Windows code page into a byte string in strict
 * mode.
 *
 * Returns consumed characters if succeed, returns -2 on encode error, or raise
 * an OSError and returns -1 on other error.
 */
static int
encode_code_page_strict(UINT code_page, PyObject **outbytes,
                        PyObject *unicode, Py_ssize_t offset, int len,
                        const char* errors)
{
    BOOL usedDefaultChar = FALSE;
    BOOL *pusedDefaultChar = &usedDefaultChar;
    int outsize;
    wchar_t *p;
    Py_ssize_t size;
    const DWORD flags = encode_code_page_flags(code_page, NULL);
    char *out;
    /* Create a substring so that we can get the UTF-16 representation
       of just the slice under consideration. */
    PyObject *substring;
    int ret = -1;

    assert(len > 0);

    if (code_page != CP_UTF8 && code_page != CP_UTF7)
        pusedDefaultChar = &usedDefaultChar;
    else
        pusedDefaultChar = NULL;

    substring = PyUnicode_Substring(unicode, offset, offset+len);
    if (substring == NULL)
        return -1;
#if USE_UNICODE_WCHAR_CACHE
_Py_COMP_DIAG_PUSH
_Py_COMP_DIAG_IGNORE_DEPR_DECLS
    p = PyUnicode_AsUnicodeAndSize(substring, &size);
    if (p == NULL) {
        Py_DECREF(substring);
        return -1;
    }
_Py_COMP_DIAG_POP
#else /* USE_UNICODE_WCHAR_CACHE */
    p = PyUnicode_AsWideCharString(substring, &size);
    Py_CLEAR(substring);
    if (p == NULL) {
        return -1;
    }
#endif /* USE_UNICODE_WCHAR_CACHE */
    assert(size <= INT_MAX);

    /* First get the size of the result */
    outsize = WideCharToMultiByte(code_page, flags,
                                  p, (int)size,
                                  NULL, 0,
                                  NULL, pusedDefaultChar);
    if (outsize <= 0)
        goto error;
    /* If we used a default char, then we failed! */
    if (pusedDefaultChar && *pusedDefaultChar) {
        ret = -2;
        goto done;
    }

    if (*outbytes == NULL) {
        /* Create string object */
        *outbytes = PyBytes_FromStringAndSize(NULL, outsize);
        if (*outbytes == NULL) {
            goto done;
        }
        out = PyBytes_AS_STRING(*outbytes);
    }
    else {
        /* Extend string object */
        const Py_ssize_t n = PyBytes_Size(*outbytes);
        if (outsize > PY_SSIZE_T_MAX - n) {
            PyErr_NoMemory();
            goto done;
        }
        if (_PyBytes_Resize(outbytes, n + outsize) < 0) {
            goto done;
        }
        out = PyBytes_AS_STRING(*outbytes) + n;
    }

    /* Do the conversion */
    outsize = WideCharToMultiByte(code_page, flags,
                                  p, (int)size,
                                  out, outsize,
                                  NULL, pusedDefaultChar);
    if (outsize <= 0)
        goto error;
    if (pusedDefaultChar && *pusedDefaultChar) {
        ret = -2;
        goto done;
    }
    ret = 0;

done:
#if USE_UNICODE_WCHAR_CACHE
    Py_DECREF(substring);
#else /* USE_UNICODE_WCHAR_CACHE */
    PyMem_Free(p);
#endif /* USE_UNICODE_WCHAR_CACHE */
    return ret;

error:
    if (GetLastError() == ERROR_NO_UNICODE_TRANSLATION) {
        ret = -2;
        goto done;
    }
    PyErr_SetFromWindowsErr(0);
    goto done;
}

/*
 * Encode a Unicode string to a Windows code page into a byte string using an
 * error handler.
 *
 * Returns consumed characters if succeed, or raise an OSError and returns
 * -1 on other error.
 */
static int
encode_code_page_errors(UINT code_page, PyObject **outbytes,
                        PyObject *unicode, Py_ssize_t unicode_offset,
                        Py_ssize_t insize, const char* errors)
{
    const DWORD flags = encode_code_page_flags(code_page, errors);
    Py_ssize_t pos = unicode_offset;
    Py_ssize_t endin = unicode_offset + insize;
    /* Ideally, we should get reason from FormatMessage. This is the Windows
       2000 English version of the message. */
    const char *reason = "invalid character";
    /* 4=maximum length of a UTF-8 sequence */
    char buffer[4];
    BOOL usedDefaultChar = FALSE, *pusedDefaultChar;
    Py_ssize_t outsize;
    char *out;
    PyObject *errorHandler = NULL;
    PyObject *exc = NULL;
    PyObject *encoding_obj = NULL;
    const char *encoding;
    Py_ssize_t newpos, newoutsize;
    PyObject *rep;
    int ret = -1;

    assert(insize > 0);

    encoding = code_page_name(code_page, &encoding_obj);
    if (encoding == NULL)
        return -1;

    if (errors == NULL || strcmp(errors, "strict") == 0) {
        /* The last error was ERROR_NO_UNICODE_TRANSLATION,
           then we raise a UnicodeEncodeError. */
        make_encode_exception(&exc, encoding, unicode, 0, 0, reason);
        if (exc != NULL) {
            PyCodec_StrictErrors(exc);
            Py_DECREF(exc);
        }
        Py_XDECREF(encoding_obj);
        return -1;
    }

    if (code_page != CP_UTF8 && code_page != CP_UTF7)
        pusedDefaultChar = &usedDefaultChar;
    else
        pusedDefaultChar = NULL;

    if (Py_ARRAY_LENGTH(buffer) > PY_SSIZE_T_MAX / insize) {
        PyErr_NoMemory();
        goto error;
    }
    outsize = insize * Py_ARRAY_LENGTH(buffer);

    if (*outbytes == NULL) {
        /* Create string object */
        *outbytes = PyBytes_FromStringAndSize(NULL, outsize);
        if (*outbytes == NULL)
            goto error;
        out = PyBytes_AS_STRING(*outbytes);
    }
    else {
        /* Extend string object */
        Py_ssize_t n = PyBytes_Size(*outbytes);
        if (n > PY_SSIZE_T_MAX - outsize) {
            PyErr_NoMemory();
            goto error;
        }
        if (_PyBytes_Resize(outbytes, n + outsize) < 0)
            goto error;
        out = PyBytes_AS_STRING(*outbytes) + n;
    }

    /* Encode the string character per character */
    while (pos < endin)
    {
        Py_UCS4 ch = PyUnicode_READ_CHAR(unicode, pos);
        wchar_t chars[2];
        int charsize;
        if (ch < 0x10000) {
            chars[0] = (wchar_t)ch;
            charsize = 1;
        }
        else {
            chars[0] = Py_UNICODE_HIGH_SURROGATE(ch);
            chars[1] = Py_UNICODE_LOW_SURROGATE(ch);
            charsize = 2;
        }

        outsize = WideCharToMultiByte(code_page, flags,
                                      chars, charsize,
                                      buffer, Py_ARRAY_LENGTH(buffer),
                                      NULL, pusedDefaultChar);
        if (outsize > 0) {
            if (pusedDefaultChar == NULL || !(*pusedDefaultChar))
            {
                pos++;
                memcpy(out, buffer, outsize);
                out += outsize;
                continue;
            }
        }
        else if (GetLastError() != ERROR_NO_UNICODE_TRANSLATION) {
            PyErr_SetFromWindowsErr(0);
            goto error;
        }

        rep = unicode_encode_call_errorhandler(
                  errors, &errorHandler, encoding, reason,
                  unicode, &exc,
                  pos, pos + 1, &newpos);
        if (rep == NULL)
            goto error;

        Py_ssize_t morebytes = pos - newpos;
        if (PyBytes_Check(rep)) {
            outsize = PyBytes_GET_SIZE(rep);
            morebytes += outsize;
            if (morebytes > 0) {
                Py_ssize_t offset = out - PyBytes_AS_STRING(*outbytes);
                newoutsize = PyBytes_GET_SIZE(*outbytes) + morebytes;
                if (_PyBytes_Resize(outbytes, newoutsize) < 0) {
                    Py_DECREF(rep);
                    goto error;
                }
                out = PyBytes_AS_STRING(*outbytes) + offset;
            }
            memcpy(out, PyBytes_AS_STRING(rep), outsize);
            out += outsize;
        }
        else {
            Py_ssize_t i;
            enum PyUnicode_Kind kind;
            const void *data;

            if (PyUnicode_READY(rep) == -1) {
                Py_DECREF(rep);
                goto error;
            }

            outsize = PyUnicode_GET_LENGTH(rep);
            morebytes += outsize;
            if (morebytes > 0) {
                Py_ssize_t offset = out - PyBytes_AS_STRING(*outbytes);
                newoutsize = PyBytes_GET_SIZE(*outbytes) + morebytes;
                if (_PyBytes_Resize(outbytes, newoutsize) < 0) {
                    Py_DECREF(rep);
                    goto error;
                }
                out = PyBytes_AS_STRING(*outbytes) + offset;
            }
            kind = PyUnicode_KIND(rep);
            data = PyUnicode_DATA(rep);
            for (i=0; i < outsize; i++) {
                Py_UCS4 ch = PyUnicode_READ(kind, data, i);
                if (ch > 127) {
                    raise_encode_exception(&exc,
                        encoding, unicode,
                        pos, pos + 1,
                        "unable to encode error handler result to ASCII");
                    Py_DECREF(rep);
                    goto error;
                }
                *out = (unsigned char)ch;
                out++;
            }
        }
        pos = newpos;
        Py_DECREF(rep);
    }
    /* write a NUL byte */
    *out = 0;
    outsize = out - PyBytes_AS_STRING(*outbytes);
    assert(outsize <= PyBytes_GET_SIZE(*outbytes));
    if (_PyBytes_Resize(outbytes, outsize) < 0)
        goto error;
    ret = 0;

error:
    Py_XDECREF(encoding_obj);
    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    return ret;
}

static PyObject *
encode_code_page(int code_page,
                 PyObject *unicode,
                 const char *errors)
{
    Py_ssize_t len;
    PyObject *outbytes = NULL;
    Py_ssize_t offset;
    int chunk_len, ret, done;

    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return NULL;
    }

    if (PyUnicode_READY(unicode) == -1)
        return NULL;
    len = PyUnicode_GET_LENGTH(unicode);

    if (code_page < 0) {
        PyErr_SetString(PyExc_ValueError, "invalid code page number");
        return NULL;
    }

    if (len == 0)
        return PyBytes_FromStringAndSize(NULL, 0);

    offset = 0;
    do
    {
#ifdef NEED_RETRY
        if (len > DECODING_CHUNK_SIZE) {
            chunk_len = DECODING_CHUNK_SIZE;
            done = 0;
        }
        else
#endif
        {
            chunk_len = (int)len;
            done = 1;
        }

        ret = encode_code_page_strict(code_page, &outbytes,
                                      unicode, offset, chunk_len,
                                      errors);
        if (ret == -2)
            ret = encode_code_page_errors(code_page, &outbytes,
                                          unicode, offset,
                                          chunk_len, errors);
        if (ret < 0) {
            Py_XDECREF(outbytes);
            return NULL;
        }

        offset += chunk_len;
        len -= chunk_len;
    } while (!done);

    return outbytes;
}

PyObject *
PyUnicode_EncodeMBCS(const Py_UNICODE *p,
                     Py_ssize_t size,
                     const char *errors)
{
    PyObject *unicode, *res;
    unicode = PyUnicode_FromWideChar(p, size);
    if (unicode == NULL)
        return NULL;
    res = encode_code_page(CP_ACP, unicode, errors);
    Py_DECREF(unicode);
    return res;
}

PyObject *
PyUnicode_EncodeCodePage(int code_page,
                         PyObject *unicode,
                         const char *errors)
{
    return encode_code_page(code_page, unicode, errors);
}

PyObject *
PyUnicode_AsMBCSString(PyObject *unicode)
{
    return PyUnicode_EncodeCodePage(CP_ACP, unicode, NULL);
}

#undef NEED_RETRY

#endif /* MS_WINDOWS */

/* --- Character Mapping Codec -------------------------------------------- */

static int
charmap_decode_string(const char *s,
                      Py_ssize_t size,
                      PyObject *mapping,
                      const char *errors,
                      _PyUnicodeWriter *writer)
{
    const char *starts = s;
    const char *e;
    Py_ssize_t startinpos, endinpos;
    PyObject *errorHandler = NULL, *exc = NULL;
    Py_ssize_t maplen;
    enum PyUnicode_Kind mapkind;
    const void *mapdata;
    Py_UCS4 x;
    unsigned char ch;

    if (PyUnicode_READY(mapping) == -1)
        return -1;

    maplen = PyUnicode_GET_LENGTH(mapping);
    mapdata = PyUnicode_DATA(mapping);
    mapkind = PyUnicode_KIND(mapping);

    e = s + size;

    if (mapkind == PyUnicode_1BYTE_KIND && maplen >= 256) {
        /* fast-path for cp037, cp500 and iso8859_1 encodings. iso8859_1
         * is disabled in encoding aliases, latin1 is preferred because
         * its implementation is faster. */
        const Py_UCS1 *mapdata_ucs1 = (const Py_UCS1 *)mapdata;
        Py_UCS1 *outdata = (Py_UCS1 *)writer->data;
        Py_UCS4 maxchar = writer->maxchar;

        assert (writer->kind == PyUnicode_1BYTE_KIND);
        while (s < e) {
            ch = *s;
            x = mapdata_ucs1[ch];
            if (x > maxchar) {
                if (_PyUnicodeWriter_Prepare(writer, 1, 0xff) == -1)
                    goto onError;
                maxchar = writer->maxchar;
                outdata = (Py_UCS1 *)writer->data;
            }
            outdata[writer->pos] = x;
            writer->pos++;
            ++s;
        }
        return 0;
    }

    while (s < e) {
        if (mapkind == PyUnicode_2BYTE_KIND && maplen >= 256) {
            enum PyUnicode_Kind outkind = writer->kind;
            const Py_UCS2 *mapdata_ucs2 = (const Py_UCS2 *)mapdata;
            if (outkind == PyUnicode_1BYTE_KIND) {
                Py_UCS1 *outdata = (Py_UCS1 *)writer->data;
                Py_UCS4 maxchar = writer->maxchar;
                while (s < e) {
                    ch = *s;
                    x = mapdata_ucs2[ch];
                    if (x > maxchar)
                        goto Error;
                    outdata[writer->pos] = x;
                    writer->pos++;
                    ++s;
                }
                break;
            }
            else if (outkind == PyUnicode_2BYTE_KIND) {
                Py_UCS2 *outdata = (Py_UCS2 *)writer->data;
                while (s < e) {
                    ch = *s;
                    x = mapdata_ucs2[ch];
                    if (x == 0xFFFE)
                        goto Error;
                    outdata[writer->pos] = x;
                    writer->pos++;
                    ++s;
                }
                break;
            }
        }
        ch = *s;

        if (ch < maplen)
            x = PyUnicode_READ(mapkind, mapdata, ch);
        else
            x = 0xfffe; /* invalid value */
Error:
        if (x == 0xfffe)
        {
            /* undefined mapping */
            startinpos = s-starts;
            endinpos = startinpos+1;
            if (unicode_decode_call_errorhandler_writer(
                    errors, &errorHandler,
                    "charmap", "character maps to <undefined>",
                    &starts, &e, &startinpos, &endinpos, &exc, &s,
                    writer)) {
                goto onError;
            }
            continue;
        }

        if (_PyUnicodeWriter_WriteCharInline(writer, x) < 0)
            goto onError;
        ++s;
    }
    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    return 0;

onError:
    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    return -1;
}

static int
charmap_decode_mapping(const char *s,
                       Py_ssize_t size,
                       PyObject *mapping,
                       const char *errors,
                       _PyUnicodeWriter *writer)
{
    const char *starts = s;
    const char *e;
    Py_ssize_t startinpos, endinpos;
    PyObject *errorHandler = NULL, *exc = NULL;
    unsigned char ch;
    PyObject *key, *item = NULL;

    e = s + size;

    while (s < e) {
        ch = *s;

        /* Get mapping (char ordinal -> integer, Unicode char or None) */
        key = PyLong_FromLong((long)ch);
        if (key == NULL)
            goto onError;

        item = PyObject_GetItem(mapping, key);
        Py_DECREF(key);
        if (item == NULL) {
            if (PyErr_ExceptionMatches(PyExc_LookupError)) {
                /* No mapping found means: mapping is undefined. */
                PyErr_Clear();
                goto Undefined;
            } else
                goto onError;
        }

        /* Apply mapping */
        if (item == Py_None)
            goto Undefined;
        if (PyLong_Check(item)) {
            long value = PyLong_AS_LONG(item);
            if (value == 0xFFFE)
                goto Undefined;
            if (value < 0 || value > MAX_UNICODE) {
                PyErr_Format(PyExc_TypeError,
                             "character mapping must be in range(0x%x)",
                             (unsigned long)MAX_UNICODE + 1);
                goto onError;
            }

            if (_PyUnicodeWriter_WriteCharInline(writer, value) < 0)
                goto onError;
        }
        else if (PyUnicode_Check(item)) {
            if (PyUnicode_READY(item) == -1)
                goto onError;
            if (PyUnicode_GET_LENGTH(item) == 1) {
                Py_UCS4 value = PyUnicode_READ_CHAR(item, 0);
                if (value == 0xFFFE)
                    goto Undefined;
                if (_PyUnicodeWriter_WriteCharInline(writer, value) < 0)
                    goto onError;
            }
            else {
                writer->overallocate = 1;
                if (_PyUnicodeWriter_WriteStr(writer, item) == -1)
                    goto onError;
            }
        }
        else {
            /* wrong return value */
            PyErr_SetString(PyExc_TypeError,
                            "character mapping must return integer, None or str");
            goto onError;
        }
        Py_CLEAR(item);
        ++s;
        continue;

Undefined:
        /* undefined mapping */
        Py_CLEAR(item);
        startinpos = s-starts;
        endinpos = startinpos+1;
        if (unicode_decode_call_errorhandler_writer(
                errors, &errorHandler,
                "charmap", "character maps to <undefined>",
                &starts, &e, &startinpos, &endinpos, &exc, &s,
                writer)) {
            goto onError;
        }
    }
    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    return 0;

onError:
    Py_XDECREF(item);
    Py_XDECREF(errorHandler);
    Py_XDECREF(exc);
    return -1;
}

PyObject *
PyUnicode_DecodeCharmap(const char *s,
                        Py_ssize_t size,
                        PyObject *mapping,
                        const char *errors)
{
    _PyUnicodeWriter writer;

    /* Default to Latin-1 */
    if (mapping == NULL)
        return PyUnicode_DecodeLatin1(s, size, errors);

    if (size == 0)
        _Py_RETURN_UNICODE_EMPTY();
    _PyUnicodeWriter_Init(&writer);
    writer.min_length = size;
    if (_PyUnicodeWriter_Prepare(&writer, writer.min_length, 127) == -1)
        goto onError;

    if (PyUnicode_CheckExact(mapping)) {
        if (charmap_decode_string(s, size, mapping, errors, &writer) < 0)
            goto onError;
    }
    else {
        if (charmap_decode_mapping(s, size, mapping, errors, &writer) < 0)
            goto onError;
    }
    return _PyUnicodeWriter_Finish(&writer);

  onError:
    _PyUnicodeWriter_Dealloc(&writer);
    return NULL;
}

/* Charmap encoding: the lookup table */

struct encoding_map {
    PyObject_HEAD
    unsigned char level1[32];
    int count2, count3;
    unsigned char level23[1];
};

static PyObject*
encoding_map_size(PyObject *obj, PyObject* args)
{
    struct encoding_map *map = (struct encoding_map*)obj;
    return PyLong_FromLong(sizeof(*map) - 1 + 16*map->count2 +
                           128*map->count3);
}

static PyMethodDef encoding_map_methods[] = {
    {"size", encoding_map_size, METH_NOARGS,
     PyDoc_STR("Return the size (in bytes) of this object") },
    { 0 }
};

static PyTypeObject EncodingMapType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "EncodingMap",          /*tp_name*/
    sizeof(struct encoding_map),   /*tp_basicsize*/
    0,                      /*tp_itemsize*/
    /* methods */
    0,                      /*tp_dealloc*/
    0,                      /*tp_vectorcall_offset*/
    0,                      /*tp_getattr*/
    0,                      /*tp_setattr*/
    0,                      /*tp_as_async*/
    0,                      /*tp_repr*/
    0,                      /*tp_as_number*/
    0,                      /*tp_as_sequence*/
    0,                      /*tp_as_mapping*/
    0,                      /*tp_hash*/
    0,                      /*tp_call*/
    0,                      /*tp_str*/
    0,                      /*tp_getattro*/
    0,                      /*tp_setattro*/
    0,                      /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,     /*tp_flags*/
    0,                      /*tp_doc*/
    0,                      /*tp_traverse*/
    0,                      /*tp_clear*/
    0,                      /*tp_richcompare*/
    0,                      /*tp_weaklistoffset*/
    0,                      /*tp_iter*/
    0,                      /*tp_iternext*/
    encoding_map_methods,   /*tp_methods*/
    0,                      /*tp_members*/
    0,                      /*tp_getset*/
    0,                      /*tp_base*/
    0,                      /*tp_dict*/
    0,                      /*tp_descr_get*/
    0,                      /*tp_descr_set*/
    0,                      /*tp_dictoffset*/
    0,                      /*tp_init*/
    0,                      /*tp_alloc*/
    0,                      /*tp_new*/
    0,                      /*tp_free*/
    0,                      /*tp_is_gc*/
};

PyObject*
PyUnicode_BuildEncodingMap(PyObject* string)
{
    PyObject *result;
    struct encoding_map *mresult;
    int i;
    int need_dict = 0;
    unsigned char level1[32];
    unsigned char level2[512];
    unsigned char *mlevel1, *mlevel2, *mlevel3;
    int count2 = 0, count3 = 0;
    int kind;
    const void *data;
    Py_ssize_t length;
    Py_UCS4 ch;

    if (!PyUnicode_Check(string) || !PyUnicode_GET_LENGTH(string)) {
        PyErr_BadArgument();
        return NULL;
    }
    kind = PyUnicode_KIND(string);
    data = PyUnicode_DATA(string);
    length = PyUnicode_GET_LENGTH(string);
    length = Py_MIN(length, 256);
    memset(level1, 0xFF, sizeof level1);
    memset(level2, 0xFF, sizeof level2);

    /* If there isn't a one-to-one mapping of NULL to \0,
       or if there are non-BMP characters, we need to use
       a mapping dictionary. */
    if (PyUnicode_READ(kind, data, 0) != 0)
        need_dict = 1;
    for (i = 1; i < length; i++) {
        int l1, l2;
        ch = PyUnicode_READ(kind, data, i);
        if (ch == 0 || ch > 0xFFFF) {
            need_dict = 1;
            break;
        }
        if (ch == 0xFFFE)
            /* unmapped character */
            continue;
        l1 = ch >> 11;
        l2 = ch >> 7;
        if (level1[l1] == 0xFF)
            level1[l1] = count2++;
        if (level2[l2] == 0xFF)
            level2[l2] = count3++;
    }

    if (count2 >= 0xFF || count3 >= 0xFF)
        need_dict = 1;

    if (need_dict) {
        PyObject *result = PyDict_New();
        PyObject *key, *value;
        if (!result)
            return NULL;
        for (i = 0; i < length; i++) {
            key = PyLong_FromLong(PyUnicode_READ(kind, data, i));
            value = PyLong_FromLong(i);
            if (!key || !value)
                goto failed1;
            if (PyDict_SetItem(result, key, value) == -1)
                goto failed1;
            Py_DECREF(key);
            Py_DECREF(value);
        }
        return result;
      failed1:
        Py_XDECREF(key);
        Py_XDECREF(value);
        Py_DECREF(result);
        return NULL;
    }

    /* Create a three-level trie */
    result = PyObject_Malloc(sizeof(struct enc
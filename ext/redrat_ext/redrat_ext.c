#include "redrat_ext.h"

#define REDRAT_DEBUG 1

#ifdef REDRAT_DEBUG
#define Assert(x)                                                             \
    do {                                                                      \
        bool boog = !(x);                                                     \
                                                                              \
        if (boog)                                                             \
            rb_bug("redrat_ext: Assertion failure on %s:%d",                  \
                   __FILE__, __LINE__);                                       \
    } while (0)

#else /* REDRAT_DEBUG */
#define Assert(x) do { } while (0)
#endif /* REDRAT_DEBUG */

#define Assert_PyList(o)        Assert(PyList_Check(o)     == 1)
#define Assert_PyMapping(o)     Assert(PyMapping_Check(o)  == 1)
#define Assert_PySequence(o)    Assert(PySequence_Check(o) == 1)
#define Assert_PyString(o)      Assert(PyString_Check(o)   == 1)

/* Tests to see if a Ruby VALUE is of the PythonValue type */
#define REDRAT_PYTHONVALUE_P(rVal) (CLASS_OF(rVal) == rb_cPythonValue)

/*
 * A handy macro to jump to the presumed-existing py_rb_error label which is
 * intended to do any cleanup and then call rb_raise.  rb_raise can then
 */
#define REDRAT_ERRJMP_PYEXC(rExc, pVal)                                       \
    do {                                                                      \
        if ((pVal) == NULL)                                                   \
        {                                                                     \
            (rExc) = redrat_exception_convert();                              \
                                                                              \
            if ((rExc) != Qnil)                                               \
                goto py_rb_error;                                             \
        }                                                                     \
    } while (0)

/*
 * Macro to generate prototypes for Python comparison operations, this is
 * reused by redrat_compare_generate to generate the function bodies.
 */
#define redrat_prototype_generate(OP)                                         \
    static VALUE                                                              \
    redrat_ruby_python_##OP(VALUE self, VALUE other)                          \

redrat_prototype_generate(LT);
redrat_prototype_generate(LE);
redrat_prototype_generate(EQ);
redrat_prototype_generate(NE);
redrat_prototype_generate(GT);
redrat_prototype_generate(GE);

/* Internal function definitions */
static void redrat_py_decref_wrap(PyObject *freeing);
static VALUE redrat_ruby_handoff(PyObject *gced_by_ruby);
static VALUE redrat_exception_convert();
static PyObject *redrat_ruby_string_to_python(VALUE rStr);
static PyObject *redrat_ruby_symbol_to_python_string(VALUE rSym);
static VALUE redrat_ruby_delegate_python(int argc, VALUE *argv, VALUE self);
static VALUE redrat_builtin_mapping(VALUE self, VALUE builtin_name);
static VALUE redrat_apply(int argc, VALUE *argv, VALUE self);

/*
 * GLOBAL STATE
 *
 * These variables hold basic concepts exposed to the Ruby runtime.  If a Ruby
 * ever supports multiple interpreter states (passing that information to
 * something like Init_redrat_ext), then these should occupy a struct that can
 * be passed to every extension function.
 */

/* The RedRat Module */
static VALUE rb_mRedRat;

/* The RedRat::PythonValue class */
static VALUE rb_cPythonValue;

/* The RedRat::RedRatException class */
static VALUE rb_eRedRatException;

/*
 * INTERNAL PROCEDURE DEFINITIONS
 *
 * These procedures have no obvious exposure to Ruby, and could be classified
 * as internal mechanics.
 */

/*
 * redrat_py_decref_wrap - Work around Py_DECREF being a macro
 *
 * This enables function pointer passing for Ruby GC.
 */
static void
redrat_py_decref_wrap(PyObject *freeing)
{
    PyGILState_STATE gstate = PyGILState_Ensure();

    Py_DECREF(freeing);
    PyGILState_Release(gstate);
}

/*
 * redrat_ruby_handoff - Hands off a PyObject to Ruby
 *
 * This procedure presumes that the Python GIL is already held.
 *
 * This includes the hybridization of Ruby and Python GC.  To do this, Ruby
 * will get its own reference (Py_INCREF) and registers a callback that exists
 * to call Py_DECREF.  In this way, a PythonValue that has no remaining
 * references in the Python runtime will still not be freed, since Ruby has not
 * GCed it yet.
 */
static VALUE
redrat_ruby_handoff(PyObject *gced_by_ruby)
{
    Py_INCREF(gced_by_ruby);

    return Data_Wrap_Struct(rb_cPythonValue,
                            NULL, redrat_py_decref_wrap,
                            gced_by_ruby);
}

/*
 * redrat_exception_convert - Convert Python exceptions to Ruby exceptions
 *
 * This procedure presumes that the Python GIL is already held.
 *
 * The convention in the Python C API is to allow NULL-valued PyObject* values
 * flow through the system while an out-of-band error state is left intact.
 * Before returning control to MRI, determine if it is necessary to raise a
 * Ruby exception or not.
 */
static VALUE
redrat_exception_convert()
{
    PyObject *pExc = PyErr_Occurred();

    if (pExc != NULL)
    {
        VALUE rExc;
        PyObject *pExceptionString = PyObject_Str(pExc);

        if (pExceptionString == NULL)
            rb_raise(rb_eRedRatException,
                     "could not retrieve Python exception error message");
        else
        {
            Assert_PyString(pExceptionString);
            rb_str_new2(PyString_AsString(pExceptionString));
        }

        rExc = rb_exc_new2(rb_eRedRatException, "Python Exception");
        rb_iv_set(rExc, "python_exception", redrat_ruby_handoff(pExc));

        PyErr_Clear();
        return rExc;
    }
    else
    {
        Assert(pExc == NULL);

        rb_bug("redrat_ext: expected Python error state, "
               "but no error state was found");
    }
}

/* XXX: Total and possibly exploitable ignorance of unicode encodings. */

/*
 * This procedure presumes that the Python GIL is already held.
 */
static PyObject *
redrat_ruby_string_to_python(VALUE rStr)
{
    return PyUnicode_FromString(RSTRING_PTR(rStr));
}

/*
 * This procedure presumes that the Python GIL is already held.
 */
static PyObject *
redrat_ruby_symbol_to_python_string(VALUE rSym)
{
    return redrat_ruby_string_to_python(rb_id2str(SYM2ID(rSym)));
}

/*
 * RUBY INTERFACE PROCEDURES
 *
 * These procedures have direct impact on what constructs are exposed to a Ruby
 * program.
 */

/*
 * redrat_ruby_delegate_python - Delegate Ruby messages to Python
 *
 * This has the distinction of only supporting textual types.  If one wants a
 * Python integer or list, one should instead construct values from unicode
 * literals and then use the resulting PythonValues.  The goal is to avoid the
 * datatype-conversion tarpit as a semantic crutch -- if such conversions ever
 * become added, it is hopefully clear as an optimization rather than a
 * necessary mechanic.
 */
static VALUE
redrat_ruby_delegate_python(int argc, VALUE *argv, VALUE self)
{
    PyGILState_STATE    gstate;

    VALUE       rExcFromStringCoercion = Qnil;
    VALUE       rExcFromDelegation     = Qnil;
    VALUE       rSym;

    PyObject    *pMessageName = NULL;
    PyObject    *pSelf        = NULL;
    PyObject    *pResult      = NULL;

    VALUE       rResult;

    if (argc > 1)
        rb_raise(rb_eArgError,
                 "redrat_ext: only instance variable accesses are allowed");

    rSym = argv[0];
    Assert(SYMBOL_P(rSym));

    gstate = PyGILState_Ensure();

    pMessageName = redrat_ruby_symbol_to_python_string(rSym);
    REDRAT_ERRJMP_PYEXC(rExcFromStringCoercion, pMessageName);

    Data_Get_Struct(self, PyObject, pSelf);
    Py_INCREF(pSelf);

    pResult = PyObject_GenericGetAttr(pSelf, pMessageName);
    REDRAT_ERRJMP_PYEXC(rExcFromDelegation, pResult);

    rResult = redrat_ruby_handoff(pResult);
    Py_DECREF(pSelf);

    PyGILState_Release(gstate);

    return rResult;

py_rb_error:
    Py_XDECREF(pMessageName);
    Py_XDECREF(pSelf);
    Py_XDECREF(pResult);

    PyGILState_Release(gstate);

    if (rExcFromDelegation != Qnil)
        rb_raise(rExcFromDelegation,
                 "redrat_ext: Could not delegate to Python");
    else if (rExcFromStringCoercion != Qnil)
        rb_raise(rExcFromStringCoercion,
                 "redrat_ext: Could not convert Ruby symbol "
                 "for delegation to Python");

    Assert(false);
}

/*
 * redrat_builtin_mapping - The Root of All Things
 *
 * Retrieves a hash of Python builtins, from names (strings) to values
 * (PythonValues).  This is akin to executing __builtins__.__dict__ in Python.
 *
 * From here, just about everything is possible because of the existence of
 * __import__.  An annotated example of what this C code is intended to enable
 * follows:
 *
 *     # Access the builtins, available as a dictionary
 *
 *     >>> __builtins__
 *     <module '__builtin__' (built-in)>
 *
 *     # __import__ is the low-level method used for importing modules
 *
 *     >>> __builtins__.__import__
 *     <built-in function __import__>
 *
 *     # Any available module can be imported, yielding another module
 *     # PyObject; furthermore, those modules can be delegated messages from
 *     # Ruby, as Python modules are just PyObjects.
 *
 *     >>> __builtins__.__import__('argparse')
 *     <module 'argparse' from '/usr/lib/python2.7/argparse.pyc'>
 */
static VALUE
redrat_builtin_mapping(VALUE self, VALUE builtin_name)
{
    PyGILState_STATE     gstate;

    PyObject    *pBuiltins;
    PyObject    *pBuiltinItems = NULL;

    VALUE       rExcGetBuiltin   = Qnil;
    VALUE       rExcMappingItems = Qnil;
    VALUE       rExcPairGet      = Qnil;
    VALUE       rExcPairUnpack   = Qnil;
    VALUE       rExcUnstringKey  = Qnil;

    VALUE       rHashBuiltins = rb_hash_new();

    /* For iteration over the builtins */
    Py_ssize_t           item_len;
    Py_ssize_t           item_iter;
    PyObject            *pair       = NULL;
    PyObject            *key        = NULL;
    PyObject            *value      = NULL;

    gstate = PyGILState_Ensure();

    pBuiltins = PyEval_GetBuiltins();
    REDRAT_ERRJMP_PYEXC(rExcGetBuiltin, pBuiltins);
    Py_INCREF(pBuiltins);

    /* Extract the items from the mapping */
    Assert_PyMapping(pBuiltins);
    pBuiltinItems = PyMapping_Items(pBuiltins);
    REDRAT_ERRJMP_PYEXC(rExcMappingItems, pBuiltinItems);

    item_len = PySequence_Length(pBuiltinItems);

    for (item_iter = 0; item_iter < item_len; item_iter += 1)
    {
        VALUE rStrKey;
        VALUE rPyValue;

        pair = PySequence_GetItem(pBuiltinItems, item_iter);
        REDRAT_ERRJMP_PYEXC(rExcPairGet, pair);

        key = PySequence_GetItem(pair, 0);
        REDRAT_ERRJMP_PYEXC(rExcPairUnpack, key);

        if (PyString_Check(key))
            rStrKey = rb_str_new2(PyString_AsString(key));
        else
        {

            REDRAT_ERRJMP_PYEXC(rExcUnstringKey, key);
        }

        value = PySequence_GetItem(pair, 1);
        REDRAT_ERRJMP_PYEXC(rExcPairUnpack, value);

        rPyValue = redrat_ruby_handoff(value);
        rb_hash_aset(rHashBuiltins, rStrKey, rPyValue);

        Py_DECREF(pair);
        Py_DECREF(key);
        Py_DECREF(value);
    }

    PyGILState_Release(gstate);

    return rHashBuiltins;

py_rb_error:
    /*
     * A failure to return the builtin mapping to Ruby, which makes it
     * seemingly impossible to bootstrap access to richer Python functionality.
     * That's bad news so early in the game, but faithfully clean up and raise
     * an exception.
     *
     * These Py_XDECREF calls have to be carefully checked that they do not
     * cause over-decref-ing should a procedure returns a "borrowed" reference
     * and yet winds up in this error path.  In the above, PyEval_GetBuiltins()
     * is such an example.
     */
    Py_XDECREF(pBuiltins);
    Py_XDECREF(pBuiltinItems);
    Py_XDECREF(pair);
    Py_XDECREF(key);
    Py_XDECREF(value);

    PyGILState_Release(gstate);

    if (rExcGetBuiltin != Qnil)
        rb_raise(rExcGetBuiltin,
                 "redrat_ext: couldn't read Python builtins, "
                 "RedRat cannot initialize");
    else if (rExcMappingItems != Qnil)
        rb_raise(rExcMappingItems,
                 "redrat_ext: couldn't retrieve items from Python builtins, "
                 "RedRat cannot initialize");
    else if (rExcPairGet != Qnil)
        rb_raise(rExcPairGet,
                 "redrat_ext: couldn't retrieve an item from Python builtins, "
                 "RedRat cannot initialize");
    else if (rExcUnstringKey != Qnil)
        rb_raise(rExcUnstringKey,
                 "redrat_ext: retrieved a non-stringlike key from Python, "
                 "RedRat cannot initialize");
    else if (rExcPairUnpack != Qnil)
        rb_raise(rExcPairUnpack,
                 "redrat_ext: couldn't unpack an item from the Python builtin "
                 "items, RedRat cannot initialize");
    else
        rb_fatal("redrat_ext: report this bug in redrat_builtin_mapping");

    Assert(false);
}

static VALUE
redrat_apply(int argc, VALUE *argv, VALUE self)
{
    PyGILState_STATE    gstate;

    PyObject *pMaybeCallable;
    PyObject *pArgs = NULL;
    PyObject *pResult;

    VALUE rExcStringConvert;
    VALUE rExcApplication;
    VALUE rResult;

    int argIter;

    /* Reject zero arguments */
    if (argc == 0)
        rb_raise(rb_eArgError,
                 "redrat_ext: apply must take at least one argument");

    /* Reject blocks */
    if (rb_block_given_p())
        rb_raise(rb_eArgError,
                 "redrat_ext: apply does not accept a block");

    /* Reject apply(NOT_A_PYTHONVALUE, ...) */
    if (!REDRAT_PYTHONVALUE_P(argv[0]))
        rb_raise(rb_eArgError,
                 "redrat_ext: apply must apply a PythonValue to zero or "
                 "more arguments");

    Data_Get_Struct(argv[0], PyObject, pMaybeCallable);

    gstate = PyGILState_Ensure();

    Py_INCREF(pMaybeCallable);

    /*
     * Gin up an argument tuple for the function.  Because the first element of
     * argv is the function itself, initialize the capacity accordingly.
     *
     * It's still possible for a type error to occur, but at this point it is
     * assumed that most calls are going to succeed, so start building the
     * argument list greedily anyway.
     */
    pArgs = PyTuple_New(argc - 1);

    for (argIter = 1; argIter < argc; argIter += 1)
    {
        const Py_ssize_t         tupleWritePosition = argIter - 1;
        const VALUE              rCurrentArg        = argv[argIter];
        PyObject                *pArg;

        Assert(PyTuple_Size(pArgs) > tupleWritePosition);

        switch (TYPE(rCurrentArg))
        {
            case T_STRING:
                pArg = redrat_ruby_string_to_python(rCurrentArg);
                REDRAT_ERRJMP_PYEXC(rExcStringConvert, pArg);
                PyTuple_SET_ITEM(pArgs, tupleWritePosition, pArg);
                break;
            case T_OBJECT:
                if (REDRAT_PYTHONVALUE_P(rCurrentArg))
                {
                    Data_Get_Struct(rCurrentArg, PyObject, pArg);
                    Py_INCREF(pArg);
                    PyTuple_SET_ITEM(pArgs, tupleWritePosition, pArg);
                    break;
                }

                /* Intentional fall-through */
            default:
                rb_raise(rb_eArgError,
                         "redrat_ext: Only Ruby Strings and PythonValues "
                         "are supported in RedRat apply");
        }
    }

    pResult = PyObject_Call(pMaybeCallable, pArgs, NULL);
    REDRAT_ERRJMP_PYEXC(rExcApplication, pResult);

    Py_DECREF(pMaybeCallable);
    Py_DECREF(pArgs);

    rResult = redrat_ruby_handoff(pResult);

    Py_DECREF(pResult);

    PyGILState_Release(gstate);
    return rResult;

py_rb_error:
    Py_XDECREF(pArgs);
    Py_XDECREF(pResult);

    PyGILState_Release(gstate);

    if (rExcApplication != Qnil)
        rb_raise(rExcApplication,
                 "redrat_ext: applied function raised an error");
    else if (rExcStringConvert != Qnil)
        rb_raise(rExcStringConvert,
                 "redrat_ext: could not convert a Ruby string to a "
                 "Python string during function application");

    Assert(false);
}

/*
 * Intermezzo: A bunch of operator overloads for common comparisons exposed to
 * Ruby.  Macros are used as a textual hack to make this more terse.
 */
#define redrat_compare_generate(OP)                                           \
    redrat_prototype_generate(OP)                                             \
    {                                                                         \
        PyGILState_STATE         gstate;                                      \
        PyObject                *pSelf;                                       \
        PyObject                *pOther;                                      \
        VALUE                    rTruth;                                      \
        VALUE                    rExc;                                        \
                                                                              \
        if (!REDRAT_PYTHONVALUE_P(self))                                      \
            rb_raise(rb_eTypeError,                                           \
                     "redrat_ext: self must be a PythonValue");               \
        else if (!REDRAT_PYTHONVALUE_P(other))                                \
            rb_raise(rb_eArgError,                                            \
                     "redrat_ext: compared value must be a PythonValue");     \
                                                                              \
        Data_Get_Struct(self, PyObject, pSelf);                               \
        Data_Get_Struct(other, PyObject, pOther);                             \
                                                                              \
        gstate = PyGILState_Ensure();                                         \
                                                                              \
        switch (PyObject_RichCompareBool(pSelf, pOther, Py_##OP))             \
        {                                                                     \
            case -1:                                                          \
                rExc = redrat_exception_convert();                            \
                if (rExc != Qnil)                                             \
                    goto py_rb_error;                                         \
                else                                                          \
                    Assert(false);                                            \
            case 0:                                                           \
                rTruth = Qfalse;                                              \
                break;                                                        \
            case 1:                                                           \
                rTruth = Qtrue;                                               \
                break;                                                        \
        }                                                                     \
                                                                              \
        PyGILState_Release(gstate);                                           \
                                                                              \
        return rTruth;                                                        \
                                                                              \
    py_rb_error:                                                              \
        PyGILState_Release(gstate);                                           \
                                                                              \
        if (rExc != Qnil)                                                     \
            rb_raise(rExc, "redrat_ext: error during comparison");            \
                                                                              \
        Assert(false);                                                        \
    }

redrat_compare_generate(LT);
redrat_compare_generate(LE);
redrat_compare_generate(EQ);
redrat_compare_generate(NE);
redrat_compare_generate(GT);
redrat_compare_generate(GE);

void
Init_redrat_ext()
{
    rb_mRedRat = rb_define_module("RedRat");

    rb_define_module_function(rb_mRedRat, "builtins",
                              redrat_builtin_mapping, 0);
    rb_define_module_function(rb_mRedRat, "apply", redrat_apply, -1);

    rb_cPythonValue = rb_define_class_under(
        rb_mRedRat, "PythonValue", rb_cObject);

    /* Comparison operator definitions */

    rb_define_method(rb_cPythonValue, "<",
                     redrat_ruby_python_LT, 1);
    rb_define_method(rb_cPythonValue, "<=",
                     redrat_ruby_python_LE, 1);
    rb_define_method(rb_cPythonValue, "==",
                     redrat_ruby_python_EQ, 1);
    rb_define_method(rb_cPythonValue, "!=",
                     redrat_ruby_python_NE, 1);
    rb_define_method(rb_cPythonValue, ">",
                     redrat_ruby_python_GT, 1);
    rb_define_method(rb_cPythonValue, ">=",
                     redrat_ruby_python_GE, 1);


    /* Used for all other kinds of message and values */
    rb_define_method(rb_cPythonValue, "method_missing",
                     redrat_ruby_delegate_python, -1);

    rb_eRedRatException = rb_define_class_under(rb_mRedRat, "RedRatException",
                                                rb_eStandardError);

    Py_Initialize();
}

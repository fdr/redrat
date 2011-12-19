#include "redrat_ext.h"

#define REDRAT_DEBUG 1
#define REDRAT_NO_GC 1

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
 * Macro to generate prototypes for Python functions that return strings.
 * These are mostly intended to help with display and debugging, since there is
 * actual type coercion to Ruby strings.
 *
 * Reused by redrat_stringify_generate.
 */
#define redrat_stringify_generate_prototype(lowcase)                          \
    static VALUE redrat_##lowcase(VALUE self, VALUE rPythonValue)

redrat_stringify_generate_prototype(repr);
redrat_stringify_generate_prototype(str);

/* Internal Ruby procedure definitions */
static void redrat_py_decref_wrap(PyObject *freeing);
static VALUE redrat_ruby_handoff(PyObject *gced_by_ruby);
static VALUE redrat_exception_convert();
static PyObject *redrat_ruby_string_to_python(VALUE rStr);
static PyObject *redrat_ruby_symbol_to_python_string(VALUE rSym);
static VALUE redrat_getattr(VALUE self, VALUE rTarget, VALUE rPyString);
static VALUE redrat_builtin_mapping(VALUE self);
static VALUE redrat_apply(int argc, VALUE *argv, VALUE self);
static VALUE redrat_truth(VALUE self, VALUE rVal);
static VALUE redrat_unicode(VALUE self, VALUE rVal);
static VALUE redrat_python_exception_getter(VALUE self);


/* Python definitions */

/* A redrat-supplied RubyObject as represented in Python */
typedef struct {
    PyObject_HEAD
    VALUE r;
} redrat_RubyObject;

/* Python procedure prototypes */
#ifndef PyMODINIT_FUNC
#define PyMODINIT_FUNC void
#endif

PyMODINIT_FUNC initredrat(void);

static void redrat_rubyobject_dealloc(redrat_RubyObject *self);
static PyObject *redrat_python_handoff(VALUE r);

/* The type instance for RubyObjects in Python */
static PyTypeObject redrat_RubyType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "redrat.RubyObject",       /*tp_name*/
    sizeof(redrat_RubyObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)redrat_rubyobject_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "redrat Ruby objects",     /* tp_doc */
};

/*
 * GLOBAL STATE
 *
 * These variables hold basic concepts exposed to the Ruby and Python runtimes.
 * If a Ruby ever supports sufficiently separated multiple interpreter states
 * (passing that information to something like Init_redrat_ext), then these
 * should occupy a struct that can be passed to every extension function.
 *
 * TODO: Right now sub-interpreters for Python are not supported.  That
 * limitation should be lifted, which will mean creating a new Object in Ruby
 * that calls the Py_NewInterpreter() function (let's call it
 * RedRat::Internal::PythonInterpreter), and holds a reference to the
 * subinterpreter, and each one of those should possess references to their
 * Python-loaded RubyValue and RubyException types.  It also means globally
 * accessed builtins like apply, repr, and str latch onto an interpreter state
 * instead singletons of the Module, as they do now.  Py_EndInterpreter will
 * need to be called upon the garbage collection of a PythonInterpreter
 * instance, and PyThreadState_Swap will need to be called when messages on
 * those PythonInterpreter instances are accessed.
 */

/* The RedRat Module */
static VALUE rb_mRedRat;

/* The RedRat::Internal Module */
static VALUE rb_mRedRatInternal;

/* The RedRat::Internal::PythonValue class */
static VALUE rb_cPythonValue;

/* The RedRat::Internal::RedRatException class */
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

#ifndef REDRAT_NO_GC
    Py_DECREF(freeing);
#endif
    PyGILState_Release(gstate);
}

/*
 * redrat_ruby_handoff - Hands off a PyObject to Ruby
 *
 * If this PyObject is of type redrat.RubyObject, then just return the
 * unwrapped Ruby object inside.
 *
 * This procedure presumes that the Python GIL and Ruby GILs are already held.
 *
 * This includes the hybridization of Ruby and Python GC.  To do this, Ruby
 * will get its own reference (Py_INCREF) and registers a callback that exists
 * to call Py_DECREF.  In this way, a PythonValue that has no remaining
 * references in the Python runtime will still not be freed, since Ruby has not
 * GCed it yet.
 */
static VALUE
redrat_ruby_handoff(PyObject *handing_off)
{
    if (handing_off->ob_type == &redrat_RubyType)
        return ((redrat_RubyObject *) handing_off)->r;
    else
    {
        Py_INCREF(handing_off);

        return Data_Wrap_Struct(rb_cPythonValue,
                                NULL, redrat_py_decref_wrap,
                                handing_off);
    }
}

static void
redrat_rb_exc_raise(VALUE rExc, const char *reason)
{
    rb_iv_set(rExc, "@redrat_reason", rb_str_new2(reason));
    rb_exc_raise(rExc);
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
    PyObject *pType;
    PyObject *pValue;
    PyObject *pTraceback;

    PyErr_Fetch(&pType, &pValue, &pTraceback);

    if (pType != NULL)
    {
        VALUE rException;

        /* The trinity of Python exception information, see PyErr_Fetch */
        VALUE rpType;
        VALUE rpValue = Qnil;
        VALUE rpTraceback = Qnil;

        rpType = redrat_ruby_handoff(pType);

        if (pValue != NULL)
            rpValue = redrat_ruby_handoff(pValue);

        if (pTraceback != NULL)
            rpTraceback = redrat_ruby_handoff(pTraceback);

        rException = rb_exc_new2(rb_eRedRatException, "RedRat exception");
        rb_iv_set(rException, "@python_type", rpType);
        rb_iv_set(rException, "@python_value", rpValue);
        rb_iv_set(rException, "@python_traceback", rpTraceback);

        Py_DECREF(pType);
        Py_XDECREF(pValue);
        Py_XDECREF(pTraceback);

        PyErr_Clear();
        return rException;
    }
    else
    {
        Assert(pValue == NULL);

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
    char *stringval = (StringValueCStr(rStr));
    char *copy = malloc(sizeof(*stringval) * strlen(stringval));

    strcpy(copy, stringval);
    printf("redrat_ruby_string_to_python \"%s\"\n", copy);

    return PyUnicode_FromString(copy);
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
 * redrat_getattr - Get attributes from a PythonValue via a PythonValue
 *
 * Nominally the second argument will be constructed via the 'unicode' method
 * provided in this module.
 */
static VALUE
redrat_getattr(VALUE self, VALUE rTarget, VALUE rPyString)
{
    PyGILState_STATE    gstate;

    PyObject *pTarget;
    PyObject *pAttrName;
    PyObject *pResult = NULL;

    VALUE rExcFromDelegation;
    VALUE rResult;

    if (!(REDRAT_PYTHONVALUE_P(rTarget) && REDRAT_PYTHONVALUE_P(rPyString)))
        rb_raise(rb_eArgError,
                 "redrat_ext: getattr only supports PythonValues");

    gstate = PyGILState_Ensure();

    Data_Get_Struct(rTarget, PyObject, pTarget);
    Data_Get_Struct(rPyString, PyObject, pAttrName);

    pResult = PyObject_GenericGetAttr(pTarget, pAttrName);
    REDRAT_ERRJMP_PYEXC(rExcFromDelegation, pResult);
    rResult = redrat_ruby_handoff(pResult);

    PyGILState_Release(gstate);

    return rResult;

py_rb_error:
    PyGILState_Release(gstate);

    if (rExcFromDelegation != Qnil)
        redrat_rb_exc_raise(rExcFromDelegation,
                            "redrat_ext: Could not delegate to Python");

    Assert(false);
}

/*
 * redrat_builtin_mapping - The Root of All Things
 *
 * Retrieves a PythonValue that supports the dictionary protocol to access the
 * builtins.
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
redrat_builtin_mapping(VALUE self)
{
    PyGILState_STATE     gstate;

    PyObject *pBuiltins;

    VALUE rExcGetBuiltin = Qnil;
    VALUE rReturn;

    gstate = PyGILState_Ensure();

    pBuiltins = PyEval_GetBuiltins();
    REDRAT_ERRJMP_PYEXC(rExcGetBuiltin, pBuiltins);
    Py_INCREF(pBuiltins);
    Assert_PyMapping(pBuiltins);

    rReturn = redrat_ruby_handoff(pBuiltins);

    PyGILState_Release(gstate);

    return rReturn;

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

    PyGILState_Release(gstate);

    if (rExcGetBuiltin != Qnil)
        redrat_rb_exc_raise(rExcGetBuiltin,
                            "redrat_ext: couldn't read Python builtins, "
                            "RedRat cannot initialize");
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

    VALUE rExcStringConvert = Qnil;
    VALUE rExcApplication   = Qnil;
    VALUE rResult;

    int argIter;

    /* Reject zero arguments */
    if (argc == 0)
        rb_raise(rb_eArgError,
                 "redrat_ext: apply must take at least one argument");

    gstate = PyGILState_Ensure();

    pMaybeCallable = redrat_python_handoff(argv[0]);
    Py_INCREF(pMaybeCallable);

    printf("fdr: about to do function application\n");

    /*
     * Gin up an argument tuple for the function.
     *
     * The first Ruby argument to this function is not counted towards this, so
     * adjust the tuple size accordingly.
     */
    pArgs = PyTuple_New(argc - 1);

    for (argIter = 1; argIter < argc; argIter += 1)
    {
        const Py_ssize_t         tupleWritePosition = argIter - 1;
        const VALUE              rCurrentArg        = argv[argIter];
        PyObject                *pArg;

        Assert(PyTuple_Size(pArgs) > tupleWritePosition);

        pArg = redrat_python_handoff(rCurrentArg);

        PyTuple_SET_ITEM(pArgs, tupleWritePosition, pArg);
    }

    printf("fdr: trying function application\n");
    pResult = PyObject_Call(pMaybeCallable, pArgs, NULL);
    REDRAT_ERRJMP_PYEXC(rExcApplication, pResult);

    rResult = redrat_ruby_handoff(pResult);

    Py_DECREF(pMaybeCallable);
    Py_DECREF(pArgs);
    Py_DECREF(pResult);

    PyGILState_Release(gstate);
    return rResult;

py_rb_error:
    Py_XDECREF(pArgs);
    Py_XDECREF(pResult);

    PyGILState_Release(gstate);

    if (rExcApplication != Qnil)
        redrat_rb_exc_raise(rExcApplication,
                            "redrat_ext: applied function raised an error");
    else if (rExcStringConvert != Qnil)
        redrat_rb_exc_raise(rExcStringConvert,
                            "redrat_ext: could not convert a Ruby string to a "
                            "Python string during function application");

    Assert(false);
}

static VALUE
redrat_truth(VALUE self, VALUE rVal)
{
    PyGILState_STATE gstate;

    PyObject *pVal;
    VALUE     rTruth;

    if (!REDRAT_PYTHONVALUE_P(rVal))
        rb_raise(rb_eArgError,
                 "redrat_ext: truth can only accept a PythonValue");

    Assert(REDRAT_PYTHONVALUE_P(rVal));
    Data_Get_Struct(rVal, PyObject, pVal);

    gstate = PyGILState_Ensure();

    switch (PyObject_Not(pVal))
    {
        case -1:
            goto py_rb_error;
        case 0:
            rTruth = Qtrue;
            break;
        case 1:
            rTruth = Qfalse;
            break;
        default:
            Assert(false);
            rb_fatal("redrat_ext: received totally unexpected return code in "
                     "redrat_truth");
            rTruth = Qnil;
    }

    PyGILState_Release(gstate);

    return rTruth;

py_rb_error:
    {
        VALUE     rExc;

        /*
         * Assumption: getting here means that a Python Exception will be
         * found.  That may be a false one, though, and in general this
         * function will screw up the GIL state when that happens.
         */
        rExc = redrat_exception_convert();
        PyErr_Clear();

        PyGILState_Release(gstate);

        redrat_rb_exc_raise(
            rExc, "redrat_ext: could not compute truth value for PythonValue");
        Assert(false);
    }
}

static VALUE
redrat_unicode(VALUE self, VALUE rVal)
{
    if (TYPE(rVal) == T_STRING)
    {
        PyGILState_STATE gstate = PyGILState_Ensure();

        PyObject *pUnicode;
        VALUE     rExcPythonUnicode = Qnil;
        VALUE     rRetVal;

        pUnicode = redrat_ruby_string_to_python(rVal);
        REDRAT_ERRJMP_PYEXC(rExcPythonUnicode, pUnicode);

        rRetVal = redrat_ruby_handoff(pUnicode);
        Py_DECREF(pUnicode);

        PyGILState_Release(gstate);

        return rRetVal;

    py_rb_error:
        /*
         * This is the only error path, so this should never be non-NULL, yet
         * do the normal XDECREF handling for idiom's sake.
         */
        Assert(pUnicode == NULL);
        Py_XDECREF(pUnicode);

        PyGILState_Release(gstate);

        /*
         * There is only one error path, so this must be the exception
         * condition raised, but follow the usual error handling protocol
         * anyway, which involves checking the exception for Nillity.
         */
        Assert(rExcPythonUnicode != Qnil);
        if (rExcPythonUnicode != Qnil)
            redrat_rb_exc_raise(
                rExcPythonUnicode,
                "redrat_exc: could not construct PythonValue from "
                "Ruby String");

        Assert(false);
    }
    else
        rb_raise(rb_eArgError,
                 "redrat_ext: Only Ruby Strings are accepted when "
                 "constructing Python Unicode");
}

#define redrat_stringify_generate(lowcase, upcase)                            \
    static VALUE                                                              \
    redrat_##lowcase(VALUE self, VALUE rPythonValue)                          \
    {                                                                         \
        PyGILState_STATE  gstate;                                             \
        PyObject         *pThing;                                             \
        PyObject         *pString = NULL;                                     \
        VALUE             r;                                                  \
        VALUE             rExcCant;                                           \
                                                                              \
        Data_Get_Struct(rPythonValue, PyObject, pThing);                      \
                                                                              \
        gstate = PyGILState_Ensure();                                         \
                                                                              \
        pString = PyObject_##upcase(pThing);                                  \
        REDRAT_ERRJMP_PYEXC(rExcCant, pString);                               \
        Assert(PyString_Check(pString));                                      \
        r = rb_str_new2(PyString_AsString(pString));                          \
        Py_DECREF(pString);                                                   \
                                                                              \
        PyGILState_Release(gstate);                                           \
                                                                              \
        return r;                                                             \
                                                                              \
    py_rb_error:                                                              \
        Py_XDECREF(pString);                                                  \
                                                                              \
        PyGILState_Release(gstate);                                           \
                                                                              \
        if (rExcCant != Qnil)                                                 \
            redrat_rb_exc_raise(                                              \
                rExcCant,                                                     \
                "redrat_ext: could not compute representation of "            \
                "Python object");                                             \
    }

redrat_stringify_generate(repr, Repr)
redrat_stringify_generate(str, Str)

void
Init_redrat_ext()
{
    rb_mRedRat = rb_define_module("RedRat");
    rb_mRedRatInternal = rb_define_module_under(rb_mRedRat, "Internal");

    rb_define_module_function(rb_mRedRatInternal, "builtins",
                              redrat_builtin_mapping, 0);
    rb_define_module_function(rb_mRedRatInternal, "apply", redrat_apply, -1);
    rb_define_module_function(
        rb_mRedRatInternal, "unicode", redrat_unicode, 1);
    rb_define_module_function(
        rb_mRedRatInternal, "getattr", redrat_getattr, 2);
    rb_define_module_function(rb_mRedRatInternal, "truth", redrat_truth, 1);

    /* Generated, see redrat_stringify_generate */
    rb_define_module_function(rb_mRedRatInternal, "repr", redrat_repr, 1);
    rb_define_module_function(rb_mRedRatInternal, "str", redrat_str, 1);

    rb_cPythonValue = rb_define_class_under(rb_mRedRatInternal,
                                            "PythonValue", rb_cObject);

    /*
     * The RedRatException type, which wraps (optionally) a RedRat reason for
     * the exception as well as the underlying python_exception, which can be
     * inspected as a normal PythonValue would.
     */
    rb_eRedRatException = rb_define_class_under(rb_mRedRatInternal,
                                                "RedRatException",
                                                rb_eStandardError);
    rb_define_attr(rb_eRedRatException, "redrat_reason", 1, 0);
    rb_define_attr(rb_eRedRatException, "python_type", 1, 1);
    rb_define_attr(rb_eRedRatException, "python_value", 1, 1);
    rb_define_attr(rb_eRedRatException, "python_traceback", 1, 1);

    Py_SetProgramName("play.rb");
    Py_Initialize();

    /* Initialize the redrat module in Python */
    initredrat();
}


/* Python Constructs */

/*
 * The inverse of PythonValue in RedRat for Ruby is RubyObject in Python.  This
 * is useful so that one can write Ruby classes that can be sent into Python
 * classes, or even send standard Ruby Hashes, Arrays, and Unicode directly to
 * Python, without marshalling or copying, so long as they are taught the right
 * operator conventions.
 */

static PyMethodDef rubyobject_methods[] = {
    {NULL}  /* Sentinel */
};

static void
redrat_rubyobject_dealloc(redrat_RubyObject* self)
{
    /*
     * Notify Ruby that this value is no longer required by Python.  Analogous
     * to its Ruby inverse, redrat_py_decref_wrap.
     */
#ifndef REDRAT_NO_GC
    rb_gc_unregister_address(&(self->r));
#endif
    self->ob_type->tp_free((PyObject*)self);
}

/*
 * redrat_python_handoff - Hands off a Ruby VALUE to Python
 *
 * If this PyObject is of type RubyObject, then just return the unwrapped Ruby
 * object inside.
 *
 * This procedure presumes that the Python GIL and Ruby GILs are already held.
 *
 * This includes the hybridization of Ruby and Python GC.  To do this, Ruby
 * will get its own global variable reference (via rb_gc_register_address) and
 * in the destructor for a RubyObject the inverse, rb_gc_unregister_address
 * must be called.
 */
static PyObject *
redrat_python_handoff(VALUE r)
{
    if (REDRAT_PYTHONVALUE_P(r))
    {
        PyObject *ret;

        printf("fdr: unwrapping RubyObject into Python\n");
        Data_Get_Struct(r, PyObject, ret);

        return ret;
    }
    else
    {
        redrat_RubyObject *pyr;

        printf("fdr: handing off ruby object to python\n");
        /*
         * Notify Ruby that this value has a reference somewhere otherwise
         * unknown to its mark-sweep collection pass, as so the value does not
         * get GCed while Python has references still.
         */
        rb_gc_register_address(&r);
        pyr = (void *) redrat_RubyType.tp_alloc(&redrat_RubyType, 0);
        pyr->r = r;
        return (PyObject *) pyr;
    }
}

PyMODINIT_FUNC
initredrat(void)
{
    PyObject *m;

    redrat_RubyType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&redrat_RubyType) < 0)
        return;

    m = Py_InitModule3("redrat", rubyobject_methods,
                       "Module that exposes Ruby Objects to Python.");

    Py_INCREF(&redrat_RubyType);
    PyModule_AddObject(m, "RubyObject", (PyObject *) &redrat_RubyType);
}

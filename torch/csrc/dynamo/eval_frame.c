#define PY_SSIZE_T_CLEAN
#include <torch/csrc/dynamo/cpython_defs.h>
#include <torch/csrc/utils/python_compat.h>
#include <opcode.h>
#include <stdbool.h>

// see https://bugs.python.org/issue35886
#if PY_VERSION_HEX >= 0x03080000
#define Py_BUILD_CORE
#include <internal/pycore_pystate.h>

// These headers were added in 3.11
#if IS_PYTHON_3_11_PLUS
#include <internal/pycore_frame.h>
#endif

#undef Py_BUILD_CORE
#endif // PY_VERSION_HEX >= 0x03080000

// All the eval APIs change in 3.11 so we need to decide which one to use on the fly
// https://docs.python.org/3/c-api/init.html#c._PyFrameEvalFunction
#if IS_PYTHON_3_11_PLUS
#define THP_EVAL_API_FRAME_OBJECT _PyInterpreterFrame

// We need to be able to return the _PyInterpreterFrame to python so create
// a python binding for it

typedef struct THPPyInterpreterFrame {
  PyObject_HEAD
  _PyInterpreterFrame* frame; // Borrowed reference
} THPPyInterpreterFrame;

THPPyInterpreterFrame* THPPyInterpreterFrame_New(_PyInterpreterFrame* frame);

#define DECLARE_PYOBJ_ATTR(name) \
static PyObject* THPPyInterpreterFrame_##name(THPPyInterpreterFrame* self, PyObject* _noargs) { \
  PyObject* res = (PyObject*)self->frame->name; \
  Py_XINCREF(res); \
  return res; \
}

DECLARE_PYOBJ_ATTR(f_func)
DECLARE_PYOBJ_ATTR(f_globals)
DECLARE_PYOBJ_ATTR(f_builtins)
DECLARE_PYOBJ_ATTR(f_locals)
DECLARE_PYOBJ_ATTR(f_code)
DECLARE_PYOBJ_ATTR(frame_obj)

#undef DECLARE_PYOBJ_ATTR

static THPPyInterpreterFrame* THPPyInterpreterFrame_previous(THPPyInterpreterFrame* self, PyObject* _noargs) {
  THPPyInterpreterFrame* res = THPPyInterpreterFrame_New(self->frame->previous);
  return res;
}

// This is not a true attribute of the class but we do access it in python and it is hard to implement
// on the python side, so do it here:
static PyObject* THPPyInterpreterFrame_f_lasti(THPPyInterpreterFrame* self, PyObject* _noargs) {
  return PyLong_FromLong(_PyInterpreterFrame_LASTI(self->frame));
}

static PyObject* THPPyInterpreterFrame_f_lineno(THPPyInterpreterFrame* self, PyObject* _noargs) {
  if (!self->frame->frame_obj) {
    return PyLong_FromLong(self->frame->f_code->co_firstlineno);
  }
  int lineno = PyFrame_GetLineNumber(self->frame->frame_obj);
  if (lineno < 0) {
    Py_RETURN_NONE;
  }
  return PyLong_FromLong(lineno);
}

static PyObject* THPPyInterpreterFrame_f_back(THPPyInterpreterFrame* self, PyObject* _noargs) {
  if (!self->frame->frame_obj) {
    Py_RETURN_NONE;
  }
  return (PyObject*)PyFrame_GetBack(self->frame->frame_obj);
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-avoid-non-const-global-variables,modernize-avoid-c-arrays)
static struct PyGetSetDef THPPyInterpreterFrame_properties[] = {
    {"f_func", (getter)THPPyInterpreterFrame_f_func, NULL, NULL, NULL},
    {"f_globals", (getter)THPPyInterpreterFrame_f_globals, NULL, NULL, NULL},
    {"f_builtins", (getter)THPPyInterpreterFrame_f_builtins, NULL, NULL, NULL},
    {"f_locals", (getter)THPPyInterpreterFrame_f_locals, NULL, NULL, NULL},
    {"f_code", (getter)THPPyInterpreterFrame_f_code, NULL, NULL, NULL},
    {"frame_obj", (getter)THPPyInterpreterFrame_frame_obj, NULL, NULL, NULL},
    {"previous", (getter)THPPyInterpreterFrame_previous, NULL, NULL, NULL},
    {"f_lasti", (getter)THPPyInterpreterFrame_f_lasti, NULL, NULL, NULL},
    {"f_lineno", (getter)THPPyInterpreterFrame_f_lineno, NULL, NULL, NULL},
    {"f_back", (getter)THPPyInterpreterFrame_f_back, NULL, NULL, NULL},
    {NULL}};

static PyTypeObject THPPyInterpreterFrameType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "torch._C.dynamo.eval_frame._PyInterpreterFrame",
    .tp_basicsize = sizeof(THPPyInterpreterFrame),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_getset = THPPyInterpreterFrame_properties,
};


THPPyInterpreterFrame* THPPyInterpreterFrame_New(_PyInterpreterFrame* frame) {
  PyTypeObject* type = (PyTypeObject*)&THPPyInterpreterFrameType;
  THPPyInterpreterFrame* self = (THPPyInterpreterFrame*)type->tp_alloc(type, 0);
  if (!self)
    return NULL;
  self->frame = frame;
  return self;
}


#else
#define THP_EVAL_API_FRAME_OBJECT PyFrameObject

#define THP_PyFrame_FastToLocalsWithError PyFrame_FastToLocalsWithError
#endif

#ifdef _WIN32
#define unlikely(x) (x)
#else
#define unlikely(x) __builtin_expect((x), 0)
#endif

#define NULL_CHECK(val)                                         \
  if (unlikely((val) == NULL)) {                                \
    fprintf(stderr, "NULL ERROR: %s:%d\n", __FILE__, __LINE__); \
    PyErr_Print();                                              \
    abort();                                                    \
  } else {                                                      \
  }

#define CHECK(cond)                                                     \
  if (unlikely(!(cond))) {                                              \
    fprintf(stderr, "DEBUG CHECK FAILED: %s:%d\n", __FILE__, __LINE__); \
    abort();                                                            \
  } else {                                                              \
  }

// Uncomment next line to print DEBUG_TRACE messages
// #define TORCHDYNAMO_DEBUG 1

#ifdef TORCHDYNAMO_DEBUG

#define DEBUG_CHECK(cond) CHECK(cond)
#define DEBUG_NULL_CHECK(val) NULL_CHECK(val)
#define DEBUG_TRACE(msg, ...) \
  fprintf(stderr, "TRACE[%s:%d] " msg "\n", __func__, __LINE__, __VA_ARGS__)
#define DEBUG_TRACE0(msg) \
  fprintf(stderr, "TRACE[%s:%d] " msg "\n", __func__, __LINE__)

#else

#define DEBUG_CHECK(cond)
#define DEBUG_NULL_CHECK(val)
#define DEBUG_TRACE(msg, ...)
#define DEBUG_TRACE0(msg)

#endif

// Flag to just run a frame normally
#define SKIP_CODE ((void*)0x1)

bool is_dynamo_compiling = false;
static PyObject* guard_fail_hook = NULL;
static PyObject* guard_error_hook = NULL;
static PyObject* profiler_start_hook = NULL;
static PyObject* profiler_end_hook = NULL;
static PyObject* guard_profiler_name_str = NULL; /* cached py str */

static size_t cache_entry_extra_index = -1;
static size_t dynamic_frame_state_extra_index = -2;

static Py_tss_t eval_frame_callback_key = Py_tss_NEEDS_INIT;

inline static PyObject* eval_frame_callback_get(void) {
  void* result = PyThread_tss_get(&eval_frame_callback_key);
  if (unlikely(result == NULL)) {
    return (PyObject*)Py_None;
  } else {
    return (PyObject*)result;
  }
}

inline static void eval_frame_callback_set(PyObject* obj) {
  PyThread_tss_set(&eval_frame_callback_key, obj);
}

static void ignored(void* obj) {}
static PyObject* _custom_eval_frame_shim(
    PyThreadState* tstate,
    THP_EVAL_API_FRAME_OBJECT* frame,
    int throw_flag);
static PyObject* _custom_eval_frame(
    PyThreadState* tstate,
    THP_EVAL_API_FRAME_OBJECT* frame,
    int throw_flag,
    PyObject* callback);
static PyObject *(*previous_eval_frame)(PyThreadState *tstate,
                                        THP_EVAL_API_FRAME_OBJECT* frame, int throw_flag) = NULL;

#if PY_VERSION_HEX >= 0x03090000
static PyObject* custom_eval_frame_shim(
    PyThreadState* tstate,
    THP_EVAL_API_FRAME_OBJECT* frame,
    int throw_flag) {
  return _custom_eval_frame_shim(tstate, frame, throw_flag);
}
#else
static PyObject* custom_eval_frame_shim(THP_EVAL_API_FRAME_OBJECT* frame, int throw_flag) {
  PyThreadState* tstate = PyThreadState_GET();
  return _custom_eval_frame_shim(tstate, frame, throw_flag);
}
#endif

inline static PyObject* eval_frame_default(
    PyThreadState* tstate,
    THP_EVAL_API_FRAME_OBJECT* frame,
    int throw_flag) {
#if PY_VERSION_HEX >= 0x03090000
  if (tstate == NULL) {
    tstate = PyThreadState_GET();
  }
  if (previous_eval_frame) {
    return previous_eval_frame(tstate, frame, throw_flag);
  }
  else {
    return _PyEval_EvalFrameDefault(tstate, frame, throw_flag);
  }
#else
  return _PyEval_EvalFrameDefault(frame, throw_flag);
#endif
}

inline static void enable_eval_frame_shim(PyThreadState* tstate) {
#if PY_VERSION_HEX >= 0x03090000
  if (_PyInterpreterState_GetEvalFrameFunc(tstate->interp) !=
      &custom_eval_frame_shim) {
    DEBUG_CHECK(previous_eval_frame == NULL);
    previous_eval_frame = _PyInterpreterState_GetEvalFrameFunc(tstate->interp);
    _PyInterpreterState_SetEvalFrameFunc(tstate->interp,
                                         &custom_eval_frame_shim);
  }
#else
  if (tstate->interp->eval_frame != &custom_eval_frame_shim) {
    // First call
    tstate->interp->eval_frame = &custom_eval_frame_shim;
  }
#endif
}

inline static void enable_eval_frame_default(PyThreadState* tstate) {
#if PY_VERSION_HEX >= 0x03090000
  if (_PyInterpreterState_GetEvalFrameFunc(tstate->interp) !=
      previous_eval_frame) {
    DEBUG_CHECK(previous_eval_frame != NULL);
    _PyInterpreterState_SetEvalFrameFunc(tstate->interp,
                                         previous_eval_frame);
    previous_eval_frame = NULL;
  }
#else
  if (tstate->interp->eval_frame != &_PyEval_EvalFrameDefault) {
    // First call
    tstate->interp->eval_frame = &_PyEval_EvalFrameDefault;
  }
#endif
}

static inline PyObject* call_callback(
    PyObject* callable,
    THP_EVAL_API_FRAME_OBJECT* _frame,
    long cache_len,
    PyObject* frame_state) {

#if IS_PYTHON_3_11_PLUS
  THPPyInterpreterFrame* frame = THPPyInterpreterFrame_New(_frame);
  if (frame == NULL) {
    return NULL;
  }
#else
  PyObject* frame = Py_NewRef(_frame);
#endif
  PyObject* res = PyObject_CallFunction(callable, "OlO", frame, cache_len, frame_state);
  Py_DECREF(frame);
  return res;
}

inline static const char* name(THP_EVAL_API_FRAME_OBJECT* frame) {
  DEBUG_CHECK(PyUnicode_Check(frame->f_code->co_name));
  return PyUnicode_AsUTF8(frame->f_code->co_name);
}

inline static bool is_nn_module_instance(PyObject* obj) {
  PyObject* torch = PyImport_ImportModule("torch");
  PyObject* nn = PyObject_GetAttrString(torch, "nn");
  PyObject* nn_module = PyObject_GetAttrString(nn, "Module");
  if (PyObject_IsInstance(obj, nn_module)) {
    Py_DECREF(torch);
    Py_DECREF(nn);
    Py_DECREF(nn_module);
    return true;
  }
  Py_DECREF(torch);
  Py_DECREF(nn);
  Py_DECREF(nn_module);
  return false;
}


inline static bool is_dunder_method(THP_EVAL_API_FRAME_OBJECT* frame) {
  const char* frame_name = name(frame);
  return sizeof(frame_name) >= 2 && frame_name[0] == '_' && frame_name[1] == '_';
}

inline static PyObject* get_nn_module_if_frame_is_method_of_nn_module(THP_EVAL_API_FRAME_OBJECT* frame) {
  // Essentially returns isinstance(f_locals["self"], nn.Module).
  // There are some caveats here
  // 1) We rely on name self. It is possible that a method does not use self keyword.
  // 2) It is possible that a function is incorrectly detected here as nn module
  // method because the function has a self keyword which happens to be a nn
  // module instance.
  // For both of these cases, we will still be functionally correct. Our cache
  // will still work, just that it might have more collisions than necessary for
  // the above cases.

  if (is_dunder_method(frame)) {
    // Skip for dunder methods like __init__ and __getattribute__. The self
    // object might not be in the full initialized state to do isinstance(self,
    // nn.Module).
    return NULL;
  }

  Py_ssize_t nlocals = frame->f_code->co_nlocals;
  PyObject* co_varnames = PyCode_GetVarnames(frame->f_code);
  if (nlocals == 0 || PyTuple_Size(co_varnames) == 0) {
    return NULL;
  }

  // Find the index of the first local variable named "self". Because of
  // continuation on graph breaks, we may have self at non-zero location on the
  // resumed frames.
  Py_ssize_t self_index = 0;
  bool found = false;
  for (Py_ssize_t i = 0; i < nlocals; i++) {
    PyObject* first_var = PyTuple_GET_ITEM(co_varnames, i);
    if (first_var != NULL) {
      const char* first_var_name = PyUnicode_AsUTF8(first_var);
      if (strcmp(first_var_name, "self") == 0) {
        self_index = i;
        found = true;
        break;
      }
    }
  }

  if (!found) {
    return NULL;
  }


  #if IS_PYTHON_3_11_PLUS
  PyObject** fastlocals = frame->localsplus;
  #else
  PyObject** fastlocals = frame->f_localsplus;
  #endif

  PyObject* self_object = fastlocals[self_index];
  if (self_object == NULL) {
    return NULL;
  }
  if (is_nn_module_instance(self_object)) {
    return self_object;
  }
  return NULL;
}

typedef struct cache_entry {
  // check the guards: lambda: <locals of user function>: bool
  PyObject* check_fn;
  // modified user bytecode (protected by check_fn's guards)
  PyCodeObject* code;
  // on a cache miss, linked list of next thing to try
  struct cache_entry* next;
} CacheEntry;

static CacheEntry* create_cache_entry(
    CacheEntry* next,
    PyObject* guarded_code) {
  // Adds a new entry at the front of the linked list.
  CacheEntry* e = (CacheEntry*)malloc(sizeof(CacheEntry));
  DEBUG_NULL_CHECK(e);
  e->check_fn = PyObject_GetAttrString(guarded_code, "check_fn");
  NULL_CHECK(e->check_fn);
  e->code = (PyCodeObject*)PyObject_GetAttrString(guarded_code, "code");
  NULL_CHECK(e->code);
  e->next = next;
  return e;
}

static void destroy_cache_entry(CacheEntry* e) {
  if (e == NULL || e == SKIP_CODE) {
    return;
  }
  Py_XDECREF(e->check_fn);
  Py_XDECREF(e->code);
  destroy_cache_entry(e->next);
  free(e);
}

typedef struct {
  PyObject_HEAD
  CacheEntry* cache_entry;
} CacheEntryWrapper;

static PyTypeObject CacheEntryWrapperType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "torch._C.dynamo.eval_frame.CacheEntryWrapper",
  .tp_basicsize = sizeof(CacheEntryWrapper),
  .tp_itemsize = 0,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_new = PyType_GenericNew,
};


// The cache lives on the extra segment of code object. It can have one of the 4 possible values
// 1) NULL - First time accessed the extra segment.
// 2) SKIP_CODE - We will skip the original frame.
// 3) CacheEntryWrapper - This contains the cache_entry for the frame.
// 4) A dict from nn module to the CacheEntryWrapper.
//
// Extracting a cache entry is split into two functions -
// get_cache_entry_without_unwrapping and unwrap_cache_entry. The reason for
// this split is to enable early return for SKIP_CODE.

// For (1) and (2), we need to short circuit and return quickly for skipped
// frames. Therefore, we cover this separately in get_cache_entry_without_unwrapping.

// For (3) - If the frame is not a method of a nn.Module instance,
// we just store the CacheEntry object directly on the extra segment.
// For (4) - If the frame is a method of a nn.Module instance, we
// store the CacheEntry for each nn module instance. This ensures that if a
// SubModule is instantiated multiple times in a nn.Module and there is a
// graph break in the SubModule, we save the CacheEntry per submodule isntance
// and not per code object. If it is per code object (which is static and
// shared across all nn module instances), this leads to collisions.

inline static PyObject* get_cache_entry_without_unwrapping(PyCodeObject* code) {
  // This just returns the extra value at cache entry index. It does not do any
  // unwrapping mentioned in (3) and (4). This is useful to check if the frame
  // has to be skipped and we can return quickly in _custom_eval_frame. This is
  // useful in cases where we do lots of skips (like pytorch tests).
   PyObject* extra = NULL;
  _PyCode_GetExtra((PyObject*)code, cache_entry_extra_index, (void*)&extra);
  return extra;
}

inline static CacheEntry* get_cache_entry(PyCodeObject* code, PyObject* nn_module) {
  // The cache lives on the extra segment of code object. It can have one of the 4 possible values
  // 1) NULL - First time accessed the extra segment.
  // 2) SKIP_CODE - We will skip the original frame.
  // 3) CacheEntryWrapper - This contains the cache_entry for the frame.
  // 4) A dict from nn module to the CacheEntryWrapper.
  //
  // More details on (4) - If the frame is a method of a nn.Module instance, we
  // store the CacheEntry for each nn module instance. This ensures that if a
  // SubModule is instantiated multiple times in a nn.Module and there is a
  // graph break in the SubModule, we save the CacheEntry per submodule isntance
  // and not per code object. If it is per code object (which is static and
  // shared across all nn module instances), this leads to collisions.
  //
  // More details on (3) - If the frame is not a method of a nn.Module instance,
  // we just store the CacheEntry object directly on the extra segment.
  //
  // Note on (2) - We could move SKIP_CODE directly into the
  // CacheEntryWrapper->cache_entry, but we keep it separate to have a quick
  // lookup time for frames that are skipped.
  PyObject* extra = get_cache_entry_without_unwrapping(code);
  // Case 1 and 2 - Short circuit for SKIP_CODE.
  // TODO - Does skipping makes it faster?
  if (extra == NULL || extra == SKIP_CODE) {
    return (CacheEntry*)extra;
  }

  // Case 3 - Cache entry is stored directly on the extra segment.
  if (PyObject_IsInstance(extra, (PyObject *)&CacheEntryWrapperType)) {
    return ((CacheEntryWrapper*)extra)->cache_entry;
  }

  // Case 4.
  DEBUG_CHECK(PyDict_Check(extra));
  DEBUG_NULL_CHECK(nn_module);

  // TODO - the callback is set to NULL. Currently, there is a callback in
  // CheckFnManager invalidate, which should track the garbage collection of nn
  // modules. However, we can't fully rely on that one to call reset_code
  // because when the callback is called, the referent is dead, so the weakrefs
  // in the C cache will no longer hold equality with the weakref from python.
  // Revisit this callback later, when we want to automatically free the cached
  // guards/graphs on module garbage collection. This involves understanding how
  // to write a Python callback function purely in C.
  PyObject* nn_module_weakref = PyWeakref_NewRef(nn_module, NULL);

  // If the cache is empty, return a null object.
  PyObject* nn_module_to_cache_entry_map = extra;
  if (PyDict_GetItem(nn_module_to_cache_entry_map, nn_module_weakref) == NULL) {
    return NULL;
  }

  // Find the cache entry.
  CacheEntryWrapper* cache_entry_wrapper = (CacheEntryWrapper*)PyDict_GetItem(nn_module_to_cache_entry_map, nn_module_weakref);
  return cache_entry_wrapper->cache_entry;
}

inline static void set_cache_entry_on_code(PyCodeObject* code, CacheEntry* extra) {
  _PyCode_SetExtra((PyObject*)code, cache_entry_extra_index, extra);
}

inline static void set_cache_entry(PyCodeObject* code, CacheEntry* cache_entry, PyObject* nn_module) {
  // The cache lives on the extra segment of code object. It can have one of the 4 possible values
  // 1) NULL - First time accessed the extra segment.
  // 2) SKIP_CODE - We will skip the original frame.
  // 3) CacheEntryWrapper - This contains the cache_entry for the frame.
  // 4) A dict from nn module to the CacheEntryWrapper.
  // Look in get_cache_entry comments for more details.

  // TODO(jansel): would it be faster to bypass this?

  // Case 1 and 2 - Short circuit for SKIP_CODE.
  if (cache_entry == NULL || cache_entry == SKIP_CODE) {
    set_cache_entry_on_code(code, cache_entry);
    return;
  }
  // Case 3
  if (nn_module == NULL) {
    CacheEntryWrapper *cache_entry_wrapper = (CacheEntryWrapper*) PyObject_CallObject((PyObject *) &CacheEntryWrapperType, NULL);
    cache_entry_wrapper->cache_entry = cache_entry;
    _PyCode_SetExtra((PyObject*)code, cache_entry_extra_index, (PyObject*) cache_entry_wrapper);
    return;
  }

  // Case 4
  PyObject* nn_module_to_cache_entry_map = NULL;
  _PyCode_GetExtra((PyObject*)code, cache_entry_extra_index, (void*)&nn_module_to_cache_entry_map);

  if (nn_module_to_cache_entry_map == NULL) {
    nn_module_to_cache_entry_map = PyDict_New();
  }

  PyObject* nn_module_weakref = PyWeakref_NewRef(nn_module, NULL);
  // TODO - Do I need to DECREF nn_module here? Is it a stolen reference?
  CacheEntryWrapper *cache_entry_wrapper = (CacheEntryWrapper*) PyObject_CallObject((PyObject *) &CacheEntryWrapperType, NULL);
  cache_entry_wrapper->cache_entry = cache_entry;
  PyDict_SetItem(nn_module_to_cache_entry_map, nn_module_weakref, (PyObject*) cache_entry_wrapper);
  _PyCode_SetExtra((PyObject*)code, cache_entry_extra_index, nn_module_to_cache_entry_map);

}

inline static PyObject* get_frame_state(PyCodeObject* code, PyObject* nn_module) {
  // Read get_cache_entry to understand the cache structure. For frame_state, we
  // have to replicate similar hierarchy. There are 3 possible values.
  // 1) NULL - First time accessed the extra segment.
  // 2) Not a NN module method frame - just return PyObject
  // 3) NN module method frame - access the dict of nn module to frame state.

  PyObject* extra = NULL;
  _PyCode_GetExtra((PyObject*)code, dynamic_frame_state_extra_index, (void*)&extra);

  // Case 1 and 2
  if (extra == NULL || nn_module == NULL) {
    return NULL;
  }

  // case 3.
  PyObject* nn_module_weakref = PyWeakref_NewRef(nn_module, NULL);

  // If the cache is empty, return a null object.
  PyObject* nn_module_to_cache_entry_map = extra;
  if (PyDict_GetItem(nn_module_to_cache_entry_map, nn_module_weakref) == NULL) {
    return NULL;
  }
  // Find the frame state.
  return PyDict_GetItem(nn_module_to_cache_entry_map, nn_module_weakref);
}

inline static void set_frame_state(PyCodeObject* code, PyObject* extra, PyObject* nn_module) {
  // Read get_cache_entry to understand the cache structure. For frame_state, we
  // have to replicate similar hierarchy. There are 3 possible values.
  // 1) NULL - First time accessed the extra segment.
  // 2) Not a NN module method frame - just return PyObject
  // 3) NN module method frame - access the dict of nn module to frame state.

  // Case 1 and 2
  if (extra == NULL || nn_module == NULL) {
    _PyCode_SetExtra((PyObject*)code, dynamic_frame_state_extra_index, extra);
    return;
  }


  // Case 3
  PyObject* nn_module_to_cache_entry_map = NULL;
  _PyCode_GetExtra((PyObject*)code, cache_entry_extra_index, (void*)&nn_module_to_cache_entry_map);

  if (nn_module_to_cache_entry_map == NULL) {
    nn_module_to_cache_entry_map = PyDict_New();
  }

  PyObject* nn_module_weakref = PyWeakref_NewRef(nn_module, NULL);
  PyDict_SetItem(nn_module_to_cache_entry_map, nn_module_weakref, extra);
  _PyCode_SetExtra((PyObject*)code, cache_entry_extra_index, nn_module_to_cache_entry_map);
}

static PyObject* call_guard_fail_hook(
    PyObject* hook,
    CacheEntry* e,
    size_t index,
    PyObject* f_locals) {
  // call debugging logic when a guard fails
  return PyObject_CallFunction(
      hook,
      "OOOnO",
      e->check_fn,
      e->code,
      f_locals,
      (Py_ssize_t)index,
      (e->next == NULL ? Py_True : Py_False));
}

static PyObject* call_profiler_start_hook(PyObject* name_str) {
  if (profiler_start_hook == NULL) return NULL;
  return PyObject_CallOneArg(profiler_start_hook, name_str);
}

static void call_profiler_end_hook(PyObject* record) {
  // 'record' obj is the return value of calling _start_hook()
  if (profiler_end_hook == NULL || record == NULL) return;
  PyObject* res = PyObject_CallOneArg(profiler_end_hook, record);
  if (res == NULL) {
    PyErr_WriteUnraisable(profiler_end_hook);
    return;
  }
  Py_DECREF(res);
}

// Return value: borrowed reference
// Is either Py_None or a PyCodeObject
static PyObject* lookup(CacheEntry* e, THP_EVAL_API_FRAME_OBJECT *frame, CacheEntry* prev, size_t index, PyObject* maybe_nn_module) {
  if (e == NULL) {
    // NB: intentionally not using Py_RETURN_NONE, to return borrowed ref
    return Py_None;
  }
  PyObject *f_locals = frame->f_locals;
  PyObject* valid = PyObject_CallOneArg(e->check_fn, f_locals);
  if (unlikely(valid == NULL)) {
    if (guard_error_hook != NULL) {
      PyObject *type, *value, *traceback;
      PyErr_Fetch(&type, &value, &traceback);
      PyObject* r = call_guard_fail_hook(guard_error_hook, e, index, f_locals);
      if (r == NULL) {
        return NULL;
      }
      Py_DECREF(r);
      PyErr_Restore(type, value, traceback);
    }
    return NULL;
  }
  Py_DECREF(valid);
  if (valid == Py_True) {
    // Keep the head as the most recently used cache entry.
    // If the hit cache entry is not the head of the linked list,
    // move it to the head
    if (prev != NULL) {
        CacheEntry* extra = get_cache_entry(frame->f_code, maybe_nn_module);
        prev->next = e->next;
        e->next = extra;
        set_cache_entry(frame->f_code, e, maybe_nn_module);
    }
    return (PyObject*)e->code;
  }
  if (unlikely(guard_fail_hook != NULL)) {
    PyObject* r = call_guard_fail_hook(guard_fail_hook, e, index, f_locals);
    if (r == NULL) {
      return NULL;
    }
    Py_DECREF(r);
  }
  return lookup(e->next, frame, e, index + 1, maybe_nn_module);
}

static long cache_size(CacheEntry* e) {
  if (e == NULL) {
    return 0;
  }
  return 1 + cache_size(e->next);
}

inline static PyObject* eval_custom_code(
    PyThreadState* tstate,
    THP_EVAL_API_FRAME_OBJECT* frame,
    PyCodeObject* code,
    int throw_flag) {
  Py_ssize_t ncells = 0;
  Py_ssize_t nfrees = 0;
  Py_ssize_t nlocals_new = code->co_nlocals;
  Py_ssize_t nlocals_old = frame->f_code->co_nlocals;

  ncells = PyCode_GetNCellvars(code);
  nfrees = PyCode_GetNFreevars(code);

  DEBUG_NULL_CHECK(tstate);
  DEBUG_NULL_CHECK(frame);
  DEBUG_NULL_CHECK(code);
  DEBUG_CHECK(nlocals_new >= nlocals_old);

  #if IS_PYTHON_3_11_PLUS

  DEBUG_CHECK(ncells == frame->f_code->co_ncellvars);
  DEBUG_CHECK(nfrees == frame->f_code->co_nfreevars);

  // Generate Python function object and _PyInterpreterFrame in a way similar to
  // https://github.com/python/cpython/blob/e715da6db1d1d70cd779dc48e1ba8110c51cc1bf/Python/ceval.c#L1130
  PyFunctionObject* func = _PyFunction_CopyWithNewCode((PyFunctionObject*) frame->f_func, code);
  if (func == NULL) {
    return NULL;
  }

  size_t size = code->co_nlocalsplus + code->co_stacksize + FRAME_SPECIALS_SIZE;
  // THP_EVAL_API_FRAME_OBJECT (_PyInterpreterFrame) is a regular C struct, so
  // it should be safe to use system malloc over Python malloc, e.g. PyMem_Malloc
  THP_EVAL_API_FRAME_OBJECT* shadow = malloc(size * sizeof(PyObject*));
  if (shadow == NULL) {
    Py_DECREF(func);
    return NULL;
  }

  Py_INCREF(func);
  // consumes reference to func
  _PyFrame_InitializeSpecials(shadow, func, NULL, code->co_nlocalsplus);

  PyObject** fastlocals_old = frame->localsplus;
  PyObject** fastlocals_new = shadow->localsplus;

  // localsplus are XINCREF'd by default eval frame, so all values must be valid.
  for (int i = 0; i < code->co_nlocalsplus; i++) {
    fastlocals_new[i] = NULL;
  }

  // copy from old localsplus to new localsplus:
  // for i, name in enumerate(localsplusnames_new):
  //   name_to_idx[name] = i
  // for i, name in enumerate(localsplusnames_old):
  //   fastlocals_new[name_to_idx[name]] = fastlocals_old[i]
  PyObject* name_to_idx = PyDict_New();
  if (name_to_idx == NULL) {
    DEBUG_TRACE0("unable to create localsplus name dict");
    THP_PyFrame_Clear(shadow);
    free(shadow);
    Py_DECREF(func);
    return NULL;
  }

  for (Py_ssize_t i = 0; i < code->co_nlocalsplus; i++) {
    PyObject *name = PyTuple_GET_ITEM(code->co_localsplusnames, i);
    PyObject *idx = PyLong_FromSsize_t(i);
    if (name == NULL || idx == NULL || PyDict_SetItem(name_to_idx, name, idx) != 0) {
      Py_DECREF(name_to_idx);
      THP_PyFrame_Clear(shadow);
      free(shadow);
      Py_DECREF(func);
      return NULL;
    }
  }

  for (Py_ssize_t i = 0; i < frame->f_code->co_nlocalsplus; i++) {
    PyObject *name = PyTuple_GET_ITEM(frame->f_code->co_localsplusnames, i);
    PyObject *idx = PyDict_GetItem(name_to_idx, name);
    Py_ssize_t new_i = PyLong_AsSsize_t(idx);
    if (name == NULL || idx == NULL || (new_i == (Py_ssize_t)-1 && PyErr_Occurred() != NULL)) {
      Py_DECREF(name_to_idx);
      THP_PyFrame_Clear(shadow);
      free(shadow);
      Py_DECREF(func);
      return NULL;
    }
    Py_XINCREF(fastlocals_old[i]);
    fastlocals_new[new_i] = fastlocals_old[i];
  }

  Py_DECREF(name_to_idx);

  #else

  DEBUG_CHECK(ncells == PyTuple_GET_SIZE(frame->f_code->co_cellvars));
  DEBUG_CHECK(nfrees == PyTuple_GET_SIZE(frame->f_code->co_freevars));

  THP_EVAL_API_FRAME_OBJECT* shadow = PyFrame_New(tstate, code, frame->f_globals, NULL);
  if (shadow == NULL) {
    return NULL;
  }

  PyObject** fastlocals_old = frame->f_localsplus;
  PyObject** fastlocals_new = shadow->f_localsplus;

  for (Py_ssize_t i = 0; i < nlocals_old; i++) {
    Py_XINCREF(fastlocals_old[i]);
    fastlocals_new[i] = fastlocals_old[i];
  }

  for (Py_ssize_t i = 0; i < ncells + nfrees; i++) {
    Py_XINCREF(fastlocals_old[nlocals_old + i]);
    fastlocals_new[nlocals_new + i] = fastlocals_old[nlocals_old + i];
  }

  #endif

  PyObject* result = eval_frame_default(tstate, shadow, throw_flag);

  #if IS_PYTHON_3_11_PLUS

  THP_PyFrame_Clear(shadow);
  free(shadow);
  Py_DECREF(func);

  #else

  Py_DECREF(shadow);

  #endif

  return result;
}

static PyObject* _custom_eval_frame_shim(
    PyThreadState* tstate,
    THP_EVAL_API_FRAME_OBJECT* frame,
    int throw_flag) {
  // Shims logic into one of three states. Can probably be refactored into a
  // single func, later:
  //  - None: disables TorchDynamo
  //  - False: run-only mode (reuse existing compiles)
  //  - Python callable(): enables TorchDynamo
  PyObject* callback = eval_frame_callback_get();

  if (callback == Py_None) {
    return eval_frame_default(tstate, frame, throw_flag);
  }

  return _custom_eval_frame(tstate, frame, throw_flag, callback);
}

static PyObject* _custom_eval_frame(
    PyThreadState* tstate,
    THP_EVAL_API_FRAME_OBJECT* frame,
    int throw_flag,
    PyObject* callback) {
  #if IS_PYTHON_3_11_PLUS
  DEBUG_TRACE(
      "begin %s %s %i %i",
      name(frame),
      PyUnicode_AsUTF8(frame->f_code->co_filename),
      frame->f_code->co_firstlineno,
      _PyInterpreterFrame_LASTI(frame));
  #else
  DEBUG_TRACE(
      "begin %s %s %i %i %i",
      name(frame),
      PyUnicode_AsUTF8(frame->f_code->co_filename),
      frame->f_lineno,
      frame->f_lasti,
      frame->f_iblock);
  #endif

  if (throw_flag) {
    // When unwinding generators, eval frame is called with throw_flag ==
    // true.  Frame evaluation is supposed to continue unwinding by propagating
    // the exception.  Dynamo doesn't really know how to do this, nor does it
    // really want to do this, because there's unlikely any code to capture
    // (you're going to immediately quit out of the frame, perhaps running
    // some unwinding logic along the way).  So we just run the default
    // handler in this case.
    //
    // NB: A previous version of this patch returned NULL.  This is wrong,
    // because returning NULL is *different* from unwinding an exception.
    // In particular, you will not execute things like context manager
    // __exit__ if you just return NULL.
    //
    // NB: It's /conceivable/ that you might want to actually still call the
    // Dynamo callback when throw_flag == TRUE, to give Dynamo a chance to
    // do any stack unwinding code.  But this is not really useful because
    // (1) Dynamo doesn't actually know how to do stack unwinding, so it would
    // immediately skip the frame, and (2) even if it did, this would only
    // be profitable if there was tensor code in the unwinding code.  Seems
    // unlikely.
    DEBUG_TRACE("throw %s", name(frame));
    return eval_frame_default(tstate, frame, throw_flag);
  }

  PyObject* wrapped_extra = get_cache_entry_without_unwrapping(frame->f_code);
  if (wrapped_extra == SKIP_CODE || (callback == Py_False && wrapped_extra == NULL)) {
    DEBUG_TRACE("skip %s", name(frame));
    return eval_frame_default(tstate, frame, throw_flag);
  }

  // TODO(jansel): investigate directly using the "fast" representation
  // TODO(alband): This is WRONG for python3.11+ we pass in a _PyInterpreterFrame
  // even though we should pass a PyFrameObject.
  if (THP_PyFrame_FastToLocalsWithError(frame) < 0) {
    DEBUG_TRACE("error %s", name(frame));
    return NULL;
  }

  // Get the nn_module object if the frame is a method of a nn.Module instance.
  // This will be used by get/set_cache_entry and get/set_frame_state.
  eval_frame_callback_set(Py_None);
  DEBUG_CHECK(PyDict_CheckExact(frame->f_locals));
  PyObject* maybe_nn_module = get_nn_module_if_frame_is_method_of_nn_module(frame);
  CacheEntry* extra = get_cache_entry(frame->f_code, maybe_nn_module);
  eval_frame_callback_set(callback);
  if (extra == SKIP_CODE || (callback == Py_False && extra == NULL)) {
    DEBUG_TRACE("skip %s", name(frame));
    return eval_frame_default(tstate, frame, throw_flag);
  }

  // A callback of Py_False indicates "run only" mode, the cache is checked, but
  // we never compile.
  if (callback == Py_False) {
    DEBUG_TRACE("In run only mode %s", name(frame));
    PyObject* hook_record = call_profiler_start_hook(guard_profiler_name_str);
    PyObject* maybe_cached_code = lookup(extra, frame, NULL, 0, maybe_nn_module);
    call_profiler_end_hook(hook_record);
    Py_XDECREF(hook_record);

    if (maybe_cached_code == NULL) {
      // guard eval failed, keep propagating
      return NULL;
    } else if (maybe_cached_code == Py_None) {
      DEBUG_TRACE("cache miss %s", name(frame));
      return eval_frame_default(tstate, frame, throw_flag);
    }
    PyCodeObject* cached_code = (PyCodeObject*)maybe_cached_code;
    // used cached version
    DEBUG_TRACE("cache hit %s", name(frame));
    return eval_custom_code(tstate, frame, cached_code, throw_flag);
  }
  DEBUG_CHECK(PyDict_CheckExact(frame->f_locals));
  DEBUG_CHECK(PyDict_CheckExact(frame->f_globals));
  DEBUG_CHECK(PyDict_CheckExact(frame->f_builtins));

  // We don't run the current custom_eval_frame behavior for guards.
  // So we temporarily set the callback to Py_None to drive the correct behavior
  // in the shim.
  eval_frame_callback_set(Py_None);

  PyObject* hook_record = call_profiler_start_hook(guard_profiler_name_str);
  PyObject* maybe_cached_code = lookup(extra, frame, NULL, 0, maybe_nn_module);
  call_profiler_end_hook(hook_record);
  Py_XDECREF(hook_record);
  if (maybe_cached_code == NULL) {
    // Python error
    return NULL;
  } else if (maybe_cached_code != Py_None) {
    PyCodeObject* cached_code = (PyCodeObject*)maybe_cached_code;
    // used cached version
    DEBUG_TRACE("cache hit %s", name(frame));
    // Re-enable custom behavior
    eval_frame_callback_set(callback);
    return eval_custom_code(tstate, frame, cached_code, throw_flag);
  }
  // cache miss

  PyObject *frame_state = get_frame_state(frame->f_code, maybe_nn_module);
  if (frame_state == NULL) {
    // TODO(voz): Replace this dict with a real FrameState object.
    frame_state = PyDict_New();
    set_frame_state(frame->f_code, frame_state, maybe_nn_module);
  }
  // TODO(alband): This is WRONG for python3.11+ we pass in a _PyInterpreterFrame
  // that gets re-interpreted as a PyObject (which it is NOT!)
  PyObject* result =
      call_callback(callback, frame, cache_size(extra), frame_state);
  if (result == NULL) {
    // internal exception, returning here will leak the exception into user code
    // this is useful for debugging -- but we dont want it to happen outside of
    // testing
    // NB: we intentionally DO NOT re-enable custom behavior to prevent
    // cascading failure from internal exceptions.  The upshot is if
    // Dynamo barfs, that's it for Dynamo, even if you catch the exception
    // inside the torch.compile block we won't try to Dynamo anything else.
    return NULL;
  } else if (result != Py_None) {
    DEBUG_TRACE("create cache %s", name(frame));
    extra = create_cache_entry(extra, result);
    Py_DECREF(result);
    set_cache_entry(frame->f_code, extra, maybe_nn_module);
    // Re-enable custom behavior
    eval_frame_callback_set(callback);
    return eval_custom_code(tstate, frame, extra->code, throw_flag);
  } else {
    DEBUG_TRACE("create skip %s", name(frame));
    Py_DECREF(result);
    destroy_cache_entry(extra);
    set_cache_entry(frame->f_code, SKIP_CODE, maybe_nn_module);
    // Re-enable custom behavior
    eval_frame_callback_set(callback);
    return eval_frame_default(tstate, frame, throw_flag);
  }
}

static int active_dynamo_threads = 0;

static PyObject* increment_working_threads(PyThreadState* tstate) {
  active_dynamo_threads = active_dynamo_threads + 1;
  if (active_dynamo_threads > 0) {
    enable_eval_frame_shim(tstate);
  }
  Py_RETURN_NONE;
}

static PyObject* decrement_working_threads(PyThreadState* tstate) {
  if (active_dynamo_threads > 0) {
    active_dynamo_threads = active_dynamo_threads - 1;
    if (active_dynamo_threads == 0) {
      enable_eval_frame_default(tstate);
    }
  }
  Py_RETURN_NONE;
}

static PyObject* set_eval_frame(PyObject* new_callback, PyThreadState* tstate) {
  // Change the eval frame callback and return the old one
  //  - None: disables TorchDynamo
  //  - False: run-only mode (reuse existing compiles)
  //  - Python callable(): enables TorchDynamo
  PyObject* old_callback = eval_frame_callback_get();

  // owned by caller
  Py_INCREF(old_callback);

  if (old_callback != Py_None && new_callback == Py_None) {
    decrement_working_threads(tstate);
  } else if (old_callback == Py_None && new_callback != Py_None) {
    increment_working_threads(tstate);
  }

  Py_INCREF(new_callback);
  Py_DECREF(old_callback);

  // Set thread local callback. This will drive behavior of our shim, if/when it
  // is installed.
  eval_frame_callback_set(new_callback);

  is_dynamo_compiling = !(new_callback == Py_None);
  return old_callback;
}

static PyObject* set_eval_frame_py(PyObject* dummy, PyObject* callback) {
  if (callback != Py_None && callback != Py_False &&
      !PyCallable_Check(callback)) {
    DEBUG_TRACE0("arg error");
    PyErr_SetString(PyExc_TypeError, "expected a callable");
    return NULL;
  }
  DEBUG_TRACE(
      "python enabled=%d and is run_only=%d",
      callback != Py_None,
      callback == Py_False);
  return set_eval_frame(callback, PyThreadState_GET());
}

static void destroy_cache_entry_wrapper(PyObject* code) {
  PyObject* extra = NULL;
  _PyCode_GetExtra((PyObject*)code, cache_entry_extra_index, (void*)&extra);
  if (extra != NULL && extra != SKIP_CODE) {
    if (PyObject_IsInstance(extra, (PyObject *)&CacheEntryWrapperType)) {
      CacheEntry* e = ((CacheEntryWrapper*)extra)->cache_entry;
      destroy_cache_entry(e);
    } else if (PyDict_Check(extra)) {
      PyObject* values = PyDict_Values(extra);
      for (Py_ssize_t i = 0; i < PyList_GET_SIZE(values); i++) {
        PyObject* value = PyList_GET_ITEM(values, i);
        if (value != NULL && PyObject_IsInstance(value, (PyObject *)&CacheEntryWrapperType)) {
          CacheEntry* e = ((CacheEntryWrapper*)value)->cache_entry;
          destroy_cache_entry(e);
        }
      }
    }
  }
  set_cache_entry_on_code((PyCodeObject*)code, NULL);
}

static void destroy_frame_state(PyObject* code) {
  PyObject* frame_state = NULL;
  _PyCode_GetExtra((PyObject*)code, dynamic_frame_state_extra_index, (void*)&frame_state);
  Py_XDECREF(frame_state);
  _PyCode_SetExtra((PyObject*)code, dynamic_frame_state_extra_index, NULL);
}

static PyObject* reset_code(PyObject* dummy, PyObject* code) {
  if (!PyCode_Check(code)) {
    DEBUG_TRACE0("arg error");
    PyErr_SetString(PyExc_TypeError, "expected a code object");
    return NULL;
  }
  destroy_cache_entry_wrapper(code);
  destroy_frame_state(code);
  Py_RETURN_NONE;
}

static PyObject* unsupported(PyObject* dummy, PyObject* args) {
  // a dummy C function used in testing
  PyObject* obj1 = NULL;
  PyObject* obj2 = NULL;
  if (!PyArg_ParseTuple(args, "OO", &obj1, &obj2)) {
    return NULL;
  }
  Py_INCREF(obj2);
  return obj2;
}

static PyObject* skip_code(PyObject* dummy, PyObject* obj) {
  if (!PyCode_Check(obj)) {
    PyErr_SetString(PyExc_TypeError, "expected a code object");
    return NULL;
  }
  set_cache_entry_on_code((PyCodeObject*)obj, SKIP_CODE);
  Py_RETURN_NONE;
}

static PyObject* set_guard_fail_hook(PyObject* dummy, PyObject* obj) {
  if (obj == Py_None) {
    obj = NULL;
  }
  Py_XSETREF(guard_fail_hook, Py_XNewRef(obj));
  Py_RETURN_NONE;
}

static PyObject* set_guard_error_hook(PyObject* dummy, PyObject* obj) {
  if (obj == Py_None) {
    obj = NULL;
  }
  Py_XSETREF(guard_error_hook, Py_XNewRef(obj));
  Py_RETURN_NONE;
}

static PyObject* clear_profiler_hooks(PyObject* module, PyObject* unused) {
  Py_CLEAR(profiler_start_hook);
  Py_CLEAR(profiler_end_hook);
  Py_RETURN_NONE;
}

static PyObject* set_profiler_hooks(PyObject* module, PyObject* args) {
  PyObject* start = NULL;
  PyObject* end = NULL;
  if (!PyArg_ParseTuple(args, "OO:set_profiler_hooks", &start, &end)) {
    return NULL;
  }
  if (start == Py_None || end == Py_None) {
    clear_profiler_hooks(module, NULL);
  } else {
    Py_XSETREF(profiler_start_hook, Py_NewRef(start));
    Py_XSETREF(profiler_end_hook, Py_NewRef(end));
  }
  Py_RETURN_NONE;
}

static PyMethodDef _methods[] = {
    {"set_eval_frame", set_eval_frame_py, METH_O, NULL},
    {"reset_code", reset_code, METH_O, NULL},
    {"unsupported", unsupported, METH_VARARGS, NULL},
    {"skip_code", skip_code, METH_O, NULL},
    {"set_guard_fail_hook", set_guard_fail_hook, METH_O, NULL},
    {"set_guard_error_hook", set_guard_error_hook, METH_O, NULL},
    {"set_profiler_hooks", set_profiler_hooks, METH_VARARGS, NULL},
    {"clear_profiler_hooks", clear_profiler_hooks, METH_NOARGS, NULL},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef _module = {
    PyModuleDef_HEAD_INIT,
    "torch._C._dynamo.eval_frame",
    "Module containing hooks to override eval_frame",
    -1,
    _methods};

PyObject* torch_c_dynamo_eval_frame_init(void) {
  cache_entry_extra_index = _PyEval_RequestCodeExtraIndex(ignored);
  if (cache_entry_extra_index < 0) {
    PyErr_SetString(PyExc_RuntimeError,
                    "dynamo: unable to register cache_entry extra index");
    return NULL;
  }

  dynamic_frame_state_extra_index = _PyEval_RequestCodeExtraIndex(ignored);
  if (dynamic_frame_state_extra_index < 0) {
    PyErr_SetString(PyExc_RuntimeError,
                    "dynamo: unable to register dynamic_frame_state extra index");
    return NULL;
  }

  guard_profiler_name_str = PyUnicode_FromString("TorchDynamo Cache Lookup");
  if (guard_profiler_name_str == NULL) {
    return NULL;
  }

  int result = PyThread_tss_create(&eval_frame_callback_key);
  CHECK(result == 0);

  Py_INCREF(Py_None);
  eval_frame_callback_set(Py_None);

  PyObject* module = PyModule_Create(&_module);
  if (module == NULL) {
    return NULL;
  }

#if IS_PYTHON_3_11_PLUS
  if (PyType_Ready(&THPPyInterpreterFrameType) < 0) {
    return NULL;
  }
  Py_INCREF(&THPPyInterpreterFrameType);
  if (PyModule_AddObject(module, "_PyInterpreterFrame", (PyObject*)&THPPyInterpreterFrameType) != 0) {
    return NULL;
  }
#endif

  if (PyType_Ready(&CacheEntryWrapperType) < 0) {
    return NULL;
  }
  Py_INCREF(&CacheEntryWrapperType);
  if (PyModule_AddObject(module, "_CacheEntryPyWrapper", (PyObject *) &CacheEntryWrapperType) < 0) {
      Py_DECREF(&CacheEntryWrapperType);
      return NULL;
  }

  return module;
}

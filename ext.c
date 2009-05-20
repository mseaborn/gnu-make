
#include <Python.h>

#include "hooks.h"


/* TODO: implement this using ctypes instead. */
static PyObject *make_run(PyObject *self, PyObject *args)
{
  PyObject *argv;
  int argv_size;
  char **argv_copy;
  int i;
  if(!PyArg_ParseTuple(args, "O", &argv) ||
     !PyTuple_Check(argv))
    return NULL;
  argv_size = PyTuple_Size(argv);
  argv_copy = PyMem_NEW(char *, argv_size + 1);
  for(i = 0; i < argv_size; i++) {
    if(!PyArg_Parse(PyTuple_GetItem(argv, i), "s", &argv_copy[i])) {
      PyMem_Free(argv_copy);
      return NULL;
    }
  }
  argv_copy[argv_size] = NULL;
  main(argv_size, argv_copy, environ);
  Py_INCREF(Py_None);
  return Py_None;
}

static PyObject *set_hook(PyObject *self, PyObject *args)
{
  int hook_func;
  if(!PyArg_ParseTuple(args, "I", &hook_func))
    return NULL;
  job_hook = hook_func;
  Py_INCREF(Py_None);
  return Py_None;
}

static PyMethodDef module_methods[] = {
  { "run", make_run, METH_VARARGS, "make's main() entry point" },
  { "set_hook", set_hook, METH_VARARGS, "Set hook function" },
  { NULL, NULL, 0, NULL }  /* Sentinel */
};

void initgnumake(void)
{
  PyObject *mod;
  mod = Py_InitModule3("gnumake", module_methods,
                       "GNU Make wrapper");
}

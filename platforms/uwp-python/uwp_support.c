// Host application owns fatal-error/crash reporting; no console backtrace module.
#include "Python.h"
#include "pycore_pylifecycle.h"
PyStatus _PyFaulthandler_Init(int enable){(void)enable;return PyStatus_Ok();}
void _PyFaulthandler_Fini(void){}

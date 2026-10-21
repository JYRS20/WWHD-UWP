// In-process translator host. No shell or external Python executable.
#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "python_host.h"
#include <windows.h>
#include <string>
namespace {
SRWLOCK interpreter_lock=SRWLOCK_INIT;
bool attempted=false;
WwhdPythonReport active_report=nullptr;void* active_context=nullptr;
PyObject* host_write(PyObject*,PyObject* args){
    const char* text=nullptr;Py_ssize_t size=0;
    if(!PyArg_ParseTuple(args,"s#",&text,&size))return nullptr;
    if(active_report&&size)active_report(active_context,text,size);
    return PyLong_FromSsize_t(size);
}
PyMethodDef methods[]{{"write",host_write,METH_VARARGS,nullptr},{nullptr,nullptr,0,nullptr}};
PyModuleDef definition={PyModuleDef_HEAD_INIT,"_wwhd_host",nullptr,-1,methods};
PyObject* initialize_host_module(){return PyModule_Create(&definition);}
void message(const std::string& text){if(active_report)active_report(active_context,text.data(),text.size());}
void python_error(){
    PyObject *type=nullptr,*value=nullptr,*trace=nullptr;PyErr_Fetch(&type,&value,&trace);
    PyErr_NormalizeException(&type,&value,&trace);
    PyObject* text=PyObject_Str(value?value:Py_None);
    const char* utf8=text?PyUnicode_AsUTF8(text):nullptr;
    message(std::string("FAIL embedded translator: ")+(utf8?utf8:"Python error"));
    PyObject* traceback=PyImport_ImportModule("traceback");
    if(traceback){
        PyObject* lines=PyObject_CallMethod(traceback,"format_exception","OOO",type?type:Py_None,value?value:Py_None,trace?trace:Py_None);
        if(lines){PyObject* sep=PyUnicode_FromString("");PyObject* joined=sep?PyUnicode_Join(sep,lines):nullptr;
            if(joined){const char* detail=PyUnicode_AsUTF8(joined);if(detail)message(detail);Py_DECREF(joined);}
            Py_XDECREF(sep);Py_DECREF(lines);}
        Py_DECREF(traceback);
    }
    PyErr_Clear();
    Py_XDECREF(text);Py_XDECREF(type);Py_XDECREF(value);Py_XDECREF(trace);PyErr_Clear();
}
bool set_path(PyObject* globals,const char* name,const wchar_t* path){
    PyObject* value=PyUnicode_FromWideChar(path?path:L"",-1);if(!value)return false;
    int status=PyDict_SetItemString(globals,name,value);Py_DECREF(value);return status==0;
}
}
int wwhd_python_run(const wchar_t* home,const wchar_t* tools,const wchar_t* work,const wchar_t* rpx,const char* script,void* context,WwhdPythonReport report){
    if(!home||!tools||!work||!script||!report)return -1;
    AcquireSRWLockExclusive(&interpreter_lock);
    active_context=context;active_report=report;
    if(attempted){message("FAIL interpreter already used; restart this diagnostic");active_report=nullptr;ReleaseSRWLockExclusive(&interpreter_lock);return -1;}
    attempted=true;
    PyImport_AppendInittab("_wwhd_host",initialize_host_module);
    PyConfig config;PyConfig_InitIsolatedConfig(&config);
    config.parse_argv=0;config.install_signal_handlers=0;config.site_import=0;
    config.write_bytecode=0;config.user_site_directory=0;config.use_environment=0;
    config.legacy_windows_stdio=1;config.module_search_paths_set=1;
    auto zip=std::wstring(home)+L"\\python311.zip";
    PyStatus status=PyConfig_SetString(&config,&config.home,home);
    if(!PyStatus_Exception(status))status=PyConfig_SetString(&config,&config.program_name,L"wwhd-uwp-python.exe");
    if(!PyStatus_Exception(status))status=PyConfig_SetString(&config,&config.stdio_encoding,L"utf-8");
    if(!PyStatus_Exception(status))status=PyWideStringList_Append(&config.module_search_paths,zip.c_str());
    if(!PyStatus_Exception(status))status=PyWideStringList_Append(&config.module_search_paths,tools);
    if(!PyStatus_Exception(status))status=Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);int result=0;
    if(PyStatus_Exception(status)){message(std::string("FAIL Python initialization: ")+(status.err_msg?status.err_msg:"unknown"));if(PyGILState_GetThisThreadState()&&PyErr_Occurred())python_error();result=-2;}
    else{
        PyObject* main=PyImport_AddModule("__main__");PyObject* globals=main?PyModule_GetDict(main):nullptr;
        bool paths=globals&&set_path(globals,"wwhd_tools",tools)&&set_path(globals,"wwhd_work",work)&&set_path(globals,"wwhd_rpx",rpx);
        const char* capture="import sys, _wwhd_host\nclass _WwhdSink:\n def write(self,text): return _wwhd_host.write(text)\n def flush(self): pass\nsys.stdout=sys.stderr=_WwhdSink()\n";
        PyObject* value=paths?PyRun_StringFlags(capture,Py_file_input,globals,globals,nullptr):nullptr;
        if(value){Py_DECREF(value);value=PyRun_StringFlags(script,Py_file_input,globals,globals,nullptr);}
        if(!value){python_error();result=-3;}else Py_DECREF(value);
        if(Py_FinalizeEx()<0&&result==0)result=-4;
    }
    active_report=nullptr;active_context=nullptr;ReleaseSRWLockExclusive(&interpreter_lock);return result;
}

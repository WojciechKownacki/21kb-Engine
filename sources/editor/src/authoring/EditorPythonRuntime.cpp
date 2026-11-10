#include "kb/editor/authoring/EditorAuthoring.hpp"
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#if defined(KB_EDITOR_HAS_PYTHON)
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif
namespace kb::editor {
#if defined(KB_EDITOR_HAS_PYTHON)
namespace {
thread_local IEditorAuthoringHost* activeHost = nullptr;
void* interpreterOwner = nullptr;
std::mutex interpreterMutex;
PyObject* Dispatch(PyObject*, PyObject* args) {
    const char* text = nullptr;
    Py_ssize_t size = 0;
    if (!PyArg_ParseTuple(args, "s#", &text, &size))
        return nullptr;
    if (!activeHost) {
        PyErr_SetString(PyExc_RuntimeError, "kb21 authoring is only available during an owner-thread editor script");
        return nullptr;
    }
    try {
        std::string response, error;
        if (!activeHost->Request(std::string_view{text, static_cast<std::size_t>(size)}, response, error)) {
            PyErr_SetString(PyExc_ValueError, error.c_str());
            return nullptr;
        }
        return PyUnicode_DecodeUTF8(response.data(), static_cast<Py_ssize_t>(response.size()), "strict");
    } catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    } catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "editor authoring raised an unknown exception");
        return nullptr;
    }
}
PyMethodDef methods[] = {{"dispatch", Dispatch, METH_VARARGS, "Execute one in-process editor authoring request."},
                         {nullptr, nullptr, 0, nullptr}};
PyModuleDef module = {PyModuleDef_HEAD_INIT, "_kb21_editor", nullptr, -1, methods, nullptr, nullptr, nullptr, nullptr};
PyObject* InitModule() {
    return PyModule_Create(&module);
}
std::string PythonError() {
    PyObject* exception = PyErr_GetRaisedException();
    if (!exception)
        return "Python execution failed";
    std::string error = Py_TYPE(exception)->tp_name;
    PyObject* value = PyObject_Str(exception);
    if (value) {
        if (const auto* utf8 = PyUnicode_AsUTF8(value)) {
            error += ": ";
            error += utf8;
        }
        Py_DECREF(value);
    }
    Py_DECREF(exception);
    PyErr_Clear();
    return error;
}
constexpr const char* Bindings = R"python(import json as _json
from _kb21_editor import dispatch as _dispatch
def _call(op, **fields):
    return _json.loads(_dispatch(_json.dumps(dict(op=op, **fields), allow_nan=False)))
def create_entity(name, preset="Empty"):
    return _call("create_entity", name=name, preset=preset)["entity"]
def find_entity(name):
    return _call("find_entity", name=name)["entity"]
def delete_entity(entity):
    _call("delete_entity", entity=entity)
def add_component(entity, component):
    _call("add_component", entity=entity, component=component)
def get_property(entity, component, property):
    return _call("get_property", entity=entity, component=component, property=property)["value"]
def set_property(entity, component, property, value):
    _call("set_property", entity=entity, component=component, property=property, value=value)
def save_scene():
    _call("save_scene")
def undo():
    _call("undo")
def redo():
    _call("redo")
def schema():
    return _call("schema")["components"]
def log(message):
    _call("log", message=message)
)python";
} // namespace
#endif
struct EditorPythonRuntime::Impl {
    std::thread::id owner = std::this_thread::get_id();
    bool initialized = false;
    bool ready = false;
    ~Impl() {
#if defined(KB_EDITOR_HAS_PYTHON)
        if (initialized && std::this_thread::get_id() == owner) {
            activeHost = nullptr;
            Py_FinalizeEx();
            std::lock_guard guard(interpreterMutex);
            interpreterOwner = nullptr;
        }
#endif
    }
    bool Initialize(std::string& error) {
#if defined(KB_EDITOR_HAS_PYTHON)
        std::lock_guard guard(interpreterMutex);
        if (initialized) {
            if (!ready)
                error = "embedded Python initialization did not complete";
            return ready;
        }
        if (interpreterOwner || Py_IsInitialized()) {
            error = "another embedded Python runtime already owns this process";
            return false;
        }
        std::wstring executable(32768, L'\0');
        const auto count = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!count || count >= executable.size()) {
            error = "cannot locate editor Python runtime";
            return false;
        }
        executable.resize(count);
        const auto root = std::filesystem::path{executable}.parent_path() / L"Python";
        const auto archive = root / L"python314.zip";
        if (!std::filesystem::exists(archive)) {
            error = "bundled editor Python standard library is missing";
            return false;
        }
        if (PyImport_AppendInittab("_kb21_editor", InitModule) < 0) {
            error = "cannot register embedded authoring module";
            return false;
        }
        PyConfig config;
        PyConfig_InitIsolatedConfig(&config);
        config.site_import = 0;
        config.write_bytecode = 0;
        config.parse_argv = 0;
        config.install_signal_handlers = 0;
        config.module_search_paths_set = 1;
        PyStatus status = PyConfig_SetString(&config, &config.program_name, executable.c_str());
        if (!PyStatus_Exception(status))
            status = PyConfig_SetString(&config, &config.home, root.c_str());
        if (!PyStatus_Exception(status))
            status = PyWideStringList_Append(&config.module_search_paths, archive.c_str());
        if (!PyStatus_Exception(status))
            status = PyWideStringList_Append(&config.module_search_paths, root.c_str());
        if (!PyStatus_Exception(status))
            status = Py_InitializeFromConfig(&config);
        if (PyStatus_Exception(status)) {
            error = status.err_msg ? status.err_msg : "cannot initialize embedded Python";
            PyConfig_Clear(&config);
            if (Py_IsInitialized())
                Py_FinalizeEx();
            return false;
        }
        PyConfig_Clear(&config);
        initialized = true;
        interpreterOwner = this;
        auto* kb21 = PyImport_AddModule("kb21");
        if (!kb21) {
            error = PythonError();
            return false;
        }
        auto* globals = PyModule_GetDict(kb21);
        auto* result = PyRun_StringFlags(Bindings, Py_file_input, globals, globals, nullptr);
        if (!result) {
            error = PythonError();
            return false;
        }
        Py_DECREF(result);
        ready = true;
        return true;
#else
        error = "embedded editor Python is unavailable on this host";
        return false;
#endif
    }
};
EditorPythonRuntime::EditorPythonRuntime() : impl_(std::make_unique<Impl>()) {}
EditorPythonRuntime::~EditorPythonRuntime() = default;
bool EditorPythonRuntime::Execute(IEditorAuthoringHost& host, std::string_view source, std::string_view filename,
                                  std::string& error) {
    error.clear();
    if (std::this_thread::get_id() != impl_->owner) {
        error = "Python runtime requires its owner thread";
        return false;
    }
    if (source.size() > 1024 * 1024 || source.find('\0') != std::string_view::npos ||
        filename.find('\0') != std::string_view::npos) {
        error = "Python script must be UTF-8 text up to 1 MiB without NUL";
        return false;
    }
    if (!impl_->Initialize(error))
        return false;
#if defined(KB_EDITOR_HAS_PYTHON)
    if (activeHost) {
        error = "nested editor Python execution is unavailable";
        return false;
    }
    struct HostScope {
        explicit HostScope(IEditorAuthoringHost& h) {
            activeHost = &h;
        }
        ~HostScope() {
            activeHost = nullptr;
        }
    } scope{host};
    PyObject* globals = PyDict_New();
    if (!globals) {
        error = PythonError();
        return false;
    }
    PyObject* name = PyUnicode_FromString("__21kb_authoring__");
    PyObject* file = PyUnicode_DecodeUTF8(filename.data(), static_cast<Py_ssize_t>(filename.size()), "strict");
    if (!name || !file || PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins()) < 0 ||
        PyDict_SetItemString(globals, "__name__", name) < 0 || PyDict_SetItemString(globals, "__file__", file) < 0) {
        Py_XDECREF(name);
        Py_XDECREF(file);
        Py_DECREF(globals);
        error = PythonError();
        return false;
    }
    Py_DECREF(name);
    Py_DECREF(file);
    const std::string text{source}, label{filename};
    PyObject* code = Py_CompileString(text.c_str(), label.c_str(), Py_file_input);
    PyObject* result = code ? PyEval_EvalCode(code, globals, globals) : nullptr;
    const bool succeeded = result != nullptr;
    if (!succeeded)
        error = label + ": " + PythonError();
    Py_XDECREF(result);
    Py_XDECREF(code);
    Py_DECREF(globals);
    return succeeded;
#else
    static_cast<void>(host);
    return false;
#endif
}
bool EditorPythonRuntime::ExecuteFile(IEditorAuthoringHost& host, const std::filesystem::path& path,
                                      std::string& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > 1024 * 1024) {
        error = "cannot read Python script or script exceeds 1 MiB";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>{input}, {}};
    if (!input || text.size() > 1024 * 1024) {
        error = "cannot read Python script";
        return false;
    }
    const auto name = path.generic_u8string();
    return Execute(host, text, std::string_view{reinterpret_cast<const char*>(name.data()), name.size()}, error);
}
} // namespace kb::editor

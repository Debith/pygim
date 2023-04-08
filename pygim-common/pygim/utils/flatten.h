#include <Python.h>

class FlattenGenerator {
public:
    FlattenGenerator(PyObject* items) : items_(items), index_(0) { }

    PyObject* next() {
        while (PyList_Check(items_)) {
            Py_ssize_t size = PyList_Size(items_);
            if (index_ < size) {
                PyObject* subitem = PyList_GetItem(items_, index_++);
                items_ = subitem;
            } else {
                break;
            }
        }
        if (PyList_Check(items_)) {
            Py_RETURN_NONE;
        } else {
            Py_INCREF(items_);
            PyObject* result = items_;
            items_ = NULL;
            return result;
        }
    }

private:
    PyObject* items_;
    Py_ssize_t index_;
};

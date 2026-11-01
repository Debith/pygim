// code/bindings.cpp — registers pygim.code: Python source parsed in C++ into a corpus of syntax
// trees, and views to read them. The registrations live in adapter/corpus_bindings.h.

#include <pybind11/pybind11.h>

#include "adapter/corpus_bindings.h"

namespace py = pybind11;

PYBIND11_MODULE(code, m) {
    m.doc() = "Python source parsed in C++: a corpus of syntax trees, and views to read them.";
    pygim::code::register_corpus(m);
}

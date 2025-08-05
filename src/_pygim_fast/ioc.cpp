#include <pybind11/pybind11.h>
#include <pybind11/stl.h>         // for automatic vector<>, optional<>, etc.

namespace py = pybind11;

// 1) Define your C++ descriptor type
struct ServiceDescriptor {
    py::object interface;          // the Python‐level interface/type
    py::object provider;           // the Python‐level class or factory
    std::string lifecycle;         // "transient" / "singleton" / …
    std::optional<std::string> name;  // named registrations
    std::vector<py::object> decorators; // AOP wrappers

    // default and value constructors are all you need to make it STL‐compatible
};

// 2) Expose it in your PYBIND11_MODULE
PYBIND11_MODULE(ioc, m) {
    // Bind the descriptor class
    py::class_<ServiceDescriptor>(m, "ServiceDescriptor")
        .def(py::init<
             py::object,                 // interface
             py::object,                 // provider
             std::string,                // lifecycle
             std::optional<std::string>, // name
             std::vector<py::object>     // decorators
        >(),
        py::arg("interface"),
        py::arg("provider"),
        py::arg("lifecycle") = "transient",
        py::arg("name")      = std::nullopt,
        py::arg("decorators")= std::vector<py::object>())
        .def_readonly("interface",  &ServiceDescriptor::interface)
        .def_readonly("provider",   &ServiceDescriptor::provider)
        .def_readonly("lifecycle",  &ServiceDescriptor::lifecycle)
        .def_readonly("name",       &ServiceDescriptor::name)
        .def_readonly("decorators", &ServiceDescriptor::decorators);

    // 3) Bind std::vector<ServiceDescriptor> so Python can see it as a list-like type
    py::bind_vector<std::vector<ServiceDescriptor>>(m, "ServiceDescriptorVector");

    // …and your Container class would hold a std::vector<ServiceDescriptor> internally…
}

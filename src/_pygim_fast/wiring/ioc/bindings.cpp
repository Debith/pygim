#include <pybind11/pybind11.h>

#include <algorithm>
#include <string>

#include "../common/adapter_errors.h"
#include "adapter.h"

namespace py = pybind11;

PYBIND11_MODULE(ioc, m) {
    using Descriptor = pygim::Container::DescriptorType;

    m.doc() = "IoC container for provider registration and resolution.";
    pygim::wiring::detail::register_wiring_errors();   // a missing key is KeyError

    py::class_<Descriptor>(m, "ServiceDescriptor",
        "Read-only snapshot of a registration; mutating it does not affect the container.")
        .def(py::init([](py::object interface,
                         py::object provider,
                         std::string lifecycle,
                         py::object name,
                         std::vector<py::object> decorators,
                         bool autowire) {
                 return Descriptor{
                     std::move(interface),
                     std::move(provider),
                     pygim::core::parse_lifecycle(lifecycle),
                     pygim::normalize_name(name),
                     std::move(decorators),
                     autowire};
             }),
             py::arg("interface"),
             py::arg("provider"),
             py::arg("lifecycle") = "transient",
             py::arg("name") = py::none(),
             py::arg("decorators") = std::vector<py::object>{},
             py::arg("autowire") = false)
        .def_readonly("interface", &Descriptor::interface)
        .def_readonly("provider", &Descriptor::provider)
        .def_property_readonly(
            "lifecycle",
            [](const Descriptor& descriptor) {
                return std::string(pygim::core::lifecycle_to_string(descriptor.lifecycle));
            })
        .def_readonly("name", &Descriptor::name)
        .def_readonly("decorators", &Descriptor::decorators)
        .def_readonly("autowire", &Descriptor::autowire)
        .def("__eq__", [](const Descriptor& a, const Descriptor& b) {
                 const auto same = [](const py::object& x, const py::object& y) { return x.equal(y); };
                 return same(a.interface, b.interface) && same(a.provider, b.provider) && a.lifecycle == b.lifecycle &&
                        a.name == b.name && a.autowire == b.autowire && std::ranges::equal(a.decorators, b.decorators, same);
             }, py::is_operator(), py::arg("other"),
             "The same registration: interface, provider, lifecycle, name, decorators and autowire.")   // NotImplemented for any other type
        .def("__repr__", [](const Descriptor& d) {
                 std::string out = "ServiceDescriptor(" + py::repr(d.interface).cast<std::string>() + ", " + py::repr(d.provider).cast<std::string>();
                 if (d.lifecycle != pygim::core::Lifecycle::Transient)
                     out += ", lifecycle='" + std::string(pygim::core::lifecycle_to_string(d.lifecycle)) + "'";
                 if (d.name) out += ", name=" + py::repr(py::str(*d.name)).cast<std::string>();
                 if (!d.decorators.empty()) out += ", decorators=" + py::repr(py::cast(d.decorators)).cast<std::string>();
                 if (d.autowire) out += ", autowire=True";
                 return out + ")";
             });

    py::class_<pygim::Container>(m, "Container",
        "IoC container mapping interfaces to providers.\n\n"
        "Thread safety: consistency relies on the GIL. If a provider releases\n"
        "the GIL (e.g. blocking I/O in __init__), two threads racing on the\n"
        "same not-yet-cached singleton can each construct an instance; guard\n"
        "concurrent first-resolves externally when providers may block.")
        .def(py::init<std::size_t>(), py::arg("capacity") = 0)
        .def("register",
             [](py::object self,
                py::object interface,
                py::object provider_or_none,
                py::object name,
                std::string lifecycle,
                std::vector<py::object> decorators,
                bool autowire,
                bool override_existing) -> py::object {
                 auto& container = self.cast<pygim::Container&>();
                 if (provider_or_none.is_none()) {
                     // Capture `self` (not a raw C++ reference) so the
                     // returned decorator keeps the container alive, and copy
                     // `decorators` per call so the decorator stays reusable.
                     py::cpp_function decorator(
                         [self = std::move(self),
                          interface = std::move(interface),
                          name = std::move(name),
                          lifecycle = std::move(lifecycle),
                          decorators = std::move(decorators),
                          autowire,
                          override_existing](py::object provider) {
                             auto& bound = self.cast<pygim::Container&>();
                             bound.register_service(interface, provider, name, lifecycle, decorators, autowire, override_existing);
                             return provider;
                         });
                     return py::object(std::move(decorator));
                 }

                 container.register_service(interface, provider_or_none, name, std::move(lifecycle), std::move(decorators), autowire, override_existing);
                 return provider_or_none;
             },
             py::arg("interface"),
             py::arg("provider") = py::none(),
             py::kw_only(),
             py::arg("name") = py::none(),
             py::arg("lifecycle") = "transient",
             py::arg("decorators") = std::vector<py::object>{},
             py::arg("autowire") = false,
             py::arg("override") = false,
             "Register a provider directly or use as a decorator.\n\n"
             "Supports optional name, transient/singleton lifecycle, decorator call chain, opt-in class autowiring, and strict override semantics.")
        .def("resolve", &pygim::Container::resolve, py::arg("key"))
        .def("describe", &pygim::Container::describe, py::arg("key"),
             "Return a read-only ServiceDescriptor snapshot of the registration for `key`.")
        .def("registered_keys", &pygim::Container::registered_keys)
        .def("__getitem__", &pygim::Container::operator[])
        .def("__contains__", &pygim::Container::contains)
        .def("__len__", &pygim::Container::size)
        .def("__repr__", &pygim::Container::repr);
}

// ioc.cpp
#include "ioc.h"
#include <stdexcept>

namespace ioc {

ServiceDescriptor::ServiceDescriptor(
    py::object interface_,
    py::object provider_,
    std::string lifecycle_,
    std::optional<std::string> name_,
    std::vector<py::object> decorators_
) : m_interface(std::move(interface_)),
    m_provider(std::move(provider_)),
    m_lifecycle(std::move(lifecycle_)),
    m_name(std::move(name_)),
    m_decorators(std::move(decorators_)) {}

Container::Container() { m_registry.reserve(16); }

void Container::register_service(
    py::object interface,
    py::object provider,
    std::optional<std::string> name,
    std::string lifecycle,
    std::vector<py::object> decorators
) {
    ServiceDescriptor desc(interface, provider, lifecycle, name, decorators);
    size_t idx = m_registry.size();
    m_registry.emplace_back(std::move(desc));
    m_index_map.emplace(
        ServiceKey(interface.ptr(), name), idx
    );
}

py::object Container::resolve(py::object key) {
    // key can be interface or (interface, name)
    py::object interface;
    std::optional<std::string> name;
    if (PyTuple_Check(key.ptr()) && PyTuple_Size(key.ptr()) == 2) {
        interface = key.attr("__getitem__")(0);
        name = key.attr("__getitem__")(1).is_none()
             ? std::nullopt
             : std::optional<std::string>(key.attr("__getitem__")(1).cast<std::string>());
    } else {
        interface = key;
    }
    ServiceKey sk(interface.ptr(), name);
    auto it = m_index_map.find(sk);
    if (it == m_index_map.end()) throw std::runtime_error("No provider for key");
    ServiceDescriptor &desc = m_registry[it->second];

    if (desc.m_lifecycle == "singleton") {
        auto sit = m_singletons.find(it->second);
        if (sit != m_singletons.end()) return sit->second;
    }
    // instantiate without args
    py::object instance = desc.m_provider();
    for (auto &dec : desc.m_decorators) instance = dec(instance);
    if (desc.m_lifecycle == "singleton") m_singletons[it->second] = instance;
    return instance;
}

} // namespace ioc

// Bindings
PYBIND11_MAKE_OPAQUE(std::vector<ioc::ServiceDescriptor>);

PYBIND11_MODULE(ioc, m) {
    namespace py = pybind11;

    py::class_<ioc::ServiceDescriptor>(m, "ServiceDescriptor")
        .def(py::init<
            py::object, py::object,
            std::string, std::optional<std::string>,
            std::vector<py::object>
        >(),
        py::arg("interface"),
        py::arg("provider"),
        py::arg("lifecycle") = "transient",
        py::arg("name") = std::nullopt,
        py::arg("decorators") = std::vector<py::object>())
        .def_readwrite("interface", &ioc::ServiceDescriptor::m_interface)
        .def_readwrite("provider",  &ioc::ServiceDescriptor::m_provider)
        .def_readwrite("lifecycle", &ioc::ServiceDescriptor::m_lifecycle)
        .def_readwrite("name",      &ioc::ServiceDescriptor::m_name)
        .def_readwrite("decorators",&ioc::ServiceDescriptor::m_decorators);

    py::bind_vector<std::vector<ioc::ServiceDescriptor>>(m, "ServiceDescriptorVector");

    py::class_<ioc::Container>(m, "Container")
        .def(py::init<>())
        .def("register", &ioc::Container::register_service,
             py::arg("interface"), py::arg("provider"),
             py::arg("name") = std::nullopt,
             py::arg("lifecycle") = "transient",
             py::arg("decorators") = std::vector<py::object>())
        .def("resolve", &ioc::Container::resolve,
             py::arg("key"))
        .def("__getitem__", &ioc::Container::operator[]);
}

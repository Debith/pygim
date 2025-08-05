// ioc.h
#ifndef IOC_H
#define IOC_H

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>
#include <string>
#include <optional>
#include <vector>
#include <unordered_map>

namespace ioc {
namespace py = pybind11;

// Key for service lookup
class ServiceKey {
public:
    PyObject* ptr;                          // interface class pointer
    std::optional<std::string> name;        // optional registration name

    ServiceKey(PyObject* p, std::optional<std::string> n)
        : ptr(p), name(std::move(n)) {}

    bool operator==(ServiceKey const &other) const noexcept {
        return ptr == other.ptr && name == other.name;
    }
};

// Provide std::hash specialization for ServiceKey
} // namespace ioc

namespace std {
template<>
struct hash<ioc::ServiceKey> {
    size_t operator()(ioc::ServiceKey const &k) const noexcept {
        size_t h1 = std::hash<PyObject*>()(k.ptr);
        size_t h2 = k.name ? std::hash<std::string>()(*k.name) : 0;
        return h1 ^ (h2 << 1);
    }
};
} // namespace std

namespace ioc {

// Descriptor for a registered service
struct ServiceDescriptor {
    py::object m_interface;
    py::object m_provider;
    std::string m_lifecycle;
    std::optional<std::string> m_name;
    std::vector<py::object> m_decorators;

    ServiceDescriptor(
        py::object interface_,
        py::object provider_,
        std::string lifecycle_ = "transient",
        std::optional<std::string> name_ = std::nullopt,
        std::vector<py::object> decorators_ = {}
    );
};

// IoC container
class Container {
public:
    Container();

    void register_service(
        py::object interface,
        py::object provider,
        std::optional<std::string> name = std::nullopt,
        std::string lifecycle = "transient",
        std::vector<py::object> decorators = {}
    );

    // support container[key] in Python
    py::object resolve(py::object key);
    py::object operator[](py::object key) { return resolve(key); }

private:
    std::vector<ServiceDescriptor> m_registry;
    std::unordered_map<ServiceKey, size_t> m_index_map;
    std::unordered_map<size_t, py::object> m_singletons;
};

} // namespace ioc

#endif // IOC_H



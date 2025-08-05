// ioc.h
#ifndef IOC_H
#define IOC_H

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <optional>
#include <vector>
#include <tuple>
#include <unordered_map>

namespace ioc {

namespace py = pybind11;

// Descriptor for a registered service
struct ServiceDescriptor {
    py::object interface;         // The Python-level interface/type
    py::object provider;          // The implementing class or factory
    std::string lifecycle;        // "transient" or "singleton"
    std::optional<std::string> name;           // Optional key
    std::vector<py::object> decorators;        // AOP-style wrappers

    ServiceDescriptor(
        py::object interface_,
        py::object provider_,
        const std::string &lifecycle_ = "transient",
        const std::optional<std::string> &name_ = std::nullopt,
        const std::vector<py::object> &decorators_ = {}
    )
    : interface(std::move(interface_)),
      provider(std::move(provider_)),
      lifecycle(lifecycle_),
      name(name_),
      decorators(decorators_)
    {}
};

// A simple IoC container
class Container {
public:
    Container();

    // Register a service descriptor
    void register_service(
        py::object interface,
        py::object provider,
        std::optional<std::string> name = std::nullopt,
        const std::string &lifecycle = "transient",
        std::vector<py::object> decorators = {}
    );

    // Resolve an instance by interface and optional name
    py::object resolve(
        py::object interface,
        std::optional<std::string> name,
        py::args args,
        py::kwargs kwargs
    );

private:
    // storage for descriptors and singleton instances
    std::vector<ServiceDescriptor> registry_;
    std::unordered_map<
        std::tuple<PyObject*, std::optional<std::string>>,
        size_t
    > index_map_;
    std::unordered_map<size_t, py::object> singletons_;
};

} // namespace ioc

#endif // IOC_H

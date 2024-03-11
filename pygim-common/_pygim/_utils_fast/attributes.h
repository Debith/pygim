#include <pybind11/pybind11.h>
#include <stdexcept> // For std::runtime_error
#include <pybind11/stl.h> // For STL containers compatibility
#include <pybind11/functional.h> // For std::function
#include <vector>

namespace py = pybind11;


static const py::object DROPPED = py::object();


static py::object sentinel_object() {
  static py::object sentinel;

  if (sentinel.ptr() == nullptr) {
    sentinel = py::capsule(&sentinel);
    sentinel.inc_ref();
  }

  return sentinel;
}


class Undefined : public py::object {
public:
    PYBIND11_OBJECT(Undefined, object, [](py::handle h) { return h.is(sentinel_object()); });
    Undefined() : py::object(sentinel_object(), stolen_t{}) {}
    ~Undefined() = default;

    static py::object instance() {
        static const py::object undefined = Undefined();
        return undefined;
    }
};

// Create a unique object to serve as our UNDEFINED value.

py::object smart_getattr(py::object& obj,
                         const std::string& name,
                         bool autocall = true,
                         const py::object& defaultValue = Undefined(),
                         py::tuple args = py::tuple(),
                         py::dict kwargs = py::dict()
                         ) {
    if (!py::hasattr(obj, name.c_str())) {
        if (defaultValue.is(Undefined())) {
            throw std::runtime_error("AttributeError: '" + std::string(py::str(obj)) + "' object has no attribute '" + name + "'");
        }
        return defaultValue;
    }

    py::object value = obj.attr(name.c_str());
    if (autocall && py::isinstance<py::function>(value)) {
        return value(*args, **kwargs);
    }
    return value;
}


class MultiCall {
public:
    // Constructor with default values and optional parameters
    MultiCall(py::list objs = py::list(),
              std::string funcName = "",
              const py::object& factory = Undefined(),
              bool withObj = false,
              bool autocall = true,
              const py::object& defaultValue = Undefined()
              )
    : m_objs(objs),
      m_funcName(funcName),
      m_factory(factory),
      m_withObj(withObj || py::isinstance<py::dict>(factory)),
      m_autocall(autocall),
      m_default(defaultValue) {
        if (defaultValue.is(py::ellipsis())) {
            m_default = DROPPED;
        }
    }

    // Method to iterate over attributes
    py::iterable iter_attributes() {
        py::list result;
        for (auto obj : m_objs) {
            auto value = py::getattr(obj, m_funcName.c_str(), Undefined());
            if (value.is(Undefined())) {
                throw std::runtime_error("AttributeError: '" + std::string(py::str(obj)) + "' has no attribute '" + m_funcName + "'");
            } else if (value.is(DROPPED)) {
                continue;
            }
            result.append(py::make_tuple(obj, value));
        }
        return result;
    }

    // Method to iterate over values and possibly call them
    py::iterable iter_values(py::args args, py::kwargs kwargs) {
        py::list result;
        for (auto attr : iter_attributes()) {
            auto[obj, value] = attr.template cast<std::tuple<py::object, py::object>>();
            if (m_autocall && py::isinstance<py::function>(value)) {
                value = value(*args, **kwargs);
            }
            if (m_withObj) {
                result.append(py::make_tuple(obj, value));
            } else {
                result.append(value);
            }
        }
        return result;
    }

    // Call operator to apply arguments and keyword arguments
    py::object operator()(py::args args, py::kwargs kwargs) {
        if (py::len(args) >= 2 && py::isinstance<py::iterable>(args[0]) && py::isinstance<py::str>(args[1])) {
            m_objs = args[0];
            m_funcName = args[1].cast<std::string>();
            py::list new_args;
            for (size_t i = 2; i < py::len(args); ++i) {
                new_args.append(args[i]);
            }
            args = py::tuple(new_args);
        }
        if (m_factory.is(py::ellipsis())) {
            return iter_values(args, kwargs);
        } else {
            return m_factory(iter_values(args, kwargs));
        }
    }

private:
    py::list m_objs;
    std::string m_funcName;
    py::object m_factory;
    bool m_withObj;
    bool m_autocall;
    py::object m_default;
};
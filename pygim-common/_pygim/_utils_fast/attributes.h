#include <pybind11/pybind11.h>
#include <stdexcept> // For std::runtime_error
#include <pybind11/stl.h> // For STL containers compatibility
#include <pybind11/functional.h> // For std::function
#include <vector>

namespace py = pybind11;

// Assuming UNDEFINED and DROPPED are defined elsewhere in the code, similar to the previous example
extern const py::object DROPPED;
static const py::object UNDEFINED; // = py::cast(new int(0), py::return_value_policy::take_ownership);

namespace py = pybind11;

// Create a unique object to serve as our UNDEFINED value.

py::object smart_getattr(py::object obj, const std::string& name, bool autocall = true, py::object default_value = UNDEFINED, py::tuple args = py::tuple(), py::dict kwargs = py::dict()) {
    if (!py::hasattr(obj, name.c_str())) {
        if (default_value.is(UNDEFINED)) {
            throw std::runtime_error("AttributeError: '" + std::string(py::str(obj)) + "' object has no attribute '" + name + "'");
        }
        return default_value;
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
    MultiCall(py::list objs = py::list(), std::string func_name = "", py::object factory = UNDEFINED, bool with_obj = false, bool autocall = true, py::object default_value = UNDEFINED)
    : objs_(objs), func_name_(func_name), factory_(factory), with_obj_(with_obj || py::isinstance<py::dict>(factory)), autocall_(autocall), default_(default_value) {
        if (default_value.is(py::ellipsis())) {
            default_ = DROPPED;
        }
    }

    // Method to iterate over attributes
    py::iterable iter_attributes() {
        py::list result;
        for (auto obj : objs_) {
            auto value = obj.attr(func_name_.c_str());
            if (value.is(UNDEFINED)) {
                throw std::runtime_error("AttributeError: '" + std::string(py::str(obj)) + "' has no attribute '" + func_name_ + "'");
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
            if (autocall_ && py::isinstance<py::function>(value)) {
                value = value(*args, **kwargs);
            }
            if (with_obj_) {
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
            objs_ = args[0];
            func_name_ = args[1].cast<std::string>();
            args = args.slice(2, py::len(args)); // Slice args to remove the first two elements
        }
        if (factory_.is(py::ellipsis())) {
            return iter_values(args, kwargs);
        } else {
            return factory_(iter_values(args, kwargs));
        }
    }

private:
    py::list objs_;
    std::string func_name_;
    py::object factory_;
    bool with_obj_;
    bool autocall_;
    py::object default_;
};
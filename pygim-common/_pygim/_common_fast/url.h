#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <sstream>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

class Url {
private:
    std::vector<std::string> _url;
    std::map<std::string, std::string> _params;

public:
    // Constructors
    Url();
    Url(const Url& other);
    Url(const std::string& value);
    Url(const std::vector<std::string>& valueList);

    // Utility function for string splitting
    std::vector<std::string> split(const std::string& s, char delimiter);

    std::string str();
    Url operator/(const std::string& other);
    Url operator/(const Url& other);

    Url withParams(const std::map<std::string, std::string>& mapping) const;
    Url operator|(const std::map<std::string, std::string>& other);

    // Exposing to Python
    static void expose_to_python(py::module& m) {
        py::class_<Url>(m, "Url")
            .def(py::init<>())  // Default constructor
            .def(py::init<const Url&>())  // Constructor with Url
            .def(py::init<const std::string&>())  // Constructor with string
            .def(py::init<const std::vector<std::string>&>())  // Constructor with list of strings

            .def("__str__", &Url::str)

            .def("__truediv__", py::overload_cast<const Url&>(&Url::operator/))  // Division operator with Url
            .def("__truediv__", py::overload_cast<const std::string&>(&Url::operator/))  // Division operator with string

            .def("with_params", [](const Url& u, const py::kwargs& kwargs) {
                std::map<std::string, std::string> params_map;
                for (auto item : kwargs) {
                    params_map[item.first.cast<std::string>()] = item.second.cast<std::string>();
                }
                return u.withParams(params_map);
            })
            .def("__or__", &Url::operator|)  // or-operator binding
            ;
    }
};

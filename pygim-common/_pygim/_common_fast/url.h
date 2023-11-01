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
    std::string mScheme;
    std::string mUsername;
    std::string mPassword;
    std::string mHost;
    uint16_t mPort;
    std::vector<std::string> mPath;
    std::string mAuthority;
    std::string mQuery;
    std::string mFragment;

    std::map<std::string, std::string> _params;

public:
    // Constructors
    Url();
    Url(const Url& other);
    Url(const std::string& value);
    Url(const std::map<std::string, std::string>& parts);
    Url(const std::string& scheme,
        const std::string& username,
        const std::string& password,
        const std::string& host,
        const uint16_t port,
        const std::string& path,
        const std::string& query,
        const std::string& fragment
    );

    // Utility function for string splitting
    std::vector<std::string> split(const std::string& s, char delimiter, bool allowEmpty=false);

    const std::string str() const;
    Url operator/(const std::string& other);
    Url operator/(const Url& other);

    bool operator==(const Url& other) const;
    bool operator!=(const Url& other) const;

    Url withParams(const std::map<std::string, std::string>& mapping) const;
    Url operator|(const std::map<std::string, std::string>& other);

    inline const std::string scheme() const { return mScheme; };
    inline const std::string username() const { return mUsername; };
    inline const std::string password() const { return mPassword; };
    inline const std::string host() const { return mHost; };
    inline const uint16_t port() const { return mPort; };
    inline const std::vector<std::string> path() const { return mPath; };
    inline const std::string authority() const { return mAuthority; };
    inline const std::string query() const { return mQuery; };
    inline const std::string fragment() const { return mFragment; };

    /*
    bool empty() const noexcept;
    bool isAbsolute() const;
    bool isOpaque() const;
    bool isValid() const;
    */

    // Exposing to Python
    static void expose_to_python(py::module& m) {
        py::class_<Url>(m, "Url")
            .def(py::init<>()) // Default constructor
            .def(py::init<const Url&>()) // Copy constructor
            .def(py::init<const std::string&>()) // Construct from string
            .def(py::init<const std::map<std::string, std::string>&>()) // Construct from map
            .def(py::init<const std::string&,
                          const std::string&,
                          const std::string&,
                          const std::string&,
                          const uint16_t,
                          const std::string&,
                          const std::string&,
                          const std::string&>(),
                py::arg("scheme") = "",
                py::arg("username") = "",
                py::arg("password") = "",
                py::arg("host") = "",
                py::arg("port") = 0,
                py::arg("path") = "",
                py::arg("query") = "",
                py::arg("fragment") = "") // Detailed constructor
            .def("__repr__", [](const Url& u) {
                return "Url(\"" + u.str() + "\")";
            })
            .def("__str__", &Url::str)

            .def("__truediv__", py::overload_cast<const Url&>(&Url::operator/))  // Division operator with Url
            .def("__truediv__", py::overload_cast<const std::string&>(&Url::operator/))  // Division operator with string

            .def("__eq__", &Url::operator==)
            .def("__ne__", &Url::operator!=)

            .def_property_readonly("scheme", &Url::scheme)
            .def_property_readonly("username", &Url::username)
            .def_property_readonly("password", &Url::password)
            .def_property_readonly("host", &Url::host)
            .def_property_readonly("port", &Url::port)
            .def_property_readonly("path", &Url::path)
            .def_property_readonly("authority", &Url::authority)
            .def_property_readonly("query", &Url::query)
            .def_property_readonly("fragment", &Url::fragment)

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

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace py = pybind11;

class FlattenGenerator {
public:
    FlattenGenerator(py::list items) :
        mItems(items),
        mIndex(0)
        {};

    bool isComplete() {
        return mIndex >= mItems.size();
    }

    py::object next() {
        py::object p = mItems[mIndex].cast<py::object>();
        mIndex += 1;
        return p;
    };

private:
    py::list mItems;
    uint64_t mIndex;
};

#ifndef VOLUME_H
#define VOLUME_H

#include "numpy.h"
#include "mri.h"
#include "log.h"


void bindVolume(py::module &m);


/**
  MRI subclass to allow interaction between c++ and python.
*/
class PyVolume : public MRI
{
public:
  PyVolume(py::array& array);
  PyVolume(const std::string& filename) : MRI(filename) {};
  ~PyVolume();

  static py::array copyArray(MRI *mri);
  py::array copyArray() { return copyArray(this); };

  void setArray(const py::array& array);
  py::array getArray();
  
  py::array getImage() {
    logWarning << "Volume.image is deprecated - use Volume.array instead to access underlying buffer data";
    return getArray();
  };

  void setImage(const py::array& array) {
    logWarning << "Volume.image is deprecated - use Volume.array instead to access underlying buffer data";
    setArray(array);
  };

  template <class T>
  void setBufferData(py::array_t<T, py::array::f_style | py::array::forcecast> array) {
    if (MRI::Shape(array.request().shape) != shape) py::value_error("array does not match MRI shape " + shapeString(shape));
    T *dst = (T *)chunk;
    const T *src = array.data(0);
    for (unsigned int i = 0; i < vox_total ; i++, dst++, src++) *dst = *src;
  }

private:
  py::array buffer_array;
};

#endif
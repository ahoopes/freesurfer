#ifndef VOLUME_H
#define VOLUME_H

#include "numpy.h"

// utils
#include "mri.h"


void bindVolume(py::module &m);
py::array makeArray(MRI *mri, bool copybuffer = true);

class PyVolume : MRI {
public:
  PyVolume(py::array array);

  // image array getter/setter
  py::array getImage();
  void setImage(py::array_t<float, py::array::f_style | py::array::forcecast> array);

  template<class T>
  void setBufferData(const float *data) {
    T *buff = (T *)m_mri->chunk;
    const float *ptr = data;
    const int count = MRInvox(m_mri);
    for (unsigned int i = 0; i < count ; i++, ptr++, buff++) {
      *buff = (T)*ptr;
    }
  }

private:
  py::array imagebuffer;
};

#endif
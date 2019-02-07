#include "mri.h"

int main(int argc, const char **argv) {
  MRI *mri = MRIread("/usr/local/freesurfer/dev/subjects/bert/mri/orig.mgz");
  return 0;
}

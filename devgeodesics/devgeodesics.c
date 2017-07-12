#include <stdio.h>

#include "mrisurf.h"
#include "geodesicsheat.h"


int main(int argc, char *argv[]) {

  MRIS *surface = MRISread("/usr/local/freesurfer/stable6/subjects/bert/surf/lh.white");
  GEOprecompute(surface);
  MRISfree(&surface);

  return 0;
}

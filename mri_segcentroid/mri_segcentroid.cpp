#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

extern "C"
{
#include "mri.h"
#include "mri2.h"
}


// -------------------- Centroid --------------------

struct Centroid {
  int label;
  std::string labelname;
  float R, A, S;
};


// -------------------- InputParser --------------------

class InputParser {
  public:
    InputParser (int &argc, char **argv){
      for (int i=1; i < argc; ++i) this->tokens.push_back(std::string(argv[i]));
    }
    const std::string& getCmdOption(const std::string &option) const {
      std::vector<std::string>::const_iterator itr;
      itr =  std::find(this->tokens.begin(), this->tokens.end(), option);
      if (itr != this->tokens.end() && ++itr != this->tokens.end()) return *itr;
      static const std::string empty_string("");
      return empty_string;
    }
    bool cmdOptionExists(const std::string &option) const {
      return std::find(this->tokens.begin(), this->tokens.end(), option) != this->tokens.end();
    }
  private:
    std::vector <std::string> tokens;
};


// --------------------  main  --------------------


int main(int argc, char **argv) {


  // -------- setup --------


  InputParser input(argc, argv);

  bool ignore_zero = true;

  // load segmentation
  const std::string &segfile = input.getCmdOption("--seg");
  if (segfile.empty()) {
    std::cout << "ERROR: must specify path to seg file with '--seg'" << std::endl;
    exit(1);
  }
  MRI *seg = MRIread(segfile.c_str());
  if (seg == NULL) {
    std::cout << "ERROR: loading segmentation " << segfile << std::endl;
    exit(1);
  }

  // get voxel to RAS matrix
  MATRIX *vox2ras;
  if (input.cmdOptionExists("--lta")) {
    const std::string &ltafile = input.getCmdOption("--lta");
    if (ltafile.empty()) {
      std::cout << "ERROR: must specify path to lta file" << std::endl;
      exit(1);
    }
    
  } else {
    vox2ras = MRIgetVoxelToRasXform(seg);
  }

  // -------- compute centroids --------


  std::vector<Centroid> centroid_list;

  Centroid centroid;
  int numids, *ids;
  ids = MRIsegIdList(seg, &numids, 0);
  for (int i = 0 ; i < numids; i++) {
    centroid.label = ids[i];
    if ((ignore_zero) && (centroid.label == 0)) continue;

    for (int col = 0 ; col < seg->width ; col++) {
      for (int row = 0 ; row < seg->height ; row++) {
        for (int slice = 0 ; slice < seg->depth ; slice++) {
          if (centroid.label == int(MRIgetVoxVal(mri, c, r, s, 0))) {

          }
        }
      }
    }

    centroid_list.push_back(centroid);
  }


  // -------- write results --------

  for (Centroid c : centroid_list) {
    std::cout << c.label << std::endl;
  }

  MRIfree(&seg);

  return 0;
}

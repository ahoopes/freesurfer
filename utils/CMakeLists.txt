project(utils)

include_directories(
  ${FS_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/include/dicom
  ${CMAKE_SOURCE_DIR}/include/NrrdIO
  ${CMAKE_SOURCE_DIR}/packages/jpeg
  ${CMAKE_SOURCE_DIR}/packages/expat
  ${CMAKE_SOURCE_DIR}/packages/tiff
  ${CMAKE_SOURCE_DIR}/packages/xml2
  ${CMAKE_SOURCE_DIR}/packages/glut
  ${X11_INCLUDE_DIR}
  ${ITK_INCLUDE_DIRS}
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-write-strings -Wno-sign-compare -Wno-unused-value")

set(SOURCES
  afni.cpp
  annotation.cpp
  autoencoder.cpp
  bfileio.cpp
  box.cpp
  Bruker.cpp
  chklc.cpp
  chronometer.cpp
  chronometerpp.cpp
  class_array.cpp
  cluster.cpp
  cma.cpp
  cmat.cpp
  cmat.cpp
  cmdargs.cpp
  colortab.cpp
  connectcomp.cpp
  corio.cpp
  cpputils.cpp
  ctrpoints.cpp
  cvector.cpp
  dct.cpp
  density.cpp
  diag.cpp
  DICOMRead.cpp
  diffuse.cpp
  dmatrix.cpp
  dti.cpp
  dtk.fs.cpp
  error.cpp
  evschutils.cpp
  fcd.cpp
  fftutils.cpp
  field_code.cpp
  filter.cpp
  fio.cpp
  flash.cpp
  fmriutils.cpp
  fs_cost_function.cpp
  fs_lbfgs.cpp
  fs_lbfgs_observer.cpp
  fs_lbfgs_subject.cpp
  fs_powell.cpp
  fsenv.cpp
  fsglm.cpp
  fsinit.cpp
  fsPrintHelp.cpp
  gca.cpp
  gcaboundary.cpp
  gcalinearnode.cpp
  gcalinearprior.cpp
  gcamcomputeLabelsLinearCPU.cpp
  gcamorph.cpp
  gcamorphtestutils.cpp
  gcautils.cpp
  gclass.cpp
  gcsa.cpp
  geos.cpp
  getdelim.cpp
  getline.cpp
  gifti_io.cpp
  gifti_local.cpp
  gifti_xml.cpp
  gtm.cpp
  gw_ic2562.cpp
  gw_utils.cpp
  hippo.cpp
  histo.cpp
  ic12.cpp
  ic163842.cpp
  ic2562.cpp
  ic42.cpp
  ic642.cpp
  icosahedron.cpp
  image.cpp
  imageio.cpp
  imautils.cpp
  intensity_eig.cpp
  ipmpar.cpp
  joint_histo.cpp
  kernel.cpp
  label.cpp
  machine.cpp
  MARS_DT_Boundary.cpp
  matfile.cpp
  matrix.cpp
  mgh_filter.cpp
  min_heap.cpp
  minmaxrc.cpp
  morph.cpp
  mosaic.cpp
  mri.cpp
  mri2.cpp
  mri_conform.cpp
  mri_fastmarching.cpp
  mri_identify.cpp
  mri_level_set.cpp
  mri_tess.cpp
  mri_topology.cpp
  mriBSpline.cpp
  mriclass.cpp
  mricurv.cpp
  mrifilter.cpp
  mriflood.cpp
  mrihisto.cpp
  mriio.cpp
  mriio_nrrd.cpp
  mriio_nrrd_itk.cpp
  MRIio_old.cpp
  mrimorph.cpp
  mrinorm.cpp
  mripolv.cpp
  mriprob.cpp
  mris_compVolFrac.cpp
  mrisegment.cpp
  mriset.cpp
  mrishash.cpp
  mrisp.cpp
  MRISrigidBodyAlignGlobal.cpp
  mrisurf.cpp
  mrisurf_base.cpp
  mrisurf_deform.cpp
  mrisurf_defect.cpp
  mrisurf_integrate.cpp
  mrisurf_io.cpp
  mrisurf_metricProperties.cpp
  mrisurf_mri.cpp
  mrisurf_obsolete.cpp
  mrisurf_timeStep.cpp
  mrisurf_topology.cpp
  mrisurf_vals.cpp
  mrisutils.cpp
  mriTransform.cpp
  mrivoxel.cpp
  nifti1_io.cpp
  numerics.cpp
  offset.cpp
  path.cpp
  pdf.cpp
  pgmstubs.cpp
  prime.cpp
  Progname.cpp
  randomfields.cpp
  rbf.cpp
  rbm.cpp
  realm.cpp
  region.cpp
  registerio.cpp
  resample.cpp
  rescale.cpp
  retinotopy.cpp
  rfa.cpp
  rforest.cpp
  rfutils.cpp
  romp_support.cpp
  selxavgio.cpp
  sig.cpp
  signa.cpp
  stats.cpp
  surfcluster.cpp
  surfgrad.cpp
  svm.cpp
  tags.cpp
  talairachex.cpp
  timer.cpp
  transform.cpp
  tritri.cpp
  tukey.cpp
  utils.cpp
  version.cpp
  vertexRotator.cpp
  vlabels.cpp
  volcluster.cpp
  voxlist.cpp
  xDebug.cpp
  xTypes.cpp
  xVoxel.cpp
  znzlib.cpp
)

# add cephes sources
set(SOURCES ${SOURCES}
  cephes/bdtr.c
  cephes/chdtr.c
  cephes/const.c
  cephes/fdtr.c
  cephes/gamma.c
  cephes/igam.c
  cephes/igami.c
  cephes/incbet.c
  cephes/incbi.c
  cephes/mtherr.c
  cephes/ndtr.c
  cephes/ndtri.c
  cephes/polevl.c
  cephes/stdtr.c
)

# nrrdio-required definitions
add_definitions(-DTEEM_DIO=0 -DTEEM_QNANHIBIT=1 -DTEEM_ENDIAN=${BYTEORDER})

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  add_definitions(-DTEEM_32BIT=0)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  add_definitions(-DTEEM_32BIT=1)
endif()

# add NrrdIO sources
set(SOURCES ${SOURCES}
  NrrdIO/754.c
  NrrdIO/formatPNG.c
  NrrdIO/accessors.c
  NrrdIO/formatPNM.c
  NrrdIO/array.c
  NrrdIO/formatText.c
  NrrdIO/arraysNrrd.c
  NrrdIO/formatVTK.c
  NrrdIO/axis.c
  NrrdIO/gzio.c
  NrrdIO/biffbiff.c
  NrrdIO/keyvalue.c
  NrrdIO/comment.c
  NrrdIO/methodsNrrd.c
  NrrdIO/defaultsNrrd.c
  NrrdIO/miscAir.c
  NrrdIO/dio.c
  NrrdIO/mop.c
  NrrdIO/encodingAscii.c
  NrrdIO/parseAir.c
  NrrdIO/encodingBzip2.c
  NrrdIO/parseNrrd.c
  NrrdIO/encoding.c
  NrrdIO/encodingGzip.c
  NrrdIO/encodingHex.c
  NrrdIO/read.c
  NrrdIO/encodingRaw.c
  NrrdIO/reorder.c
  NrrdIO/endianAir.c
  NrrdIO/endianNrrd.c
  NrrdIO/sane.c
  NrrdIO/enum.c
  NrrdIO/simple.c
  NrrdIO/enumsNrrd.c
  NrrdIO/string.c
  NrrdIO/format.c
  NrrdIO/subset.c
  NrrdIO/formatEPS.c
  NrrdIO/write.c
  NrrdIO/formatNRRD.c
)

add_library(utils STATIC ${SOURCES})

target_link_libraries(utils
  rgb
  hipsstubs
  dicom
  jpeg
  expat
  tiff
  xml2
  minc
  netcdf
  ${ZLIB_LIBRARIES}
  ${ITK_LIBRARIES}
)

if(NOT APPLE)
  target_link_libraries(utils crypt rt)
endif()

# utils binaries

# set(GIFTI_SOURCES gifti_io.c gifti_xml.c nifti1_io.c znzlib.c)

# # gifti_tool
# add_executable(gifti_tool gifti_tool.c ${GIFTI_SOURCES})
# target_link_libraries(gifti_tool expat ${ZLIB_LIBRARIES})
# install(TARGETS gifti_tool DESTINATION bin)

# # gifti test
# add_executable(gifti_test gifti_test.c ${GIFTI_SOURCES})
# target_link_libraries(gifti_test expat ${ZLIB_LIBRARIES})
# install(TARGETS gifti_test DESTINATION bin)

# # xmlToHtml
# add_executable(xmlToHtml xmlToHtml.c)
# target_link_libraries(xmlToHtml xml2 expat ${ZLIB_LIBRARIES})
# install(TARGETS xmlToHtml DESTINATION bin)

# # fsPrintHelp
# add_executable(fsPrintHelp fsPrintHelp.c)
# target_compile_definitions(fsPrintHelp PRIVATE BUILD_MAIN)
# target_link_libraries(fsPrintHelp xml2 ${ZLIB_LIBRARIES})
# install(TARGETS fsPrintHelp DESTINATION bin)

# add_subdirectory(test)

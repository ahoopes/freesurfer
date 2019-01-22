#!/usr/bin/env bash
source "$(dirname $0)/../test.sh"

test_command testcolortab ${FREESURFER_HOME}/FreeSurferColorLUT.txt
test_command test_c_nr_wrapper
test_command extest
test_command inftest
test_command tiff_write_image
test_command sc_test

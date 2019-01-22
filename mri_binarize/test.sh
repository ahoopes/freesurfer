#!/usr/bin/env bash
source "$(dirname $0)/../test.sh"

test_command mri_binarize --i aseg.mgz --o mri_binarize.mgz --match 17
compare_vol mri_binarize.mgz mask.ref.mgz

#!/usr/bin/env bash
source "$(dirname $0)/../test.sh"

test_comand mri_pretess -w mri/wm.mgz wm mri/norm.mgz wm_new.mgz
compare_vol wm_new.mgz wm_ref.mgz

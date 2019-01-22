set -e
set -o pipefail

# simple error function
function error_exit {
    >&2 echo "$(tput setaf 1)error:$(tput sgr 0) $@"
    exit 1
}

# portable absolute path function
function realpath {
    echo $(cd $(dirname $1); pwd)/$(basename $1)
}

# parse command line input
for i in "$@"; do
    case $i in
        -r|--regenerate)
        FSTEST_REGENERATE=true
        shift
        ;;
        *)
        error_exit "unknown argument '$i'"
        ;;
    esac
done

# define paths
FSTEST_CWD="$(pwd)"
FSTEST_TESTDATA_DIR="${FSTEST_CWD}/testdata"
FSTEST_SCRIPT_DIR="$(realpath $(dirname $0))"
FSTEST_TESTDATA_TARBALL="${FSTEST_SCRIPT_DIR}/testdata.tar.gz"

# if regenerating testdata, make a temporary 'testdata_regeneration' directory for new reference data
if [ "$FSTEST_REGENERATE" = true ]; then
    echo "regenerating testdata"
    # make the temporary dir and untar the original testdata into this directory
    FSTEST_REGENERATION_DIR="${FSTEST_CWD}/testdata_regeneration"
    rm -rf $FSTEST_REGENERATION_DIR && mkdir $FSTEST_REGENERATION_DIR
    tar -xzvf "$FSTEST_TESTDATA_TARBALL" -C $FSTEST_REGENERATION_DIR
fi

# todoc
function find_path {
    parent="$(dirname $1)"
    if [ -e "$parent/$2" ]; then
        echo "$parent/$2"
    elif [ "$parent" = "/" ]; then
        error_exit "recursion limit - could not locate '$2' in tree"
    else
        find_path $parent $2
    fi
}

# set freesurfer environment variables
export FREESURFER_HOME="$(find_path $FSTEST_SCRIPT_DIR distribution)"
export SUBJECTS_DIR="$FSTEST_TESTDATA_DIR"
export FSLOUTPUTTYPE="NIFTI_GZ"

# set martinos license
if [ -e "/autofs/space/freesurfer/.license" ] ; then
    export FS_LICENSE="/autofs/space/freesurfer/.license"
fi

# exit hook to cleanup any remaining testdata
function cleanup {
    FSTEST_STATUS=$?
    if [ "$FSTEST_STATUS" = 0 ]; then
        if [ "$FSTEST_REGENERATE" = true ]; then
            # tar the regenerated data
            cd $FSTEST_REGENERATION_DIR
            tar -czvf testdata.tar.gz testdata
            # make sure the annex file is unlocked before replacing it
            cd $FSTEST_SCRIPT_DIR
            git annex unlock testdata.tar.gz
            mv -f ${FSTEST_REGENERATION_DIR}/testdata.tar.gz .
            rm -rf $FSTEST_REGENERATION_DIR
            echo "testdata has been regenerated - make sure to run 'git annex add testdata.tar.gz' to rehash before committing"
            cd $FSTEST_CWD
        else
            echo "$(tput setaf 2)success:$(tput sgr 0) test passed"
        fi
        # remove testdata directory
        rm -rf $FSTEST_TESTDATA_DIR
    else
        echo "$(tput setaf 1)error:$(tput sgr 0) test failed"
    fi
}
trap cleanup EXIT

# todoc
function eval_cmd {
    echo ">> $(tput setaf 3)$@$(tput sgr 0)"
    eval $@
}

# todoc
function test_command {
    # first extract the testdata
    cd $FSTEST_CWD
    rm -rf $FSTEST_TESTDATA_DIR
    tar -xzvf "$FSTEST_TESTDATA_TARBALL"
    cd $FSTEST_TESTDATA_DIR
    # turn off errors if expecting a failure
    if [ -n "$EXPECT_FAILURE" ]; then
        set +e
    fi
    # run the command
    eval_cmd ../$@
    retcode=$?
    # reset settings and check error if failure was expected
    if [ -n "$EXPECT_FAILURE" ]; then
        if [ "$retcode" = 0 ]; then
            error_exit "command returned 0 exit status, but expected a failure"
        fi
        set -e
    fi
}

# runs a standard diff on an output and reference file
function compare_file {
    if [ "$FSTEST_REGENERATE" = true ]; then
        regenerate $1 $2
    else
        eval_cmd diff $1 $2
    fi
}

# runs mri_diff on an output and reference volume
function compare_vol {
    if [ "$FSTEST_REGENERATE" = true ]; then
        regenerate $1 $2
    else
        diffcmd=$(find_path $FSTEST_CWD mri_diff/mri_diff)
        eval_cmd $diffcmd $@ --debug --thresh 0.0 --res-thresh 0.000001 --geo-thresh 0.000008
    fi
}

# runs mris_diff on an output and reference surface
function compare_surf {
    if [ "$FSTEST_REGENERATE" = true ]; then
        regenerate $1 $2
    else
        diffcmd=$(find_path $FSTEST_CWD mris_diff/mris_diff)
        eval_cmd $diffcmd $@ --debug
    fi
}

# moves new test references into the regenerated testdata directory
function regenerate {
    mv -f ${FSTEST_TESTDATA_DIR}/$1 ${FSTEST_REGENERATION_DIR}/testdata/$2
}

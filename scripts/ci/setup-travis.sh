#!/bin/bash

set -x

cd $TRAVIS_BUILD_DIR
export CMAKE_ARGS="-DCMAKE_BUILD_TYPE=debug -DCHFL_BUILD_TESTS=ON $CMAKE_EXTRA"
export BUILD_ARGS="-j2"
export CMAKE_BUILD_TYPE="Debug"

if [[ "$CMAKE_GENERATOR" == "" ]]; then
    export CMAKE_GENERATOR="Unix Makefiles"
fi

if [[ "$STATIC_LIBS" == "ON" ]]; then
    export CMAKE_ARGS="$CMAKE_ARGS -DBUILD_SHARED_LIBS=OFF"
else
    export CMAKE_ARGS="$CMAKE_ARGS -DBUILD_SHARED_LIBS=ON"
fi

if [[ "$DO_COVERAGE" == "ON" ]]; then
    export CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_C_FLAGS=\"--coverage\" -DCMAKE_CXX_FLAGS=\"--coverage\""
    curl https://bootstrap.pypa.io/get-pip.py > get-pip.py
    python3.6 get-pip.py --user
    python3.6 -m pip install --user codecov
fi

if [[ "$EMSCRIPTEN" == "ON" ]]; then
    # Install a Travis compatible emscripten SDK
    wget https://github.com/chemfiles/emscripten-sdk/archive/master.tar.gz
    tar xf master.tar.gz
    ./emscripten-sdk-master/emsdk activate
    source ./emscripten-sdk-master/emsdk_env.sh

    export CMAKE_CONFIGURE='emcmake'
    export CMAKE_ARGS="$CMAKE_ARGS -DCHFL_TEST_RUNNER=node -DCMAKE_BUILD_TYPE=release -DCHFL_BUILD_DOCTESTS=OFF"
    export CMAKE_BUILD_TYPE="Release"

    # Install a modern cmake
    cd $HOME
    wget https://cmake.org/files/v3.9/cmake-3.9.3-Linux-x86_64.tar.gz
    tar xf cmake-3.9.3-Linux-x86_64.tar.gz
    export PATH=$HOME/cmake-3.9.3-Linux-x86_64/bin:$PATH

    export CC=emcc
    export CXX=em++

    return
fi

if [[ "$TRAVIS_OS_NAME" == "linux" ]]; then
    if [[ "$TRAVIS_COMPILER" == "gcc" ]]; then
        export CC=gcc-4.8
        export CXX=g++-4.8
    fi

    if [[ "$VALGRIND" == "ON" ]]; then
        export CMAKE_ARGS="$CMAKE_ARGS -DCHFL_TEST_RUNNER=valgrind"
    fi
fi

if [[ "$TRAVIS_OS_NAME" == "osx" ]]; then
    if [[ "$TRAVIS_COMPILER" == "gcc" ]]; then
        export CC=gcc-5
        export CXX=g++-5

        # Filter out 'warning: section "__textcoal_nt" is deprecated'
        # from the compiler output, as it makes the log reach the size limit
        export BUILD_ARGS="$BUILD_ARGS 2> >(python $TRAVIS_BUILD_DIR/scripts/ci/filter-textcoal-warnings.py)"
    fi
fi

if [[ "$TRAVIS_OS_NAME" == "windows" ]]; then
    export PATH="/c/Program Files/CMake/bin":$PATH
    export CMAKE_ARGS="$CMAKE_ARGS -DCHFL_BUILD_DOCTESTS=OFF"

    if [[ "$CMAKE_GENERATOR" == "Visual Studio"* ]]; then
        choco install -y vcbuildtools
        export PATH=$PATH:"/c/Program Files (x86)/Microsoft Visual Studio/2015/BuildTools/MSBuild/14.0/Bin"
        export BUILD_ARGS="-verbosity:minimal -m:2"
    fi

    if [[ "$CMAKE_GENERATOR" == "MinGW Makefiles" ]]; then
        export CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_SH=CMAKE_SH-NOTFOUND"
        export CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_BUILD_TYPE=release"
        export CMAKE_BUILD_TYPE="Release"

        # Remove sh.exe from git in the PATH for MinGW generator
        # 1) add extra ':' to allow the REMOVE below to work
        TMP_PATH=:$PATH:
        # 2) remove sh.exe from the PATH
        REMOVE='/c/Program Files/Git/usr/bin'
        TMP_PATH=${TMP_PATH/:$REMOVE:/:}
        # 3) remove extra ':'
        TMP_PATH=${TMP_PATH%:}
        TMP_PATH=${TMP_PATH#:}
        export PATH=$TMP_PATH
    fi
fi

if [[ "$ARCH" == "x86" ]]; then
    export CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_C_FLAGS=-m32"
fi

set +x

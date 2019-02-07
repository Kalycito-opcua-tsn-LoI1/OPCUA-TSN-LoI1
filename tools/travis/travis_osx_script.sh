#!/bin/bash
set -e

echo "\n=== Building ==="
export OPENSSL_ROOT_DIR="/usr/local/opt/openssl"
export PATH="/Users/travis/Library/Python/2.7/bin:$PATH"

echo "Documentation and certificate build" && echo -en 'travis_fold:start:script.build.doc\\r'
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DUA_BUILD_EXAMPLES=ON -DUA_BUILD_SELFSIGNED_CERTIFICATE=ON ..
make selfsigned
ls examples
cp examples/server_cert.der ../
cd .. && rm -rf build
echo -en 'travis_fold:end:script.build.doc\\r'

echo "Full Namespace 0 Generation" && echo -en 'travis_fold:start:script.build.ns0\\r'
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DUA_ENABLE_FULL_NS0=ON -DUA_BUILD_EXAMPLES=ON  ..
make -j
cd .. && rm -rf build
echo -en 'travis_fold:end:script.build.ns0\\r'

echo "Compile release build for OS X" && echo -en 'travis_fold:start:script.build.osx\\r'
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DUA_ENABLE_AMALGAMATION=ON -DUA_BUILD_EXAMPLES=ON ..
make -j
tar -pczf open62541-osx.tar.gz ../LICENSE ../AUTHORS ../README.md ./bin/examples/server_ctt ./bin/examples/client ./bin/libopen62541.a open62541.h open62541.c
cp open62541-osx.tar.gz ..
cp open62541.h .. #copy single file-release
cp open62541.c .. #copy single file-release
cd .. && rm -rf build
echo -en 'travis_fold:end:script.build.osx\\r'

echo "Compile multithreaded version" && echo -en 'travis_fold:start:script.build.multithread\\r'
mkdir -p build && cd build
cmake -DUA_ENABLE_MULTITHREADING=ON -DUA_BUILD_EXAMPLES=ON ..
make -j
cd .. && rm -rf build
echo -en 'travis_fold:end:script.build.multithread\\r'

echo "Debug build and unit tests with valgrind" && echo -en 'travis_fold:start:script.build.unit_test\\r'
mkdir -p build && cd build
cmake -DCHECK_PREFIX=/usr/local/Cellar/check/0.11.0 -DCMAKE_BUILD_TYPE=Debug -DUA_BUILD_EXAMPLES=ON -DUA_ENABLE_ENCRYPTION=ON -DUA_ENABLE_DISCOVERY=ON -DUA_ENABLE_DISCOVERY_MULTICAST=ON -DUA_BUILD_UNIT_TESTS=ON -DUA_ENABLE_COVERAGE=ON -DUA_ENABLE_UNIT_TESTS_MEMCHECK=ON ..
make -j && make test ARGS="-V"
cd .. && rm -rf build
echo -en 'travis_fold:end:script.build.unit_test\\r'

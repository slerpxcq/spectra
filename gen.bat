@echo off
:: -------- pffft --------
pushd .
cd "3rdparty/pffft"
cmake -S . -B build -DBUILD_SHARED_LIBS=OFF
popd

"3rdparty/premake5/premake5" vs2022
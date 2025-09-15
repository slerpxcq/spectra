@echo off
:: -------- pffft --------
pushd .
cd "3rdparty/pffft"
cmake -S . -B build -DBUILD_SHARED_LIBS=OFF -DPFFFT_USE_TYPE_DOUBLE=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5 
popd

"3rdparty/premake5/premake5" vs2022
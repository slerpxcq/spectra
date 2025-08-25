#pragma once

#ifdef SP_USE_F64
#define PFFFT_ENABLE_DOUBLE
#endif
#include <pffft.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef min
#undef max
#undef CreateWindow
#endif

#define SP_ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

#ifdef SP_USE_F64
#define SP_FLOAT double
#else 
#define SP_FLOAT float
#endif

#ifdef SP_USE_F64
#define SP_LOG2(x) std::log2(x)
#define SP_LOG(x) std::log(x)
#define SP_ABS(x) std::fabs(x)
#define SP_EXP(x) std::exp(x)
#define SP_POW(x,y) std::pow(x,y)
#define SP_SIN(x) std::sin(x)
#define SP_COS(x) std::cos(x)
#else
#define SP_LOG2(x) std::log2f(x)
#define SP_LOG(x) std::logf(x)
#define SP_ABS(x) std::fabsf(x)
#define SP_EXP(x) std::expf(x)
#define SP_POW(x,y) std::powf(x,y)
#define SP_SIN(x) std::sinf(x)
#define SP_COS(x) std::cosf(x)
#endif

#define SP_TIME_NOW() std::chrono::high_resolution_clock::now()
#define SP_TIMEPOINT decltype(SP_TIME_NOW())
#define SP_TIME_DELTA(x) (std::chrono::duration_cast<std::chrono::microseconds>(SP_TIME_NOW() - (x)).count() * 1e-6)

#ifdef SP_NO_CONSOLE
#ifdef _WIN32
#define SP_APP_ENTRY() \
            int WINAPI WinMain(HINSTANCE hInstance, \
            HINSTANCE hPrevInstance, \
            PSTR lpCmdLine, \
            int nCmdShow) \
{ return std::make_unique<Application>()->Run(); }
#else 
#error "Unsupported platform"
#endif
#else 
#define SP_APP_ENTRY() int main(int argc, char** argv) { return std::make_unique<Application>()->Run(); }
#endif

enum Channel : uint32_t { CHANNEL_LEFT, CHANNEL_RIGHT, CHANNEL_COUNT };

using FFTInstance = pffft::Fft<SP_FLOAT>;

static constexpr uint32_t MAX_FFT_SIZE{ 32768 };

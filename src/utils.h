#pragma once

#include "Config.h"

#include <imgui_internal.h>

#define DEFER_1(x, y) x##y
#define DEFER_2(x, y) DEFER_1(x, y)
#define DEFER_3(x)    DEFER_2(x, __COUNTER__)
#define defer(code)   auto DEFER_3(_defer_) = defer_func([&](){code;})

// https://www.gingerbill.org/article/2015/08/19/defer-in-cpp/
template <typename F>
struct Deferrer 
{
	F f;
	Deferrer(F f) : f{ f } {}
	~Deferrer() { f(); }
};

template <typename F>
Deferrer<F> defer_func(F f) {
	return Deferrer<F>(f);
}

static inline SP_FLOAT FastMag(const std::complex<SP_FLOAT>& c)
{
    SP_FLOAT absRe{ SP_ABS(c.real()) };
    SP_FLOAT absIm{ SP_ABS(c.imag()) };
    SP_FLOAT max{ std::max(absRe, absIm) };
    SP_FLOAT min{ std::min(absRe, absIm) };
	return max + 3 * min / 8;
}





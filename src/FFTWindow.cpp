#include "FFTWindow.h"

void GenBlackmanHarrisWindow(SP_FLOAT* dst, uint32_t size)
{
    static constexpr SP_FLOAT a0{ 0.355768 };
    static constexpr SP_FLOAT a1{ 0.487396 };
    static constexpr SP_FLOAT a2{ 0.144232 };
    static constexpr SP_FLOAT a3{ 0.012604 };
    static constexpr SP_FLOAT PI{ 3.14159265359 };
    SP_FLOAT N{ static_cast<SP_FLOAT>(size) };

    for (uint32_t i{}; i < size; ++i) {
        dst[i] = a0 
            - a1 * SP_COS(2 * PI * i / N) 
            + a2 * SP_COS(4 * PI * i / N)
            - a3 * SP_COS(6 * PI * i / N);
    }
}
#pragma once

#include "Config.h"

#include <cstdint>
#include <memory>
#include <vector>
#include <array>
#include <mutex>
#include <initializer_list>
#include <cassert>
#include <stdexcept>
#include <utility>
#include <atomic>

#include <miniaudio.h>

struct GLFWwindow;

class Application
{
public:
    enum class WindowType {
        BLACKMAN_HARRIS
    };
    using WindowHint = std::pair<int32_t, int32_t>;

public:
    Application();
    ~Application();
    int32_t Run();

    void InitAudioDevice();
    void DeInitAudioDevice();

    void InitImGui();
    void DeInitImGui();
    void ImGuiBeginFrame();
    void ImGuiEndFrame();

    void ResetFFT(uint32_t fftSize, WindowType windowType = WindowType::BLACKMAN_HARRIS);
    void CreateWindow(std::initializer_list<WindowHint> hints, bool vsync = true);

private: 
    static void AudioDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    static void FFTWorker(Application* app);

private:
    uint32_t fftSize{};
    uint32_t fftResultSize{};
    uint32_t sampleBufferSize{}; 

    uint32_t readPtr{};
    uint32_t writePtr{};

    std::unique_ptr<FFTInstance>                     fftInstance{};
    std::vector<SP_FLOAT>                            fftWindow{};
    std::array<std::vector<SP_FLOAT>, CHANNEL_COUNT> magnitudes{};
    std::array<std::vector<SP_FLOAT>, CHANNEL_COUNT> sampleBuffer{};
    std::array<std::vector<SP_FLOAT>, CHANNEL_COUNT> thresholds{};
    std::array<std::vector<SP_FLOAT>, CHANNEL_COUNT> heights{};
    std::vector<SP_FLOAT>                            xs{};
    std::array<std::vector<SP_FLOAT>, CHANNEL_COUNT> ys{};

	SP_FLOAT displayOffset{};
	SP_FLOAT displayScale{ 1.0 };
	SP_FLOAT fallSpeed{ 0.1 };

	std::mutex sampleBufferMutex{};
	std::mutex drawBufferMutex{};
	std::mutex fftBusyMutex{};

	std::mutex sampleAvailMutex{};
	std::condition_variable sampleAvailCond{};

    GLFWwindow* window{};

    ma_device   audioDevice{};

    std::thread fftThread{};
    std::atomic_bool isRunning{ true };
};

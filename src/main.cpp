#include "utils.h"

/************************************** GL includes **************************************/
#include <glad/glad.h>
#include <GLFW/glfw3.h>

/************************************** ImGui includes **************************************/
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <implot.h>

/************************************** PFFFT includes **************************************/
#ifdef SP_USE_DOUBLE_PRECISION
#define PFFFT_ENABLE_DOUBLE
#endif
#include <pffft.hpp>

// spline
//#include <spline.h>

/************************************** miniaudio includes **************************************/
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#undef MINIAUDIO_IMPLEMENTATION
#undef max
#undef min

/************************************** std includes **************************************/
#include <iostream>
#include <cassert>
#include <chrono>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <complex>
#include <vector>
#include <array>

#ifdef SP_USE_DOUBLE_PRECISION
#define SP_FLOAT double
#else 

#define SP_FLOAT float
#endif

#define SP_ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

#ifndef SP_USE_DOUBLE_PRECISION
#define SP_LOG2(x) std::log2f(x)
#define SP_LOG(x) std::logf(x)
#define SP_ABS(x) std::fabsf(x)
#define SP_EXP(x) std::expf(x)
#define SP_POW(x,y) std::powf(x,y)
#define SP_SIN(x) std::sinf(x)
#define SP_COS(x) std::cosf(x)
#else
#define SP_LOG2(x) std::log2(x)
#define SP_LOG(x) std::log(x)
#define SP_ABS(x) std::fabs(x)
#define SP_EXP(x) std::exp(x)
#define SP_POW(x,y) std::pow(x,y)
#define SP_SIN(x) std::sin(x)
#define SP_COS(x) std::cos(x)
#endif

/************************************** delta time utilities **************************************/
#define SP_TIME_NOW() std::chrono::high_resolution_clock::now()
#define SP_TIMEPOINT decltype(SP_TIME_NOW())
#define SP_TIME_DELTA(x) (std::chrono::duration_cast<std::chrono::microseconds>(SP_TIME_NOW() - (x)).count() * 1e-6)

/************************************** ImVec2 operators **************************************/
static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs)
{
    return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y);
}

static inline ImVec2 operator*(const ImVec2& lhs, const ImVec2& rhs)
{
    return ImVec2(lhs.x * rhs.x, lhs.y * rhs.y);
}

static inline ImVec2 operator-(const ImVec2& lhs, const ImVec2& rhs)
{
    return ImVec2(lhs.x - rhs.x, lhs.y - rhs.y);
}

/************************************** configurations **************************************/
static constexpr uint32_t CHANNEL_COUNT{ 2 };
static constexpr uint32_t DEFAULT_FFT_SIZE{ 8192 };
static constexpr uint32_t DEFAULT_FFT_RESULT_SIZE{ DEFAULT_FFT_SIZE / 2 };
enum Channel : uint32_t { LEFT_CH, RIGHT_CH };

/************************************** synchronization objects **************************************/
static std::mutex g_sampleBufferMutex{};
static std::mutex g_samplesReadyMutex{};
static std::condition_variable g_samplesReadyCondition{};
static std::mutex g_drawBufferMutex{};

struct Globals
{
    // FFT 
    uint32_t fftSize{ DEFAULT_FFT_SIZE };
    uint32_t fftResultSize{ DEFAULT_FFT_SIZE / 2 };
    uint32_t sampleBufferSize{ DEFAULT_FFT_SIZE  * 2}; 
    pffft::Fft<SP_FLOAT> fft{ DEFAULT_FFT_SIZE };
    std::vector<SP_FLOAT> fftWindow{ std::vector<SP_FLOAT>(DEFAULT_FFT_SIZE)};
    std::array<std::vector<SP_FLOAT>, CHANNEL_COUNT> magnitudes
		{ std::vector<SP_FLOAT>(fftResultSize), std::vector<SP_FLOAT>(fftResultSize) };

    // Sampling
    std::array<std::vector<SP_FLOAT>, CHANNEL_COUNT> sampleBuffer
		{ std::vector<SP_FLOAT>(sampleBufferSize), std::vector<SP_FLOAT>(sampleBufferSize) }; 
    uint32_t readPtr{}, writePtr{};

    // Drawing
    std::array<std::vector<SP_FLOAT>, CHANNEL_COUNT> thresholds
		{ std::vector<SP_FLOAT>(fftResultSize), std::vector<SP_FLOAT>(fftResultSize) };
    std::array<std::vector<SP_FLOAT>, CHANNEL_COUNT> heights
		{ std::vector<SP_FLOAT>(fftResultSize), std::vector<SP_FLOAT>(fftResultSize) };
    std::vector<SP_FLOAT> xs{ std::vector<SP_FLOAT>(fftResultSize) };

	SP_FLOAT resultOffset{ 0.5 };
	SP_FLOAT resultScale{ 0.03 };
	SP_FLOAT fallSpeed{ 0.1 };
};

static Globals g{};

static void SamplesReadyCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    auto* inputs{ static_cast<const float*>(pInput) };

    {
        std::lock_guard l{ g_sampleBufferMutex };
		for (uint32_t i{}; i < frameCount; ++i, ++g.writePtr) {
			g.writePtr %= g.sampleBufferSize;
			g.sampleBuffer[LEFT_CH][g.writePtr] = inputs[2 * i];
			g.sampleBuffer[RIGHT_CH][g.writePtr] = inputs[2 * i + 1]; }
		g.readPtr = ((int64_t)g.writePtr - g.fftSize) % g.sampleBufferSize;
    }

	g_samplesReadyCondition.notify_all();
}

static void GenBlackmanHarrisWindow(SP_FLOAT* dst, uint32_t size)
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

static SP_FLOAT FastMag(const std::complex<SP_FLOAT>& c)
{
    SP_FLOAT absRe{ SP_ABS(c.real()) };
    SP_FLOAT absIm{ SP_ABS(c.imag()) };
    SP_FLOAT max{ std::max(absRe, absIm) };
    SP_FLOAT min{ std::min(absRe, absIm) };
	return max + 3 * min / 8;
}

static void ConfigFFT(uint32_t size)
{
    g.fftSize = size;
    g.fftResultSize = g.fftSize / 2;
    g.fft = pffft::Fft<SP_FLOAT>{ static_cast<int32_t>(g.fftSize) };
    g.fftWindow.resize(g.fftSize);
    GenBlackmanHarrisWindow(g.fftWindow.data(), g.fftSize);

    for (auto& m : g.magnitudes)
        m.resize(g.fftResultSize);

    g.sampleBufferSize = 2 * g.fftSize;

    for (auto& b : g.sampleBuffer)
        b.resize(g.sampleBufferSize);

    g.readPtr = g.writePtr = 0;

    for (auto& t : g.thresholds)
        t.resize(g.fftResultSize);
    
    for (auto& h : g.heights)
        h.resize(g.fftResultSize);

    g.xs.resize(g.fftResultSize);
    for (uint32_t i{}; i < g.fftResultSize; ++i)
        g.xs[i] = static_cast<SP_FLOAT>(i);
}

static void FFTWorker()
{
	auto fftIn{ g.fft.valueVector() };
	auto fftOut{ g.fft.spectrumVector() };

    while (true) {
        std::unique_lock lock{ g_samplesReadyMutex };
        g_samplesReadyCondition.wait(lock);

        for (uint32_t channel{}; channel < CHANNEL_COUNT; ++channel) {
            {
                std::lock_guard lock{ g_sampleBufferMutex };
                for (uint32_t i{}, rdptr{ g.readPtr }; i < g.fftSize; ++i, ++rdptr) {
					rdptr %= g.sampleBufferSize;
					fftIn[i] = g.sampleBuffer[channel][rdptr] * g.fftWindow[i];
				}
            }

			g.fft.forward(fftIn, fftOut);

			{
                std::lock_guard lock{ g_drawBufferMutex };
				for (uint32_t i{}; i < g.fftResultSize; ++i) {
#ifdef SP_USE_FAST_MAGNITUDE
                    SP_FLOAT mag{ FastMag(fftOut[i]) };
#else
                    SP_FLOAT mag{ std::abs(fftOut[i]) };
#endif
                    g.heights[channel][i] = std::max(mag, g.thresholds[channel][i]);
					g.thresholds[channel][i] = std::max(mag, g.thresholds[channel][i]);
				}
            }
        }
    }
}

int main(int argc, char** argv) 
{
    GenBlackmanHarrisWindow(g.fftWindow.data(), g.fftWindow.size());
    for (uint32_t i{}; i < g.fftResultSize; ++i)
        g.xs[i] = static_cast<SP_FLOAT>(i);

	/************************************** miniaudio init **************************************/
    ma_device_config deviceConfig{};
    ma_device device{};

    deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format   = ma_format_f32;
    deviceConfig.capture.channels = CHANNEL_COUNT;
    deviceConfig.sampleRate       = 44100;
    deviceConfig.dataCallback     = SamplesReadyCallback;

    ma_result result{ ma_device_init(nullptr, &deviceConfig, &device) };
    assert(result == MA_SUCCESS);
    defer(ma_device_uninit(&device));

	/************************************** GL init **************************************/
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint( GLFW_DECORATED, GLFW_FALSE );

    if (!glfwInit())
        return -1;

    GLFWwindow* window{ glfwCreateWindow(1280, 360, "Spectra", nullptr, nullptr) };
    if (!window) {
        glfwTerminate();
        return -1;
    }
    defer(glfwTerminate());

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    int version = gladLoadGL();
    assert(version);

	/************************************** ImGui init **************************************/
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); 
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460");

	/************************************** END init **************************************/
    // Start audio device
    result = ma_device_start(&device);
    assert(result == MA_SUCCESS);
    defer(ma_device_stop(&device));

    //ConfigFFT(DEFAULT_FFT_SIZE);

    // Start FFT thread
    std::thread fftThread{ FFTWorker };
    fftThread.detach();

    static SP_TIMEPOINT lastTime{ SP_TIME_NOW() };
    static constexpr float TIME_STEP{ 1.f / 60 };

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Draw", nullptr, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoTitleBar);

        SP_FLOAT deltaTime = SP_TIME_DELTA(lastTime);
		lastTime = SP_TIME_NOW();

        {
			std::lock_guard lock(g_drawBufferMutex);
			static SP_FLOAT acc;
			acc += deltaTime;
			while (acc >= TIME_STEP) {
				for (uint32_t channel{}; channel < CHANNEL_COUNT; ++channel) {
					for (uint32_t i{}; i < g.fftResultSize; ++i)
						g.thresholds[channel][i] /= std::max(SP_EXP(g.thresholds[channel][i]), SP_FLOAT(1.01));
				}
				acc -= TIME_STEP;
			}

            // spline
            //std::vector<double> xv(xs, xs + SP_ARRAY_SIZE(xs));
            //std::vector<std::vector<double>> yvs;
            //for (uint32_t i = 0; i < CHANNEL_COUNT; ++i) 
            //    yvs.push_back(std::vector<double>(heights[i], heights[i] + SP_ARRAY_SIZE(heights[i])));
            //
            //std::vector<tk::spline> splines;
            //for (uint32_t i = 0; i < CHANNEL_COUNT; ++i)
            //    splines.emplace_back(xv, yvs[i]);

            const auto min{ ImGui::GetWindowContentRegionMin() };
            const auto max{ ImGui::GetWindowContentRegionMax() };
		 	const auto size = max - min;
                
		 	ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0,0));
             if (ImPlot::BeginPlot("FFT", size, ImPlotFlags_CanvasOnly)) {
                 // ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
                 // ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
                 ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
                 ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
                 ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
                 ImPlot::SetupAxesLimits(1, g.fftResultSize, 0.001, 100, ImPlotCond_Always);
		 		ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.5f);
                 // ImPlot::SetupAxesLimits(1, FFT_RESULT_SIZE, -1, 20, ImPlotCond_Always);
                 ImPlot::PlotShaded("L", g.xs.data(), g.heights[LEFT_CH].data(), g.fftResultSize);
                 ImPlot::PlotShaded("R", g.xs.data(), g.heights[RIGHT_CH].data(), g.fftResultSize);
                 ImPlot::PlotLine("L", g.xs.data(), g.heights[LEFT_CH].data(), g.fftResultSize);
                 ImPlot::PlotLine("R", g.xs.data(), g.heights[RIGHT_CH].data(), g.fftResultSize);
                 ImPlot::EndPlot();
             }
             ImPlot::PopStyleVar();
        }

		static constexpr const char* fftSizes[] = 
			{ "128", "256", "512", "1024", "2048", "4096", "8192", "16384"};

        static uint32_t showConfig{};
        if (ImGui::IsKeyPressed(ImGuiKey_C)) 
            showConfig ^= 1;

        if (showConfig) {
            ImGui::Begin("Config");
            ImGui::SeparatorText("FFT settings");
            if (ImGui::BeginCombo("FFT size", std::to_string(g.fftSize).c_str())) {
                for (uint32_t i{}; i < IM_ARRAYSIZE(fftSizes); ++i) {
                    if (ImGui::Selectable(fftSizes[i])) 
                        ConfigFFT(1u << (i + 7)); // 0->128, 2->256, etc.
                }
                ImGui::EndCombo();
            }
            ImGui::SeparatorText("Display settings");
            ImGui::End();
        }

        ImGui::End();
        ImGui::Render();
        int32_t displayWidth{}, displayHeight{};
        glfwGetFramebufferSize(window, &displayWidth, &displayHeight);
        glViewport(0, 0, displayWidth, displayHeight);
        glClearColor(0.1, 0.1, 0.1, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    return 0;
}

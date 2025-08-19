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
#ifdef PFFFT_ENABLE_DOUBLE
#undef PFFFT_ENABLE_DOUBLE
#endif

// spline
//#include <spline.h>

/************************************** miniaudio includes **************************************/
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#undef MINIAUDIO_IMPLEMENTATION
#undef max
#undef min

/************************************** stdlib includes **************************************/
#include <iostream>
#include <cassert>
#include <chrono>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <complex>

#ifdef SP_USE_DOUBLE_PRECISION
#define SP_FLOAT  double
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
static constexpr uint32_t CHANNEL_COUNT = 2;
static constexpr uint32_t SAMPLE_COUNT = 8192;
static constexpr uint32_t FFT_RESULT_SIZE = SAMPLE_COUNT / 2;
static constexpr uint32_t LEFT_CH = 0;
static constexpr uint32_t RIGHT_CH = 1;

/************************************** ring buffer **************************************/
static constexpr uint32_t BUFFER_CAPACITY = 2 * SAMPLE_COUNT;
static SP_FLOAT sampleBuffer[CHANNEL_COUNT][BUFFER_CAPACITY];
static std::mutex sampleBufferMutex;
static uint32_t writePtr;
static uint32_t readPtr;

/************************************** sample ready signal **************************************/
static std::mutex samplesReadyMutex;
static std::condition_variable samplesReadyCondition;

/************************************** drawing data **************************************/
static std::mutex drawBufferMutex;
static SP_FLOAT thresholds[CHANNEL_COUNT][FFT_RESULT_SIZE];
static SP_FLOAT heights[CHANNEL_COUNT][FFT_RESULT_SIZE];

static SP_FLOAT resultOffset = 0.5;
static SP_FLOAT resultScale = 0.03;
static SP_FLOAT fallSpeed = 0.1;

static void SamplesReadyCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    (void)pOutput;

    SP_FLOAT* inputs = (SP_FLOAT*)pInput;
    {
        std::lock_guard l(sampleBufferMutex);
		for (uint32_t i = 0; i < frameCount; ++i, ++writePtr) {
			writePtr %= BUFFER_CAPACITY;
			sampleBuffer[LEFT_CH][writePtr] = inputs[2 * i];
			sampleBuffer[RIGHT_CH][writePtr] = inputs[2 * i + 1];
		}
		readPtr = ((int64_t)writePtr - SAMPLE_COUNT) % BUFFER_CAPACITY;
    }
	samplesReadyCondition.notify_all();
}

static void GenBlackmanHarrisWindow(SP_FLOAT* dst)
{
    static constexpr SP_FLOAT a0 = 0.355768f;
    static constexpr SP_FLOAT a1 = 0.487396f;
    static constexpr SP_FLOAT a2 = 0.144232f;
    static constexpr SP_FLOAT a3 = 0.012604f;
    static constexpr SP_FLOAT PI = 3.14159265359;
    static SP_FLOAT N = SAMPLE_COUNT;

    for (uint32_t i = 0; i < N; ++i) {
        dst[i] = a0 
            - a1 * SP_COS(2 * PI * i / N) 
            + a2 * SP_COS(4 * PI * i / N)
            - a3 * SP_COS(6 * PI * i / N);
    }
}

static SP_FLOAT FastMag(const std::complex<SP_FLOAT>& c)
{
	SP_FLOAT absRe = SP_ABS(c.real());
    SP_FLOAT absIm = SP_ABS(c.imag());
	SP_FLOAT max = std::max(absRe, absIm);
	SP_FLOAT min = std::min(absRe, absIm);
	return max + 3 * min / 8;
}

static void FFTWorker()
{
	static pffft::Fft<SP_FLOAT> fftInstance(SAMPLE_COUNT);
    static SP_FLOAT window[SAMPLE_COUNT];
    auto fftIn = fftInstance.valueVector();
    auto fftOut = fftInstance.spectrumVector();

    static SP_FLOAT mag[CHANNEL_COUNT][FFT_RESULT_SIZE];

    GenBlackmanHarrisWindow(window);

    while (true) {
        // Wait for samples
        std::unique_lock lock(samplesReadyMutex);
        samplesReadyCondition.wait(lock);

        for (uint32_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
            {
                std::lock_guard lock(sampleBufferMutex);
				for (uint32_t i = 0, rdptr = readPtr; i < SAMPLE_COUNT; ++i, ++rdptr) {
					rdptr %= BUFFER_CAPACITY;
					fftIn[i] = sampleBuffer[channel][rdptr] * window[i];
				}
            }

			fftInstance.forward(fftIn, fftOut);

			{
				std::lock_guard lock(drawBufferMutex);
				for (uint32_t i = 0; i < FFT_RESULT_SIZE; ++i) {
                    SP_FLOAT mag = FastMag(fftOut[i]);
                    heights[channel][i] = std::max(mag, thresholds[channel][i]);
					thresholds[channel][i] = std::max(mag, thresholds[channel][i]);
				}
            }
        }
    }
}

int main(int argc, char** argv) 
{
	/************************************** statics **************************************/
	static SP_FLOAT xs[FFT_RESULT_SIZE];
    for (uint32_t i = 0; i < FFT_RESULT_SIZE; ++i)
        xs[i] = static_cast<SP_FLOAT>(i);

	/************************************** miniaudio init **************************************/
    ma_result result;
    ma_device_config deviceConfig;
    ma_device device;

    deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format   = ma_format_f32;
    deviceConfig.capture.channels = CHANNEL_COUNT;
    deviceConfig.sampleRate       = 44100;
    deviceConfig.dataCallback     = SamplesReadyCallback;

    result = ma_device_init(NULL, &deviceConfig, &device);
    assert(result == MA_SUCCESS);

	/************************************** GL init **************************************/
    GLFWwindow* window = nullptr;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint( GLFW_DECORATED, GLFW_FALSE );

    if (!glfwInit())
        return -1;

    window = glfwCreateWindow(640, 480, "Hello World", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }

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
    ImGui_ImplOpenGL3_Init("#version 430");

	/************************************** END init **************************************/
    // Start audio device
    result = ma_device_start(&device);
    assert(result == MA_SUCCESS);

    // Start FFT thread
    std::thread fftThread = std::thread(FFTWorker);
    fftThread.detach();

    static SP_TIMEPOINT lastTime = SP_TIME_NOW();
    static constexpr float TIME_STEP = 1.f / 60;

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
			std::lock_guard lock(drawBufferMutex);
			static SP_FLOAT acc;
			acc += deltaTime;
			while (acc >= TIME_STEP) {
				for (uint32_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
					for (uint32_t i = 0; i < FFT_RESULT_SIZE; ++i) 
						thresholds[channel][i] /= std::max(SP_EXP(thresholds[channel][i]), SP_FLOAT(1.01));
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

			ImVec2 min = ImGui::GetWindowContentRegionMin();
			ImVec2 max = ImGui::GetWindowContentRegionMax();
			ImVec2 size = max - min;
               
			ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0,0));
            if (ImPlot::BeginPlot("FFT", size, ImPlotFlags_CanvasOnly)) {
                // ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
                // ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
                ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
                ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
                ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
                ImPlot::SetupAxesLimits(1, FFT_RESULT_SIZE, 0.001, 100, ImPlotCond_Always);
				ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.5f);
                // ImPlot::SetupAxesLimits(1, FFT_RESULT_SIZE, -1, 20, ImPlotCond_Always);
                ImPlot::PlotShaded("L", xs, heights[LEFT_CH], FFT_RESULT_SIZE);
                ImPlot::PlotShaded("R", xs, heights[RIGHT_CH], FFT_RESULT_SIZE);
                ImPlot::PlotLine("L", xs, heights[LEFT_CH], FFT_RESULT_SIZE);
                ImPlot::PlotLine("R", xs, heights[RIGHT_CH], FFT_RESULT_SIZE);
                ImPlot::EndPlot();
            }
            ImPlot::PopStyleVar();
        }

        ImGui::End();
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1, 0.1, 0.1, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    glfwTerminate();

    ma_device_uninit(&device);

    return 0;
}

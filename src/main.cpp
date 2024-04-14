#include <iostream>
#include <cassert>
#include <chrono>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <complex>

// GL
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// ImGui
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

// pffft
#include <pffft.hpp>

// miniaudio
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs)
{
    return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y);
}

static constexpr uint32_t CHANNEL_COUNT = 2;
static constexpr uint32_t SAMPLE_COUNT = 1024;
static constexpr uint32_t LEFT_CH = 0;
static constexpr uint32_t RIGHT_CH = 1;

// Real FFT; CFFT size is half of sample count
static constexpr uint32_t FFT_SIZE = SAMPLE_COUNT / 2;
static uint64_t bitRevTable[FFT_SIZE];
static std::complex<float> rootTable[FFT_SIZE / 2];

// Double buffering 
// if the first half of the buffer is for writing while 
// the second half is for reading, vice versa
// [channel: L=0;R=1][samples]
static constexpr uint32_t BUFFER_CAPACITY = 2 * SAMPLE_COUNT;
static float buffer[2][BUFFER_CAPACITY];
static uint32_t writePtr;
static uint32_t readPtr;

static std::mutex samplesReadyMutex;
static std::condition_variable samplesReadyCondition;

// [channel][values]
static float drawBuffer[2][SAMPLE_COUNT];
static std::mutex drawBufferMutex;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    (void)pOutput;

    float* inputs = (float*)pInput;
    for (uint32_t i = 0; i < frameCount; ++i) {
        buffer[LEFT_CH][writePtr] = inputs[2 * i];
        buffer[RIGHT_CH][writePtr] = inputs[2 * i + 1];
        if (++writePtr >= BUFFER_CAPACITY) {
            writePtr = 0;
            readPtr = SAMPLE_COUNT;
            // Wrap; the second half buffer is ready for reading
			samplesReadyCondition.notify_all();
        }
    }

    static uint32_t lastPtr;
    if (lastPtr < SAMPLE_COUNT && writePtr >= SAMPLE_COUNT) {// The first half buffer is ready for reading
        readPtr = 0;
        samplesReadyCondition.notify_all();
    }
    lastPtr = writePtr;
}

void data_consumer()
{
    while (true) {
        // Wait on the signal
        std::unique_lock lock(samplesReadyMutex);
        samplesReadyCondition.wait(lock);

        {
			std::lock_guard lock2(drawBufferMutex);
        }
    }
}

int main(int argc, char** argv) 
{
    /******************************* FFT init *******************************/ 
    /******************************* miniaudio *******************************/ 
    ma_result result;
    ma_device_config deviceConfig;
    ma_device device;

    deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format   = ma_format_f32;
    deviceConfig.capture.channels = CHANNEL_COUNT;
    deviceConfig.sampleRate       = 48000;
    deviceConfig.dataCallback     = data_callback;

    result = ma_device_init(NULL, &deviceConfig, &device);
    assert(result == MA_SUCCESS);

    /******************************* glfw *******************************/ 

    GLFWwindow* window = nullptr;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

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

    
    /******************************* imgui *******************************/ 
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); 
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    result = ma_device_start(&device);
    assert(result == MA_SUCCESS);

    // Create a thread for consumer
    auto consumerThread = std::thread(data_consumer);
    consumerThread.detach();

    while (!glfwWindowShouldClose(window)) {
        // Rendering
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Draw");

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 min = ImGui::GetWindowContentRegionMin();
        ImVec2 max = ImGui::GetWindowContentRegionMax();
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 origin = pos + min;

        {
            std::lock_guard lock(drawBufferMutex);
            for (uint32_t i = 0; i < SAMPLE_COUNT; ++i) {
                drawList->AddCircleFilled(origin + ImVec2(i, drawBuffer[LEFT_CH][i]), 3.0f, IM_COL32(255, 0, 0, 64));
                drawList->AddCircleFilled(origin + ImVec2(i, drawBuffer[RIGHT_CH][i]), 3.0f, IM_COL32(0, 0, 255, 64));
            }
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

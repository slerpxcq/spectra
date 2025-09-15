#include "Application.h"
#include "Utils.h"

#include "FFTWindow.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>
#include <implot.h>

#include <functional>
#include <chrono>

#include <iostream>

void Application::AudioDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
	const auto samples{ static_cast<const float*>(pInput) };
	auto app{ static_cast<Application*>(pDevice->pUserData) };

    {
        std::lock_guard l{ app->sampleBufferMutex };
		for (uint32_t i{}; i < frameCount; ++i, ++app->writePtr) {
			app->writePtr %= app->sampleBufferSize;
			app->sampleBuffer[CHANNEL_LEFT][app->writePtr] = samples[2 * i];
			app->sampleBuffer[CHANNEL_RIGHT][app->writePtr] = samples[2 * i + 1]; 
        }

		auto readPtr{ static_cast<int64_t>(app->writePtr) - frameCount };
		if (readPtr < 0) 
			readPtr += app->sampleBufferSize;
		app->readPtr = readPtr;
    }

	app->sampleAvailCond.notify_all();
}

void Application::FFTWorker(Application* app)
{
	auto fftIn{ app->fftInstance->valueVector() };
	auto fftOut{ app->fftInstance->spectrumVector() };

    while (app->isRunning) {
        std::unique_lock lock{ app->sampleAvailMutex };
        app->sampleAvailCond.wait(lock);

        for (uint32_t channel{}; channel < CHANNEL_COUNT; ++channel) {
            {
                std::lock_guard l{ app->sampleBufferMutex };
                for (uint32_t i{}, rdptr{ app->readPtr }; i < app->fftSize; ++i, ++rdptr) {
					rdptr %= app->sampleBufferSize;
					fftIn[i] = app->sampleBuffer[channel][rdptr] * app->fftWindow[i];
				}
            }

			{
				std::lock_guard l{ app->fftBusyMutex };
				app->fftInstance->forward(fftIn, fftOut);
			}

			{
                std::lock_guard l{ app->drawBufferMutex };
				for (uint32_t i{}; i < app->fftResultSize; ++i) {
                    SP_FLOAT mag{ std::abs(fftOut[i]) };
                    app->heights[channel][i] = std::max(mag, app->thresholds[channel][i]);
					app->thresholds[channel][i] = std::max(mag, app->thresholds[channel][i]);
				}
            }
        }
    }
}

Application::Application()
{
	if (!glfwInit())
		throw std::runtime_error("Could not initialize GLFW\n");

	CreateWindow({{ GLFW_CONTEXT_VERSION_MAJOR, 4 },
				 {  GLFW_CONTEXT_VERSION_MINOR, 6 },
				 {  GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE },
				 {  GLFW_DECORATED, true }});
  
	InitImGui();
	ResetFFT(MAX_FFT_SIZE);
	InitAudioDevice();
	fftThread = std::thread{ FFTWorker, this };
}

Application::~Application()
{
	isRunning = false;
	fftThread.join();
	DeInitAudioDevice();
	glfwTerminate();
}

int32_t Application::Run()
{
    static SP_TIMEPOINT lastTime{ SP_TIME_NOW() };
    static constexpr float TIME_STEP{ 1.f / 60 };

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
		ImGuiBeginFrame();

		auto io{ ImGui::GetIO() };
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(io.DisplaySize);
		ImGui::Begin("Canvas", nullptr, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoTitleBar);

        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        const SP_FLOAT deltaTime{ SP_TIME_DELTA(lastTime) };
        lastTime = SP_TIME_NOW();

        const auto min{ ImGui::GetWindowContentRegionMin() };
        const auto max{ ImGui::GetWindowContentRegionMax() };
        const auto size = max - min;

        static auto colorL{ ImPlot::GetColormapColor(0) };
        static auto colorR{ ImPlot::GetColormapColor(1) };
        static float lineWidth{ 1.f };
        static float shadeTransparency{ .5f };
        static bool drawOrder{};
        static int scaleType{};
        static bool keepTitleBar{};
        static bool syncChannelAlpha{};

        {
			std::lock_guard lock{ drawBufferMutex };

            // Fixed timestep update
            static SP_FLOAT accumulator{};
            accumulator += deltaTime;
            while (accumulator >= TIME_STEP) {
                for (uint32_t channel{}; channel < CHANNEL_COUNT; ++channel) {
                    for (uint32_t i{}; i < fftResultSize; ++i)
                        thresholds[channel][i] /= std::max(SP_EXP(thresholds[channel][i]), SP_FLOAT(1.01));
                }
                accumulator -= TIME_STEP;
            }

            // spline
#if SP_DO_SPLINE_INTERPOLATION
            std::vector<double> xv(xs, xs + SP_ARRAY_SIZE(xs));
            std::vector<std::vector<double>> yvs;
            for (uint32_t i = 0; i < CHANNEL_COUNT; ++i)
                yvs.push_back(std::vector<double>(heights[i], heights[i] + SP_ARRAY_SIZE(heights[i])));

            std::vector<tk::spline> splines;
            for (uint32_t i = 0; i < CHANNEL_COUNT; ++i)
                splines.emplace_back(xv, yvs[i]);

            for (auto& h : g.heights) {
                for (uint32_t i{}; i < h.size(); ++i)
                    h[i] = h[i] * g.resultScale + g.resultOffset;
            }
#endif

            // Plot    
            ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0, 0));
            if (ImPlot::BeginPlot("FFT", size, ImPlotFlags_CanvasOnly)) {
                ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
                ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
                ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
                ImPlot::SetupAxesLimits(1, xs.size(), 0.001, 100, ImPlotCond_Always);
                ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, shadeTransparency);

                auto plot = [&](const char* label, 
                                const std::vector<SP_FLOAT>& xs, 
                                const std::vector<SP_FLOAT>& ys, 
                                ImVec4 color) {
                    ImPlot::PushStyleColor(ImPlotCol_Line, color);
                    ImPlot::PushStyleColor(ImPlotCol_Fill, color);
                    ImPlot::PlotShaded(label, xs.data(), ys.data(), xs.size());
                    ImPlot::SetNextLineStyle(color, lineWidth);
                    ImPlot::PlotLine(label, xs.data(), ys.data(), xs.size());
                    ImPlot::PopStyleColor(2);
                };

                if (drawOrder) {
                    plot("L", xs, heights[CHANNEL_LEFT], colorL);
                    plot("R", xs, heights[CHANNEL_RIGHT], colorR);
                }
                else {
                    plot("R", xs, heights[CHANNEL_RIGHT], colorR);
                    plot("L", xs, heights[CHANNEL_LEFT], colorL);
                }
                ImPlot::EndPlot();
            }
            ImPlot::PopStyleVar();
        }

		static uint32_t showConfig{};
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			showConfig ^= 1u;

		if (showConfig) {
			ImGui::Begin("Config");
			ImGui::SeparatorText("FFT settings");
			static constexpr const char* fftSizes[] =
				{ "128", "256", "512", "1024", "2048", "4096", "8192", "16384", "32768" };
			if (ImGui::BeginCombo("FFT size", std::to_string(fftSize).c_str())) {
				for (uint32_t i{}; i < IM_ARRAYSIZE(fftSizes); ++i) {
					if (ImGui::Selectable(fftSizes[i]))
						ResetFFT(1u << (i + 7));
				}
				ImGui::EndCombo();
			}

			ImGui::SeparatorText("Apperances");
			ImGui::SeparatorText("Window");
            ImGui::Checkbox("Keep title bar", &keepTitleBar);

			ImGui::SeparatorText("Plotting");
            ImGui::Checkbox("Synchronize channel alpha", &syncChannelAlpha);
			ImGui::Separator();
			ImGui::Checkbox("Swap channel draw order", &drawOrder);

			ImGui::SliderFloat("Edge size", &lineWidth, 1.f, 10.f);
			ImGui::SliderFloat("Shade transparency", &shadeTransparency, 0.f, 1.f);
			static constexpr const char* scaleTypes[] =
				{ "Linear", "Semi-logarithmic", "Logarithmic" };
			if (ImGui::BeginCombo("Scale type", std::to_string(scaleType).c_str())) {
				for (uint32_t i{}; i < IM_ARRAYSIZE(scaleTypes); ++i) {
					if (ImGui::Selectable(scaleTypes[i]))
						; // TODO: scale type selection
				}
				ImGui::EndCombo();
			}

			ImGui::Separator();
			static float offset{ static_cast<float>(displayOffset) };
			ImGui::SliderFloat("Offset", &offset, .1f, 1.f);
			this->displayOffset = offset;
			static float scale{ static_cast<float>(displayScale) };
			ImGui::SliderFloat("Scale", &scale, .01f, .1f);
			this->displayScale = scale;
			static float fallSpeed{ static_cast<float>(fallSpeed) };
			ImGui::SliderFloat("Fall speed", &fallSpeed, .1f, 1.f);
			this->fallSpeed = fallSpeed;

            ImGui::SeparatorText("Channel draw color");
			ImGui::PushItemWidth(200);
			ImGui::ColorPicker3("Color L", &colorL.x);
			ImGui::SameLine();
			ImGui::ColorPicker3("Color R", &colorR.x);
			ImGui::PopItemWidth();
			ImGui::End();
		}
		ImGui::End();

		ImGuiEndFrame();
		glfwSwapBuffers(window); 
	}

	return 0;
}

void Application::InitAudioDevice()
{
    ma_device_config deviceConfig{};
    deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format   = ma_format_f32;
    deviceConfig.capture.channels = CHANNEL_COUNT;
    deviceConfig.sampleRate       = 44100;
	deviceConfig.dataCallback     = AudioDataCallback;
	deviceConfig.pUserData        = this;

	if (ma_device_init(nullptr, &deviceConfig, &audioDevice) != MA_SUCCESS)
		throw std::runtime_error{ "Could not initialize audio device\n" };

	std::cout << audioDevice.capture.name << '\n';

	if (ma_device_start(&audioDevice) != MA_SUCCESS) 
		throw std::runtime_error{ "Could not start audio device\n" };
}

void Application::DeInitAudioDevice()
{
	ma_device_stop(&audioDevice);
	ma_device_uninit(&audioDevice);
}

void Application::InitImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
	auto& io{ ImGui::GetIO() };
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460");
}

void Application::DeInitImGui()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}

void Application::ImGuiBeginFrame()
{
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}

void Application::ImGuiEndFrame()
{
	int32_t x{}, y{};
	glfwGetFramebufferSize(window, &x, &y);
	auto& io{ ImGui::GetIO() };
	io.DisplaySize.x = static_cast<float>(x);
	io.DisplaySize.y = static_cast<float>(y);
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Application::ResetFFT(uint32_t fftSize, WindowType windowType)
{
    assert(fftSize >= 128 && fftSize <= 32768);
    std::scoped_lock lock{ sampleBufferMutex, drawBufferMutex, fftBusyMutex };

	this->fftSize = fftSize;

	fftResultSize = fftSize / 2;
	sampleBufferSize = fftSize * 2;

	fftInstance = std::make_unique<FFTInstance>(static_cast<int>(fftSize));
	fftWindow = std::vector<SP_FLOAT>(fftSize);
	magnitudes = { std::vector<SP_FLOAT>(fftSize), std::vector<SP_FLOAT>(fftSize) };
	sampleBuffer = { std::vector<SP_FLOAT>(sampleBufferSize), std::vector<SP_FLOAT>(sampleBufferSize) };
	thresholds = { std::vector<SP_FLOAT>(fftResultSize), std::vector<SP_FLOAT>(fftResultSize) };
	heights = { std::vector<SP_FLOAT>(fftResultSize), std::vector<SP_FLOAT>(fftResultSize) };
	xs = { std::vector<SP_FLOAT>(fftResultSize) };
	ys = { std::vector<SP_FLOAT>(fftResultSize), std::vector<SP_FLOAT>(fftResultSize) };

	for (uint32_t i{}; i < xs.size(); ++i)
		xs[i] = static_cast<SP_FLOAT>(i);

	switch (windowType) {
	case WindowType::BLACKMAN_HARRIS:
		::GenBlackmanHarrisWindow(fftWindow.data(), fftSize);
		break;
	default:
		assert(0 && "Unimplemented");
		break;
	}
}

void Application::CreateWindow(std::initializer_list<WindowHint> hints, bool vsync)
{
	for (const auto& [hint, value] : hints)
		glfwWindowHint(hint, value);

	if (window = glfwCreateWindow(1920, 200, "Spectra", nullptr, nullptr); !window)
		throw std::runtime_error("Could not create window");

    glfwMakeContextCurrent(window);

	if (vsync)
		glfwSwapInterval(1);
	else
		glfwSwapInterval(0);

	auto glVersion{ gladLoadGL() };
    assert(glVersion);
}

SP_APP_ENTRY()



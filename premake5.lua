workspace "spectra"
    architecture "x64"
    startproject "spectra"
    configurations { 
        "Debug", 
        "Release" 
    }

    IncDir = {}
    IncDir["glfw"] = "3rdparty/glfw/include"
    IncDir["glad"] = "3rdparty/glad/include"
    IncDir["imgui"] = "3rdparty/imgui"
    IncDir["implot"] = "3rdparty/implot"
    IncDir["pffft"] = "3rdparty/pffft"
    IncDir["spline"] = "3rdparty/spline/src"

    group "3rdparty"
    include "3rdparty/glfw"
    include "3rdparty/imgui"
    include "3rdparty/glad"
    include "3rdparty/implot"
	externalproject "pffft"
		location "3rdparty/pffft/build/"
		kind "StaticLib"
		language "C++"
		configmap {
            ["Debug"] = "Debug",
            ["Release"] = "Release"
        }
    group ""

    project "spectra"
        kind "ConsoleApp"
        language "C++"
        cppdialect "C++17"
        targetdir "bin/%{prj.name}/%{cfg.buildcfg}"
        objdir "obj/%{prj.name}/%{cfg.buildcfg}"

        links {
            "glad",
            "glfw",
            "imgui",
            "pffft",
            "implot",
            "opengl32.lib"
        }

        includedirs {
            "%{IncDir.glfw}",
            "%{IncDir.glad}",
            "%{IncDir.imgui}",
            "%{IncDir.pffft}",
            "%{IncDir.implot}",
            "%{IncDir.spline}",
            "src",
            "3rdparty/miniaudio"
        }

        files { 
            "src/**.cpp",
            "src/**.hpp"
        }
		
		defines {
			--"SP_USE_DOUBLE_PRECISION"
		}

        filter "system:windows"
            systemversion "latest"

        filter "configurations:Debug"
            symbols "on"

        filter "configurations:Release"
            optimize "on"
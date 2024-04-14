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
    IncDir["pffft"] = "3rdparty/pffft"

    group "3rdparty"
    include "3rdparty/glfw"
    include "3rdparty/imgui"
    include "3rdparty/glad"
    include "3rdparty/pffft"
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
            "opengl32.lib"
        }

        includedirs {
            "%{IncDir.glfw}",
            "%{IncDir.glad}",
            "%{IncDir.imgui}",
            "%{IncDir.pffft}",
            "src",
            "3rdparty/miniaudio"
        }

        files { 
            "src/**.cpp",
            "src/**.hpp"
        }

        filter "system:windows"
            staticruntime "on"
            systemversion "latest"

        filter "configurations:Debug"
            symbols "on"

        filter "configurations:Release"
            optimize "on"
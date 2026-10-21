-- include subprojects
includes("lib/commonlibsse-ng")

-- set project constants
set_project("KickTheDoor")
set_version("1.0.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

-- add common rules
add_rules("mode.debug", "mode.releasedbg")

add_requires("nlohmann_json")

-- define targets
target("KickTheDoor")
    set_version("1.1.0")
    add_rules("commonlibsse-ng.plugin", {
        name = "KickTheDoor",
        author = "W1ter",
        description = "Kick doors open or blast them with Unrelenting Force"
    })

    add_packages("nlohmann_json")

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    -- third party: SKSE Menu Framework API (header only)
    add_headerfiles("lib/SKSEMenuFramework/*.h")
    add_includedirs("lib/SKSEMenuFramework")

-- test: DoorModel::Parse on .nif files (not shipped)
target("niftest")
    set_kind("binary")
    set_default(false)
    add_deps("commonlibsse-ng")
    add_files("test/niftest.cpp", "src/DoorModel.cpp")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

-- test: animation pack loading and picking (not shipped)
target("packtest")
    set_kind("binary")
    set_default(false)
    add_deps("commonlibsse-ng")
    add_packages("nlohmann_json")
    add_files("test/packtest.cpp", "src/Clip.cpp", "src/Forms.cpp", "src/Settings.cpp", "src/Keys.cpp")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

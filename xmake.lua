-- akt toolchain build (primary: XMake)
set_project("akt")
set_version("1.0.0")
set_languages("c11")
set_encodings("utf-8")

add_rules("mode.debug", "mode.release")

local root = os.projectdir()
add_defines('AKT_ROOT="' .. root:gsub("\\", "/") .. '"')
add_defines("_CRT_SECURE_NO_WARNINGS")

target("akt_runtime")
    set_kind("static")
    add_files("runtime/src/*.c")
    add_includedirs("runtime/include", {public = true})
    set_targetdir("bin")

target("aktc")
    set_kind("binary")
    add_files("aktc/src/*.c")
    add_includedirs("aktc/src", "runtime/include")
    add_deps("akt_runtime")
    if not is_plat("windows") then
        add_syslinks("m")
    end
    set_targetdir("bin")

target("katek")
    set_kind("binary")
    add_files("katek/src/*.c")
    add_includedirs("katek/src")
    if not is_plat("windows") then
        add_syslinks("m")
    end
    set_targetdir("bin")

target("akt_tests")
    set_kind("binary")
    add_files("tests/c/*.c")
    add_includedirs("runtime/include")
    add_deps("akt_runtime")
    set_targetdir("bin")

target("akt-setup")
    set_kind("binary")
    add_files("installer/src/*.c")
    set_targetdir("bin")
    if is_plat("windows") then
        add_syslinks("advapi32", "user32")
    end


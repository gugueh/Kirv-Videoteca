cmake_minimum_required(VERSION 3.16)
option(VMP_HOST_TESTS "Build portable tests without VitaSDK" OFF)
if(NOT VMP_HOST_TESTS)
  if(NOT DEFINED ENV{VITASDK})
    message(FATAL_ERROR "VITASDK is required for the Vita build. Use -DVMP_HOST_TESTS=ON for portable tests.")
  endif()
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "VitaSDK toolchain")
endif()
project(VitaMediaPlayerHardened C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_library(vmp_core STATIC src/core.c src/library.c)
target_include_directories(vmp_core PUBLIC include)
target_compile_options(vmp_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(VMP_HOST_TESTS)
  enable_testing()
  add_executable(test_core tests/test_core.c)
  target_link_libraries(test_core PRIVATE vmp_core)
  target_compile_options(test_core PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  add_test(NAME core_regressions COMMAND test_core)
  add_executable(test_alloc_failures tests/test_alloc_failures.c)
  target_include_directories(test_alloc_failures PRIVATE include)
  target_compile_options(test_alloc_failures PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  add_test(NAME allocation_failures COMMAND test_alloc_failures)
  add_executable(test_library tests/test_library.c)
  target_link_libraries(test_library PRIVATE vmp_core)
  target_compile_options(test_library PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  add_test(NAME library_state COMMAND test_library)
else()
  include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
  add_executable(vmp_hardened src/vita_main.c src/offline.c src/ui.c src/browser.c src/viewer.c)
  target_compile_options(vmp_hardened PRIVATE -Wall -Wextra -Werror)
  target_link_options(vmp_hardened PRIVATE
    -Wl,--wrap=sceNetRecv,--wrap=sceNetSend,--wrap=sceNetSocketClose)
  target_link_libraries(vmp_hardened PRIVATE vmp_core vita2d SceAvPlayer_stub
    SceAudio_stub SceCtrl_stub SceTouch_stub SceDisplay_stub SceGxm_stub SceSysmodule_stub
    SceIofilemgr_stub SceSysmem_stub SceKernelThreadMgr_stub SceProcessmgr_stub
    ScePgf_stub ScePvf_stub SceCommonDialog_stub SceAppUtil_stub SceAppMgr_stub
    png jpeg z m c)
  # Normal restricted SELF: no UNSAFE/SECRETSAFE and no external SUPRX files.
  if(VITA_MAKE_FSELF_FLAGS OR VITA_ELF_CREATE_FLAGS)
    message(FATAL_ERROR "Custom SELF/ELF flags require a separate security review.")
  endif()
  vita_create_self(eboot.bin vmp_hardened)
  set(KIRV_UI_FILES)
  foreach(asset background logo library series movies folder play pause next previous volume subtitles settings search sort favorite check back image)
    list(APPEND KIRV_UI_FILES FILE assets/ui/${asset}.png assets/ui/${asset}.png)
  endforeach()
  vita_create_vpk(VitaMediaPlayerHardened-DEV.vpk VMPH00001 eboot.bin
    VERSION 00.23 NAME "Kirv Videoteca DEV"
    FILE assets/icon0.png sce_sys/icon0.png
    FILE assets/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
    FILE assets/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
    FILE assets/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
    ${KIRV_UI_FILES})
endif()

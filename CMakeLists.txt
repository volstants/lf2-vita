cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake"
      CACHE PATH "Toolchain file")
endif()

project(lf2-vita)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Little Fighter 2 Vita")
# NOTE: bumped from LF2V00001 → LF2V00002 because the old TITLE_ID's install
# slot on the target Vita got into a state scePromoterUtil refuses (install
# fails at the promote step, no bubble). A fresh id installs into a clean slot.
set(VITA_TITLEID  "LF2V00002")
set(VITA_VERSION  "02.06")   # 2.00 recompiled lf2.exe; 2.01 no ads/pointer, faster load; 2.02 full screen, special shortcut; 2.03 lf2.exe embedded; 2.04 Square defends, Triangle special, botoes.txt; 2.05 compact HUD (R), faster loading screen, fast sound conversion; 2.06 title menu by d-pad, Triangle alone = D>A, compact HUD crop, log in ux0:data/lf2vita/

# ── Engine ──────────────────────────────────────────────────────────────────
# recomp (default): the original lf2.exe statically recompiled to C
#   (tools/recomp), running on the Win32 emulation layer in src/recomp. The C
#   is GENERATED at build time from the user's own lf2.exe and never committed.
# legacy: the hand-written data-driven reimplementation in src/main.cpp.
set(LF2_ENGINE "recomp" CACHE STRING "recomp | legacy")
set(LF2_EXE "${CMAKE_SOURCE_DIR}/reference/decomp/lf2.exe" CACHE FILEPATH "original lf2.exe v2.0a")
set(LF2_PYTHON "python3" CACHE STRING "python with capstone + pefile")

if(LF2_ENGINE STREQUAL "recomp")
  enable_language(ASM)
  set(GEN "${CMAKE_BINARY_DIR}/recomp")
  set(GEN_SRCS "")
  foreach(i RANGE 0 10)
    if(i LESS 10)
      list(APPEND GEN_SRCS "${GEN}/lf2_gen_0${i}.c")
    else()
      list(APPEND GEN_SRCS "${GEN}/lf2_gen_${i}.c")
    endif()
  endforeach()
  list(APPEND GEN_SRCS "${GEN}/lf2_dispatch.c" "${GEN}/lf2_image.S")   # .S embeds lf2.exe
  add_custom_command(OUTPUT ${GEN_SRCS} "${GEN}/lf2_funcs.h"
    COMMAND ${CMAKE_COMMAND} -E env "LF2_EXE=${LF2_EXE}"
            ${LF2_PYTHON} "${CMAKE_SOURCE_DIR}/tools/recomp/emit.py" "${GEN}"
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/recomp/emit.py" "${CMAKE_SOURCE_DIR}/tools/recomp/analyze.py" "${CMAKE_SOURCE_DIR}/tools/recomp/lf2_seeds.txt" "${LF2_EXE}"
    COMMENT "Recompiling lf2.exe to C"
    VERBATIM)
  add_executable(${PROJECT_NAME}
    ${GEN_SRCS}
    src/recomp/rt_core.cpp src/recomp/win32_api.cpp src/recomp/win32_dx.cpp src/recomp/host_sdl.cpp)
  target_include_directories(${PROJECT_NAME} PRIVATE src/recomp "${GEN}")
  target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
  target_compile_definitions(${PROJECT_NAME} PRIVATE LF2_VITA_VERSION="${VITA_VERSION}")
  set(GEN_C_SRCS ${GEN_SRCS})
  list(FILTER GEN_C_SRCS INCLUDE REGEX "\\.c$")
  set_source_files_properties(${GEN_C_SRCS} PROPERTIES COMPILE_OPTIONS "-O2;-fno-strict-aliasing;-w")
  target_compile_options(${PROJECT_NAME} PRIVATE -O2)
  target_link_libraries(${PROJECT_NAME}
    SDL2_mixer opusfile opus vorbisfile vorbis ogg modplug xmp FLAC mpg123 mikmod
    SDL2 SDL2main stdc++ m pthread
    SceDisplay_stub SceGxm_stub SceCtrl_stub SceSysmodule_stub
    SceAudio_stub SceAudioIn_stub SceMotion_stub SceTouch_stub SceHid_stub SceIme_stub
    SceCommonDialog_stub ScePgf_stub SceAppMgr_stub SceKernelDmacMgr_stub ScePower_stub)
else()
  add_executable(${PROJECT_NAME}
    src/main.cpp
  )
  target_include_directories(${PROJECT_NAME} PRIVATE src)
  target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
  target_link_libraries(${PROJECT_NAME}
    SDL2_image webp webpdemux webpmux pthread png jpeg
    SDL2 SDL2main
    SceDisplay_stub SceGxm_stub SceCtrl_stub SceSysmodule_stub
    SceAudio_stub SceAudioIn_stub SceMotion_stub SceTouch_stub
    SceHid_stub SceIme_stub SceCommonDialog_stub ScePgf_stub SceAppMgr_stub
  )
endif()

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})

# Package EVERY converted sprite sheet (assets/*.png) and every game data file
# (data/*.dat + data.txt). Globbed at configure time — re-run cmake after adding
# new assets. The .bmp originals are NOT packaged.
file(GLOB VPK_PNGS  RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/assets/*.png)
file(GLOB VPK_DATS  RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/data/*.dat)
# bg.dat scene descriptors, parsed at runtime (dat::loadBackground). Preserve
# the original bg/sys/<scene>/ path so multi-stage support is just more globs.
file(GLOB VPK_BGS   RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/bg/sys/*/bg.dat)
# Keep arguments as a CMake list and use VERBATIM. The installed VitaSDK
# vita_create_vpk macro flattens resource paths into a string, then splits on
# spaces. That breaks packaging from the canonical "Little Fighter 2" folder.
# The VPK built here holds only the program (the game folder is read from
# ux0:data/lf2/ or, in the "completo" package that build-vita.sh assembles,
# from app0:/lf2). lf2.exe itself is embedded in the eboot (lf2_image.S).
set(VPK_RESOURCE_FILES
  # Custom icon + LiveArea. Vita's promoter requires these as 8-bit PALETTE
  # (indexed, colortype 3) PNGs — truecolor RGB/RGBA is rejected ("corrupted"
  # package, no bubble). Keep any future art in that format.
  sce_sys/icon0.png
  sce_sys/livearea/contents/bg.png
  sce_sys/livearea/contents/startup.png
  sce_sys/livearea/contents/template.xml
)
if(LF2_ENGINE STREQUAL "legacy")
  list(APPEND VPK_RESOURCE_FILES ${VPK_PNGS} ${VPK_DATS} ${VPK_BGS} data/data.txt)
endif()
set(VPK_PACK_ARGUMENTS "")
set(VPK_RESOURCE_DEPENDS "")
foreach(f IN LISTS VPK_RESOURCE_FILES)
  list(APPEND VPK_PACK_ARGUMENTS -a "${CMAKE_SOURCE_DIR}/${f}=${f}")
  list(APPEND VPK_RESOURCE_DEPENDS "${CMAKE_SOURCE_DIR}/${f}")
endforeach()

set(VPK_SFO "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.vpk_param.sfo")
set(VPK_SELF "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.self")
set(VPK_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.vpk")
add_custom_command(OUTPUT "${VPK_SFO}"
  COMMAND "${VITA_MKSFOEX}" -s "APP_VER=${VITA_VERSION}"
    -s "TITLE_ID=${VITA_TITLEID}" "${VITA_APP_NAME}" "${VPK_SFO}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
  COMMENT "Generating param.sfo for ${PROJECT_NAME}.vpk"
  VERBATIM
)
add_custom_command(OUTPUT "${VPK_OUTPUT}"
  COMMAND "${VITA_PACK_VPK}" ${VPK_PACK_ARGUMENTS}
    -s "${VPK_SFO}" -b "${VPK_SELF}" "${VPK_OUTPUT}"
  DEPENDS "${VPK_SFO}" "${VPK_SELF}" ${VPK_RESOURCE_DEPENDS}
  COMMENT "Building ${PROJECT_NAME}.vpk with paths containing spaces"
  VERBATIM
)
add_custom_target(${PROJECT_NAME}.vpk-vpk ALL DEPENDS "${VPK_OUTPUT}")
if(TARGET ${PROJECT_NAME}.self-self)
  add_dependencies(${PROJECT_NAME}.vpk-vpk ${PROJECT_NAME}.self-self)
else()
  add_dependencies(${PROJECT_NAME}.vpk-vpk ${PROJECT_NAME}.self)
endif()

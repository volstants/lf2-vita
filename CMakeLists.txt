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
set(VITA_VERSION  "01.08")   # v0.8.0 — objetos/projéteis, MP, seleção de personagem

# All headers are include-only (no .cpp besides main).
# Add subdirectories so #include "engine/..." and "characters/..." resolve.
add_executable(${PROJECT_NAME}
  src/main.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME}
  SDL2_image webp webpdemux webpmux pthread png jpeg
  SDL2 SDL2main
  SceDisplay_stub SceGxm_stub SceCtrl_stub SceSysmodule_stub
  SceAudio_stub SceAudioIn_stub SceMotion_stub SceTouch_stub
  SceHid_stub SceIme_stub SceCommonDialog_stub ScePgf_stub SceAppMgr_stub
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})

# Package EVERY converted sprite sheet (assets/*.png) and every game data file
# (data/*.dat + data.txt). Globbed at configure time — re-run cmake after adding
# new assets. The .bmp originals are NOT packaged.
file(GLOB VPK_PNGS  RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/assets/*.png)
file(GLOB VPK_DATS  RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/data/*.dat)
set(VPK_FILES "")
foreach(f ${VPK_PNGS} ${VPK_DATS} data/data.txt)
  list(APPEND VPK_FILES FILE ${f} ${f})
endforeach()

vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME    ${VITA_APP_NAME}
  # Custom icon + LiveArea. Vita's promoter requires these as 8-bit PALETTE
  # (indexed, colortype 3) PNGs — truecolor RGB/RGBA is rejected ("corrupted"
  # package, no bubble). Keep any future art in that format.
  FILE sce_sys/icon0.png                      sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png       sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png  sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  ${VPK_FILES}
)

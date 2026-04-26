cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake"
      CACHE PATH "Toolchain file")
endif()

project(lf2-vita)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Little Fighter 2 Vita")
set(VITA_TITLEID  "LF2V00001")
set(VITA_VERSION  "01.05")   # v0.5.0

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
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME    ${VITA_APP_NAME}
  FILE assets/dennis_0.png assets/dennis_0.png
  FILE assets/dennis_1.png assets/dennis_1.png
  FILE assets/firen_0.png  assets/firen_0.png
  FILE assets/forests.png  assets/forests.png
  FILE assets/forestm1.png assets/forestm1.png
  FILE assets/forestm2.png assets/forestm2.png
  FILE assets/forestm3.png assets/forestm3.png
  FILE assets/forestm4.png assets/forestm4.png
  FILE assets/forestt.png  assets/forestt.png
  FILE assets/land1.png    assets/land1.png
  FILE assets/land2.png    assets/land2.png
  FILE assets/land4.png    assets/land4.png
  FILE assets/s.png        assets/s.png
)

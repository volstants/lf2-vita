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
set(VITA_VERSION  "01.07")   # v0.7.0 — inimigos data-driven (Fighter + IA)

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
  # Custom icon + LiveArea, re-encoded as clean 8-bit RGBA (the earlier versions
  # were rejected by scePromoterUtil → package "corrupted", no bubble). If this
  # build fails to install again, the culprit is still here — disable these four
  # FILE lines to fall back to VitaSDK's default sce_sys.
  # Custom icon + LiveArea. Vita's promoter requires these as 8-bit PALETTE
  # (indexed, colortype 3) PNGs — truecolor RGB/RGBA is rejected ("corrupted"
  # package, no bubble). Keep any future art in that format.
  FILE sce_sys/icon0.png                      sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png       sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png  sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE assets/dennis_0.png assets/dennis_0.png
  FILE assets/dennis_1.png assets/dennis_1.png
  FILE assets/dennis_2.png assets/dennis_2.png
  FILE assets/firen_0.png  assets/firen_0.png
  FILE assets/firen_1.png  assets/firen_1.png
  FILE assets/firen_2.png  assets/firen_2.png
  FILE data/dennis.dat     data/dennis.dat
  FILE data/firen.dat      data/firen.dat
  # Lion Forest background layers (converted from bg/sys/lf originals) + shadow.
  # These got dropped in a concurrent rewrite — without them the game renders a
  # bare green floor and no shadows.
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
  # Scenery: the old pre-baked forest/land PNGs are gone (assets wiped and
  # re-sourced from the original LF2). Backgrounds will come data-driven from
  # bg\sys\<stage>\bg.dat once the user copies the original bg/ folder in —
  # including the per-stage shadow bitmap. Until then the game renders a plain
  # ground (renderBackground tolerates missing textures).
)

cmake_minimum_required(VERSION 3.20)
project(SteadyGrip LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # aucun redistribuable VC++ à installer

include(FetchContent)
# En-têtes OpenXR uniquement (SOURCE_SUBDIR inexistant : le loader n'est pas compilé).
FetchContent_Declare(openxr
  GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
  GIT_TAG 24a1f2b92d212501a2b1b8d30ffd53abf55fcfa8
  GIT_SHALLOW FALSE
  SOURCE_SUBDIR headers-only)
FetchContent_MakeAvailable(openxr)

# Couche OpenXR
add_library(SteadyGrip SHARED src/layer/layer.cpp src/common/PoseFilter.h src/common/Shared.h)
target_include_directories(SteadyGrip PRIVATE ${openxr_SOURCE_DIR}/include)
target_link_libraries(SteadyGrip PRIVATE advapi32 shell32 ole32)
add_custom_command(TARGET SteadyGrip POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/src/layer/XR_APILAYER_NOVENDOR_steadygrip.json $<TARGET_FILE_DIR:SteadyGrip>)

# SDK WebView2 (en-têtes et chargeur statique) : paquet NuGet officiel de Microsoft, téléchargé à la configuration.
# Le chargeur statique évite de livrer WebView2Loader.dll ; le moteur lui-même (WebView2 Runtime) est déjà
# installé avec Windows 10 et 11.
FetchContent_Declare(webview2
  URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2210.55
  DOWNLOAD_NAME webview2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(webview2)

# Application compagnon
# Elle demande les droits d'administrateur au lancement : nécessaires pour lire (sans rien modifier)
# l'état de suivi des manettes dans le service Pimax. Son interface est la page du dossier « ui ».
add_executable(SteadyGripApp WIN32 src/app/app.cpp src/app/PimaxReader.h src/app/Json.h src/app/Library.h
  src/common/LogBuffer.h src/common/Steam.h)
target_include_directories(SteadyGripApp PRIVATE ${webview2_SOURCE_DIR}/build/native/include)
target_link_libraries(SteadyGripApp PRIVATE ${webview2_SOURCE_DIR}/build/native/x64/WebView2LoaderStatic.lib
  advapi32 shell32 ole32 psapi winmm version urlmon user32)
add_custom_command(TARGET SteadyGripApp POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/ui $<TARGET_FILE_DIR:SteadyGripApp>/ui)
target_link_options(SteadyGripApp PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")

# Tests (sans casque)
add_executable(test_lossguard tests/test_lossguard.cpp)
target_compile_options(test_lossguard PRIVATE /utf-8)
add_executable(test_app tests/test_app.cpp)
target_compile_options(test_app PRIVATE /utf-8)
# Évaluation de la stabilisation de l'orientation sur une session TrackProbe : orient_eval session.csv
add_executable(orient_eval tests/orient_eval.cpp)
target_compile_options(orient_eval PRIVATE /utf-8)

foreach(t SteadyGrip SteadyGripApp)
  target_compile_options(${t} PRIVATE /utf-8 /W3)
  target_compile_definitions(${t} PRIVATE UNICODE _UNICODE)
endforeach()

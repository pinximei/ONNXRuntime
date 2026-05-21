cmake_minimum_required(VERSION 3.20)
project(ui_detector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# Reuse the ONNX Runtime + stb fetched by the main project.
set(MAIN_ROOT "${CMAKE_SOURCE_DIR}/../.." CACHE PATH "Main project root")
set(ORT_ROOT  "${MAIN_ROOT}/third_party/onnxruntime")
set(STB_DIR   "${MAIN_ROOT}/third_party/stb")

if(NOT EXISTS "${ORT_ROOT}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "ONNX Runtime not found at ${ORT_ROOT}. Run main scripts\\fetch_onnxruntime.ps1")
endif()
if(NOT EXISTS "${STB_DIR}/stb_image.h")
    message(FATAL_ERROR "stb_image.h not found at ${STB_DIR}. Run main scripts\\fetch_stb.ps1")
endif()

# ---- Shared core library (used by both CLI and GUI) ----
add_library(ui_core STATIC
    src/detector.cpp
    src/recognizer.cpp
    src/ocr_pipeline.cpp
    src/image_io.cpp
)
target_include_directories(ui_core PUBLIC
    src
    "${ORT_ROOT}/include"
    "${STB_DIR}"
)
target_link_libraries(ui_core PUBLIC
    "${ORT_ROOT}/lib/onnxruntime.lib"
)

# ---- CLI executable ----
add_executable(detect src/main.cpp)
target_link_libraries(detect PRIVATE ui_core)

add_custom_command(TARGET detect POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ORT_ROOT}/lib/onnxruntime.dll"
        "$<TARGET_FILE_DIR:detect>/onnxruntime.dll"
)

# ---- MFC GUI executable ----
# MFC requires Unicode + shared CRT (CMake's MFC support uses /MD/_AFXDLL).
set(CMAKE_MFC_FLAG 2)              # 2 = use MFC in a Shared DLL
add_compile_definitions(_AFXDLL UNICODE _UNICODE)

add_executable(detect_gui WIN32
    src/gui/stdafx.cpp
    src/gui/App.cpp
    src/gui/MainFrame.cpp
    src/gui/ImageView.cpp
    src/gui/Worker.cpp
    src/gui/Resource.rc
)
target_link_libraries(detect_gui PRIVATE ui_core)
target_include_directories(detect_gui PRIVATE src/gui)
target_precompile_headers(detect_gui PRIVATE src/gui/stdafx.h)

add_custom_command(TARGET detect_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ORT_ROOT}/lib/onnxruntime.dll"
        "$<TARGET_FILE_DIR:detect_gui>/onnxruntime.dll"
)

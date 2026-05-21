cmake_minimum_required(VERSION 3.20)
project(onnx_image_classifier LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# ---- ONNX Runtime ----
# Expect the prebuilt CPU package extracted under third_party/onnxruntime/
# Folder layout (after running scripts/fetch_onnxruntime.ps1):
#   third_party/onnxruntime/include/*.h
#   third_party/onnxruntime/lib/onnxruntime.lib
#   third_party/onnxruntime/lib/onnxruntime.dll
set(ORT_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime" CACHE PATH "ONNX Runtime root")

if(NOT EXISTS "${ORT_ROOT}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR
        "ONNX Runtime headers not found at ${ORT_ROOT}/include.\n"
        "Run: powershell -ExecutionPolicy Bypass -File scripts/fetch_onnxruntime.ps1")
endif()

# ---- stb_image (single-header image loader) ----
set(STB_DIR "${CMAKE_SOURCE_DIR}/third_party/stb")
if(NOT EXISTS "${STB_DIR}/stb_image.h")
    message(FATAL_ERROR
        "stb_image.h not found at ${STB_DIR}.\n"
        "Run: powershell -ExecutionPolicy Bypass -File scripts/fetch_stb.ps1")
endif()

add_executable(classify
    src/main.cpp
    src/classifier.cpp
    src/imagenet_labels.cpp
)

target_include_directories(classify PRIVATE
    src
    "${ORT_ROOT}/include"
    "${STB_DIR}"
)

target_link_libraries(classify PRIVATE
    "${ORT_ROOT}/lib/onnxruntime.lib"
)

# Copy onnxruntime.dll next to the exe so it runs out of the build dir.
add_custom_command(TARGET classify POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ORT_ROOT}/lib/onnxruntime.dll"
        "$<TARGET_FILE_DIR:classify>/onnxruntime.dll"
)

# Convenience: install into ./dist for redistribution
install(TARGETS classify RUNTIME DESTINATION dist)
install(FILES "${ORT_ROOT}/lib/onnxruntime.dll" DESTINATION dist)
install(DIRECTORY models DESTINATION dist OPTIONAL)

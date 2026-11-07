# Compiles GLSL to SPIR-V with glslc (shaderc) or glslangValidator.

find_program(NMSC_GLSLC glslc)
find_program(NMSC_GLSLANG NAMES glslangValidator glslang)
if(NOT NMSC_GLSLC AND NOT NMSC_GLSLANG)
  message(FATAL_ERROR "Need glslc (shaderc) or glslangValidator to compile shaders")
endif()

set(NMSC_SHADER_OUT ${CMAKE_BINARY_DIR}/shaders)
# The run folder's copy (build/hailstone/shaders, where the executable looks): each shader is copied there when it
# is compiled, so a change to a shader alone reaches it without the executable being relinked.
set(NMSC_SHADER_RUN ${CMAKE_BINARY_DIR}/hailstone/shaders)
# Files the shaders #include (GL_GOOGLE_include_directive): a change rebuilds every shader.
set(NMSC_SHADER_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/shaders/lighting.glsl
                          ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ao_kernel.glsl
                          ${CMAKE_CURRENT_SOURCE_DIR}/shaders/coverage.glsl
                          ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.glsl
                          ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fsr1/ffx_a.h
                          ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fsr1/ffx_fsr1.h)
# AMD's FSR 1 headers (fsr_easu.frag, fsr_rcas.frag).
set(NMSC_SHADER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fsr1)

function(nmsc_add_shaders target)
  set(outputs)
  foreach(src ${ARGN})
    get_filename_component(name ${src} NAME)
    set(out ${NMSC_SHADER_OUT}/${name}.spv)
    if(NMSC_GLSLC)
      set(cmd ${NMSC_GLSLC} --target-env=vulkan1.3 -O -I ${NMSC_SHADER_INCLUDE_DIR} -o ${out} ${CMAKE_CURRENT_SOURCE_DIR}/${src})
    else()
      set(cmd ${NMSC_GLSLANG} -V --target-env vulkan1.3 -I${NMSC_SHADER_INCLUDE_DIR} -o ${out} ${CMAKE_CURRENT_SOURCE_DIR}/${src})
    endif()
    add_custom_command(OUTPUT ${out}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${NMSC_SHADER_OUT}
      COMMAND ${cmd}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${NMSC_SHADER_RUN}
      COMMAND ${CMAKE_COMMAND} -E copy ${out} ${NMSC_SHADER_RUN}/${name}.spv
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src} ${NMSC_SHADER_INCLUDES}
      COMMENT "Compiling shader ${name}"
      VERBATIM)
    list(APPEND outputs ${out})
  endforeach()
  add_custom_target(${target} DEPENDS ${outputs})
endfunction()

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(AFFINE_ARM_NONE_EABI_TOOLCHAIN_ROOT "" CACHE PATH
    "Path to the arm-none-eabi toolchain root or bin directory")

set(AFFINE_ARM_NONE_EABI_TOOLCHAIN_HINTS)

function(affine_append_toolchain_hint candidate)
    if(EXISTS "${candidate}")
        list(APPEND AFFINE_ARM_NONE_EABI_TOOLCHAIN_HINTS "${candidate}")
        set(AFFINE_ARM_NONE_EABI_TOOLCHAIN_HINTS
            "${AFFINE_ARM_NONE_EABI_TOOLCHAIN_HINTS}"
            PARENT_SCOPE)
    endif()
endfunction()

if(AFFINE_ARM_NONE_EABI_TOOLCHAIN_ROOT)
    affine_append_toolchain_hint("${AFFINE_ARM_NONE_EABI_TOOLCHAIN_ROOT}")
    affine_append_toolchain_hint("${AFFINE_ARM_NONE_EABI_TOOLCHAIN_ROOT}/bin")
elseif(DEFINED ENV{ARM_NONE_EABI_TOOLCHAIN_ROOT})
    affine_append_toolchain_hint("$ENV{ARM_NONE_EABI_TOOLCHAIN_ROOT}")
    affine_append_toolchain_hint("$ENV{ARM_NONE_EABI_TOOLCHAIN_ROOT}/bin")
endif()

if(AFFINE_ARM_NONE_EABI_TOOLCHAIN_HINTS)
    find_program(AFFINE_ARM_NONE_EABI_GCC
        NAMES arm-none-eabi-gcc arm-none-eabi-gcc.exe
        HINTS ${AFFINE_ARM_NONE_EABI_TOOLCHAIN_HINTS}
        NO_DEFAULT_PATH)
else()
    find_program(AFFINE_ARM_NONE_EABI_GCC
        NAMES arm-none-eabi-gcc arm-none-eabi-gcc.exe)
endif()

if(NOT AFFINE_ARM_NONE_EABI_GCC)
    message(FATAL_ERROR
        "arm-none-eabi-gcc was not found. Install an Arm GNU toolchain and either:\n"
        "  - add its bin directory to PATH,\n"
        "  - set ARM_NONE_EABI_TOOLCHAIN_ROOT, or\n"
        "  - configure with -DAFFINE_ARM_NONE_EABI_TOOLCHAIN_ROOT=<toolchain-root-or-bin>."
    )
endif()

get_filename_component(AFFINE_ARM_NONE_EABI_TOOLCHAIN_BIN "${AFFINE_ARM_NONE_EABI_GCC}" DIRECTORY)

function(affine_find_arm_none_eabi_tool out_var tool_name)
    string(MAKE_C_IDENTIFIER "${tool_name}" tool_cache_suffix)
    set(tool_cache_var "AFFINE_${tool_cache_suffix}")

    find_program(${tool_cache_var}
        NAMES ${tool_name} ${tool_name}.exe
        HINTS "${AFFINE_ARM_NONE_EABI_TOOLCHAIN_BIN}"
        NO_DEFAULT_PATH)

    if(NOT ${tool_cache_var})
        message(FATAL_ERROR
            "${tool_name} was not found next to ${AFFINE_ARM_NONE_EABI_GCC}."
        )
    endif()

    set(${out_var} "${${tool_cache_var}}" PARENT_SCOPE)
endfunction()

set(CMAKE_C_COMPILER "${AFFINE_ARM_NONE_EABI_GCC}")
set(CMAKE_ASM_COMPILER "${AFFINE_ARM_NONE_EABI_GCC}")
affine_find_arm_none_eabi_tool(CMAKE_AR arm-none-eabi-ar)
affine_find_arm_none_eabi_tool(CMAKE_RANLIB arm-none-eabi-ranlib)
affine_find_arm_none_eabi_tool(CMAKE_OBJCOPY arm-none-eabi-objcopy)
affine_find_arm_none_eabi_tool(CMAKE_SIZE arm-none-eabi-size)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_ASM_COMPILER_FORCED TRUE)
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_ASM_COMPILER_WORKS TRUE)

set(CMAKE_EXECUTABLE_SUFFIX_C ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_ASM ".elf")

set(CMAKE_C_LINK_EXECUTABLE
    "<CMAKE_C_COMPILER> <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
set(CMAKE_ASM_LINK_EXECUTABLE
    "<CMAKE_C_COMPILER> <FLAGS> <CMAKE_ASM_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)

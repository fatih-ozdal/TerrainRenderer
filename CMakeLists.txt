
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 20)
set(OpenGL_GL_PREFERENCE GLVND)

project(CENG469-Terrain LANGUAGES C CXX)

# Handle External Libraries
add_subdirectory(ext)

# ============================= #
#  Some useful Compile Options  #
# ============================= #
add_library(compile_options INTERFACE)
if(UNIX AND NOT APPLE)
    # Linux GCC or CLANG
    target_compile_options(
        compile_options INTERFACE
        -Wall
        -Wextra                 # reasonable and standard
        -Wshadow                # warn the user if a variable declaration shadows one from a parent context
        -Wnon-virtual-dtor      # warn the user if a class with virtual functions has a non-virtual destructor. This helps
                                # catch hard to track down memory errors
        -Wold-style-cast        # warn for c-style casts
        -Wcast-align            # warn for potential performance problem casts
        -Wunused                # warn on anything being unused
        -Woverloaded-virtual    # warn if you overload (not override) a virtual function
        -Wpedantic              # warn if non-standard C++ is used
        -Wconversion            # warn on type conversions that may lose data
        -Wsign-conversion       # warn on sign conversions
        -Wnull-dereference      # warn if a null dereference is detected
        -Wdouble-promotion      # warn if float is implicit promoted to double
        -Wformat=2              # warn on security issues around functions that format output (ie printf)
        -Wimplicit-fallthrough  # warn on statements that fallthrough without an explicit annotation
    )
else()
    # Windows MSVC
    target_compile_options(
        compile_options INTERFACE
        /w14242         # 'identifier': conversion from 'type1' to 'type1', possible loss of data
        /w14254         # 'operator': conversion from 'type1:field_bits' to 'type2:field_bits', possible loss of data
        /w14263         # 'function': member function does not override any base class virtual member function
        /w14265         # 'classname': class has virtual functions, but destructor is not virtual instances of this class may not
                        # be destructed correctly
        /w14287         # 'operator': unsigned/negative constant mismatch
        /we4289         # nonstandard extension used: 'variable': loop control variable declared in the for-loop is used outside
                        # the for-loop scope
        /w14296         # 'operator': expression is always 'boolean_value'
        /w14311         # 'variable': pointer truncation from 'type1' to 'type2'
        /w14545         # expression before comma evaluates to a function which is missing an argument list
        /w14546         # function call before comma missing argument list
        /w14547         # 'operator': operator before comma has no effect; expected operator with side-effect
        /w14549         # 'operator': operator before comma has no effect; did you intend 'operator'?
        /w14555         # expression has no effect; expected expression with side- effect
        /w14619         # pragma warning: there is no warning number 'number'
        /w14640         # Enable warning on thread un-safe static member initialization
        /w14826         # Conversion from 'type1' to 'type_2' is sign-extended. This may cause unexpected runtime behavior.
        /w14905         # wide string literal cast to 'LPSTR'
        /w14906         # string literal cast to 'LPWSTR'
        /w14928         # illegal copy-initialization; more than one user-defined conversion has been implicitly applied

        # Most important flag for MSVC
        /permissive-    # standards conformance mode for MSVC compiler.

        /external:anglebrackets # Minimize warnings on external stuff
        /external:W0            # i.e. it is included with the <...> syntax.

        /D_CRT_SECURE_NO_WARNINGS # warns about fopen (since it reads from a str)
    )
endif()

# ================= #
#  Main Executable  #
# ================= #

# If you want to add additional files, add these to this list
set(SRC_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utility.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utility.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/thread_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/thread_pool.h
    # For example,
    # ${CMAKE_CURRENT_SOURCE_DIR}/src/myNewFile.cpp
    )
# Add shaders here, so that they are visible in IDE (if you use ide)
set(CENG_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/working_dir/shaders)
set(SRC_SHADERS
    ${CENG_SHADER_DIR}/generic.vert
    ${CENG_SHADER_DIR}/generic.frag)

source_group("" FILES ${SRC_ALL})
source_group("Shaders" FILES ${SRC_SHADERS})

find_package(OpenGL)

add_executable(TerrainRenderer)
target_sources(TerrainRenderer PRIVATE ${SRC_ALL} ${SRC_SHADERS})
target_link_libraries(TerrainRenderer
                      PRIVATE
                        glfw
                        glad
                        stb_image
                        glm
                        dear_imgui
                        compile_options
                        OpenGL::GL)

# Executable will be compiled to the 'working_dir'
set_target_properties(TerrainRenderer PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/working_dir)

# On linux we need add X11 as dependency
if(UNIX AND NOT APPLE)
    target_link_libraries(TerrainRenderer PRIVATE X11)
else() # Windows
    target_link_libraries(TerrainRenderer)

    # Set some debug flags for fast debugging
    # for visual studio IDE
    set_target_properties(TerrainRenderer PROPERTIES
                          VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/working_dir)

endif()



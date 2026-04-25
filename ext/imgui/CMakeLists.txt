
add_library(dear_imgui)
target_sources(dear_imgui PRIVATE
                imgui.cpp
                imgui_demo.cpp
                imgui_draw.cpp
                imgui_impl_glfw.cpp
                imgui_impl_opengl3.cpp
                imgui_tables.cpp
                imgui_widgets.cpp)
target_include_directories(dear_imgui PRIVATE ../glfw/include)
target_include_directories(dear_imgui PRIVATE imgui)
target_include_directories(dear_imgui PUBLIC .)
set_target_properties(dear_imgui PROPERTIES FOLDER Ext)
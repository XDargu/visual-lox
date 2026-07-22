set(_VisualLoxCoreSources
    utilities/utils.h
    utilities/utils.cpp
    graphs/idgeneration.h
    graphs/node.h
    graphs/node.cpp
    graphs/link.h
    graphs/link.cpp
    graphs/graph.h
    graphs/graph.cpp
    graphs/graphCompiler.h
    graphs/graphCompiler.cpp
    graphs/nodeRegistry.h
    graphs/nodeRegistry.cpp
    shared/functionShared.h
    shared/functionShared.cpp
    native/nodes/begin.h
    native/nodes/begin.cpp
    native/nodes/return.h
    native/nodes/return.cpp
    native/nodes/append.h
    native/nodes/append.cpp
    native/nodes/branch.h
    native/nodes/branch.cpp
    native/nodes/for-in.h
    native/nodes/for-in.cpp
    native/nodes/print.h
    native/nodes/print.cpp
    native/nodes/math.h
    native/nodes/math.cpp
    native/nodes/match.h
    native/nodes/match.cpp
    native/nodes/range.h
    native/nodes/range.cpp
    native/nodes/object.h
    native/nodes/object.cpp
    native/nodes/list.h
    native/nodes/list.cpp
    native/nodes/variable.h
    native/nodes/variable.cpp
    native/nodes/function.h
    native/nodes/function.cpp
    script/scriptElement.h
    script/script.h
    script/script.cpp
    script/scriptSerializer.h
    script/scriptSerializer.cpp
    script/class.h
    script/class.cpp
    script/property.h
    script/property.cpp
    script/function.h
    script/function.cpp
    runtime/standardLibrary.h
    runtime/standardLibrary.cpp
    runtime/constantFolder.h
    runtime/constantFolder.cpp
    runtime/scriptRuntime.h
    runtime/scriptRuntime.cpp
    validation/scriptValidator.h
    validation/scriptValidator.cpp
    operations/documentOperations.h
    operations/documentOperations.cpp
)

find_package(imgui_node_editor REQUIRED)

add_library(visual-lox-core STATIC ${_VisualLoxCoreSources})
target_include_directories(visual-lox-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(visual-lox-core PUBLIC lox imgui_node_editor)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${_VisualLoxCoreSources})
set_property(TARGET visual-lox-core PROPERTY FOLDER "visual-lox")

set(_VisualLoxEditorSources
    blueprints-example.cpp
    editor.h
    editor.cpp
    editorActions.h
    editorActions.cpp
    utilities/builders.h
    utilities/builders.cpp
    utilities/drawing.h
    utilities/drawing.cpp
    utilities/widgets.h
    utilities/widgets.cpp
    utilities/treeview.h
    utilities/treeview.cpp
    graphs/graphView.h
    graphs/graphView.cpp
)

add_example_executable(visual-lox-editor ${_VisualLoxEditorSources})
target_link_libraries(visual-lox-editor PRIVATE visual-lox-core)
set_property(TARGET visual-lox-editor PROPERTY FOLDER "visual-lox")

if (WIN32)
    target_link_libraries(visual-lox-editor PRIVATE comdlg32)
endif()

add_executable(visual-lox-cli cli/main.cpp)
target_link_libraries(visual-lox-cli PRIVATE visual-lox-core)
set_target_properties(visual-lox-cli PROPERTIES
    FOLDER "visual-lox"
    RUNTIME_OUTPUT_DIRECTORY                "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_BINARY_DIR}/bin"
    DEBUG_POSTFIX                           _d
)

if (BUILD_TESTING)
    add_executable(visual-lox-tests
        tests/main.cpp
        tests/runtimeTests.h
        tests/runtimeTests.cpp
        tests/documentOperationsTests.h
        tests/documentOperationsTests.cpp
        script/scriptSerializerTest.h
        script/scriptSerializerTest.cpp
    )
    target_link_libraries(visual-lox-tests PRIVATE visual-lox-core)
    set_target_properties(visual-lox-tests PROPERTIES
        FOLDER "visual-lox/tests"
        RUNTIME_OUTPUT_DIRECTORY                "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_BINARY_DIR}/bin"
        DEBUG_POSTFIX                           _d
    )

    add_test(
        NAME visual-lox-tests
        COMMAND $<TARGET_FILE:visual-lox-tests>
                ${CMAKE_CURRENT_BINARY_DIR}/serialization-roundtrip-test.vlox
    )

    add_test(
        NAME visual-lox-cli-smoke
        COMMAND $<TARGET_FILE:visual-lox-cli>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/hello.vlox
    )
endif()

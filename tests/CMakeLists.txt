find_package (Python3 REQUIRED COMPONENTS Interpreter)

function (SetPortableTestCompilerOptions target)
	target_compile_features (${target} PRIVATE cxx_std_20)
	if (MSVC)
		target_compile_options (${target} PRIVATE /W4 /WX)
	else ()
		target_compile_options (${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
	endif ()
endfunction ()

add_executable (DropViewTriangulationTests triangulation.cpp)
target_include_directories (DropViewTriangulationTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../Src")
SetPortableTestCompilerOptions (DropViewTriangulationTests)

add_test (NAME triangulation COMMAND DropViewTriangulationTests)

add_executable (DropViewSurfaceNormalTests surface_normals.cpp)
target_include_directories (DropViewSurfaceNormalTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../Src")
SetPortableTestCompilerOptions (DropViewSurfaceNormalTests)

add_test (NAME surface_normals COMMAND DropViewSurfaceNormalTests)

add_library (DropViewGlbWriter STATIC ../Src/GlbWriter.cpp)
target_include_directories (DropViewGlbWriter PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../Src")
SetPortableTestCompilerOptions (DropViewGlbWriter)

add_executable (DropViewGlbWriterFixture glb_writer_fixture.cpp)
target_link_libraries (DropViewGlbWriterFixture PRIVATE DropViewGlbWriter)
SetPortableTestCompilerOptions (DropViewGlbWriterFixture)

add_test (
	NAME glb_writer
	COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/validate_glb.py" "$<TARGET_FILE:DropViewGlbWriterFixture>"
)

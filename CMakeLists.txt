# Adapted from the Graphisoft API DevKit example build configuration.
cmake_minimum_required (VERSION 3.16)

option (DROPVIEW_TESTS_ONLY "Configure the portable tests without an Archicad API DevKit." OFF)
if (DROPVIEW_TESTS_ONLY)
	project (DropViewGLBExporterTests LANGUAGES CXX)
	include (CTest)
	add_subdirectory (tests)
	return ()
endif ()

# Find Python 3
find_package(Python3 REQUIRED COMPONENTS Interpreter)
message(STATUS "Using Python: ${Python3_EXECUTABLE} (version ${Python3_VERSION})")

function (SetCompilerOptions target)
	target_compile_features (${target} PUBLIC cxx_std_20)
	target_compile_options (${target} PUBLIC "$<$<CONFIG:Debug>:-DDEBUG>")
	if (WIN32)
		target_compile_options (${target} PUBLIC /W3 /WX /wd4996 /Zc:wchar_t-)
	else ()
		# Archicad 28/29 SDK headers trigger these diagnostics under current AppleClang.
		# Keep the exceptions narrow so warnings in our own sources remain build errors.
		target_compile_options (${target} PUBLIC -Wall -Wextra -Wpedantic -Werror -fvisibility=hidden
			"$<$<CONFIG:Release>:-O2>"
			-Wno-deprecated-literal-operator
			-Wno-deprecated-enum-compare-conditional
			-Wno-gnu-zero-variadic-macro-arguments
			-Wno-deprecated-volatile
			-Wno-reorder-ctor
			-Wno-nested-anon-types)
	endif ()
endfunction ()

function (AddGSModuleIncludeFolder target folder)
	message (STATUS "AddGSModuleIncludeFolder ${folder}")
	target_include_directories (${target} PUBLIC "${folder}")
endfunction ()

function (AddGSModuleLinkLibrary target library)
	if (WIN32)
		file (GLOB libs "${library}/Win/*.lib")
		foreach (lib ${libs})
			message (STATUS "AddGSModuleLinkLibrary ${lib}")
			target_link_libraries (${target} ${lib})
		endforeach ()
	else ()
		message (STATUS "AddGSModuleLinkLibrary ${library}")
		target_link_libraries (${target} ${library})
	endif ()
endfunction ()


set_property (GLOBAL PROPERTY USE_FOLDERS ON)

get_filename_component (CMAKE_CURRENT_FOLDER_NAME "${CMAKE_CURRENT_LIST_DIR}" NAME)
set (CMAKE_SUPPRESS_REGENERATION 1)
set (CMAKE_CONFIGURATION_TYPES Debug;Release;RelWithDebInfo)
if (WIN32)
	set (CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
else ()
	set (CMAKE_OSX_DEPLOYMENT_TARGET 26.0)
	set (CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Supported macOS architecture" FORCE)
	if (CMAKE_GENERATOR STREQUAL Xcode)
		set (CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO")
	endif ()
endif ()
set (AC_API_DEVKIT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "API DevKit directory.")
set (AC_ADDON_NAME "DropViewGLBExporter" CACHE STRING "Add-On name.")
set (AC_ADDON_LANGUAGE "INT" CACHE STRING "Add-On language code.")
set (DROPVIEW_VERSION "0.4.0-beta" CACHE STRING "Drop & View GLB Exporter version.")

message (STATUS "APIDevKit directory: ${AC_API_DEVKIT_DIR}")
set (ACAPINC_FILE_LOCATION ${AC_API_DEVKIT_DIR}/Support/Inc/ACAPinc.h)
if (EXISTS ${ACAPINC_FILE_LOCATION})
	file (READ ${ACAPINC_FILE_LOCATION} ACAPIncContent)
	string (REGEX MATCHALL "#define[ \t]+ServerMainVers_[0-9][0-9]00" VersionList ${ACAPIncContent})
	list (LENGTH VersionList VersionCount)
	if (VersionCount EQUAL 0)
		message (FATAL_ERROR "Failed to detect the Archicad version from ${ACAPINC_FILE_LOCATION}.")
	endif ()
	list (GET VersionList -1 CurrentVersionDefine)
	string (REGEX REPLACE ".*ServerMainVers_([0-9][0-9])00" "\\1" ARCHICAD_VERSION ${CurrentVersionDefine})
	message (STATUS "Archicad Version: ${ARCHICAD_VERSION}")
	if (ARCHICAD_VERSION LESS 28 OR ARCHICAD_VERSION GREATER 29)
		message (FATAL_ERROR "Unsupported Archicad version ${ARCHICAD_VERSION}. Supported versions are 28 and 29.")
	endif ()
else ()
	message (FATAL_ERROR "Failed to detect Archicad version, please check the value of the AC_API_DEVKIT_DIR variable.")
endif ()

if (WIN32)
	add_definitions (-DUNICODE -D_UNICODE -D_ITERATOR_DEBUG_LEVEL=0 -DGS_WIN)
else ()
	add_definitions (-Dmacintosh=1 -DGS_MAC)
endif ()
add_definitions (-DACExtension)

project (${AC_ADDON_NAME})

include (CTest)
if (BUILD_TESTING)
	add_subdirectory (tests)
endif ()

set (AddOnSourcesFolder ./Src)
set (AddOnResourcesFolder .)

# AddOnResources

set (ResourceObjectsDir ${CMAKE_BINARY_DIR}/ResourceObjects)

file (GLOB AddOnImageFiles
	${AddOnResourcesFolder}/RFIX/Images/*.svg
)
if (WIN32)
	file (GLOB AddOnResourceFiles
		${AddOnResourcesFolder}/R${AC_ADDON_LANGUAGE}/*.grc
		${AddOnResourcesFolder}/RFIX/*.grc
		${AddOnResourcesFolder}/RFIX.win/*.rc2
	)
	file (GLOB NativeWindowsResourceFiles
		${AddOnResourcesFolder}/RFIX.win/*.rc2
	)
	list (LENGTH NativeWindowsResourceFiles NativeWindowsResourceFileCount)
	if (NOT NativeWindowsResourceFileCount EQUAL 1)
		message (FATAL_ERROR "Exactly one RFIX.win/*.rc2 file is required, found ${NativeWindowsResourceFileCount}.")
	endif ()
	list (GET NativeWindowsResourceFiles 0 NativeWindowsResourceFile)
	file (READ "${NativeWindowsResourceFile}" NativeWindowsResourceContent)
	file (GLOB GrcResourceFiles
		${AddOnResourcesFolder}/R${AC_ADDON_LANGUAGE}/*.grc
		${AddOnResourcesFolder}/RFIX/*.grc
	)
	foreach (GrcResourceFile ${GrcResourceFiles})
		get_filename_component (GrcResourceName "${GrcResourceFile}" NAME)
		set (GeneratedWindowsResourceName "${GrcResourceName}.rc2")
		string (FIND "${NativeWindowsResourceContent}" "\"${GeneratedWindowsResourceName}\"" GeneratedWindowsResourceIncludeIndex)
		if (GeneratedWindowsResourceIncludeIndex EQUAL -1)
			message (FATAL_ERROR "${NativeWindowsResourceFile} must include ${GeneratedWindowsResourceName}; otherwise the APX is missing required Archicad resources.")
		endif ()
	endforeach ()
else ()
	file (GLOB AddOnResourceFiles
		${AddOnResourcesFolder}/R${AC_ADDON_LANGUAGE}/*.grc
		${AddOnResourcesFolder}/RFIX/*.grc
		${AddOnResourcesFolder}/RFIX.mac/*.plist
	)
endif ()

source_group ("Images" FILES ${AddOnImageFiles})
source_group ("Resources" FILES ${AddOnResourceFiles})
add_custom_target (
	AddOnResources ALL
	DEPENDS "${ResourceObjectsDir}/AddOnResources.stamp"
	SOURCES ${AddOnResourceFiles} ${AddOnImageFiles}
)

get_filename_component (AddOnSourcesFolderAbsolute "${CMAKE_CURRENT_LIST_DIR}/${AddOnSourcesFolder}" ABSOLUTE)
get_filename_component (AddOnResourcesFolderAbsolute "${CMAKE_CURRENT_LIST_DIR}/${AddOnResourcesFolder}" ABSOLUTE)
get_filename_component (APIDevKitToolsFolderAbsolute "${AC_API_DEVKIT_DIR}/Support/Tools" ABSOLUTE)
if (WIN32)
	add_custom_command (
		OUTPUT "${ResourceObjectsDir}/AddOnResources.stamp"
		DEPENDS ${AddOnResourceFiles} ${AddOnImageFiles}
		COMMENT "Compiling resources..."
		COMMAND ${CMAKE_COMMAND} -E make_directory "${ResourceObjectsDir}"
		COMMAND ${Python3_EXECUTABLE} "${APIDevKitToolsFolderAbsolute}/CompileResources.py" "${AC_ADDON_LANGUAGE}" "${AC_API_DEVKIT_DIR}" "${AddOnSourcesFolderAbsolute}" "${AddOnResourcesFolderAbsolute}" "${ResourceObjectsDir}" "${ResourceObjectsDir}/${AC_ADDON_NAME}.res"
		COMMAND ${CMAKE_COMMAND} -E touch "${ResourceObjectsDir}/AddOnResources.stamp"
	)
else ()
	add_custom_command (
		OUTPUT "${ResourceObjectsDir}/AddOnResources.stamp"
		DEPENDS ${AddOnResourceFiles} ${AddOnImageFiles}
		COMMENT "Compiling resources..."
		COMMAND ${CMAKE_COMMAND} -E make_directory "${ResourceObjectsDir}"
		COMMAND ${Python3_EXECUTABLE} "${APIDevKitToolsFolderAbsolute}/CompileResources.py" "${AC_ADDON_LANGUAGE}" "${AC_API_DEVKIT_DIR}" "${AddOnSourcesFolderAbsolute}" "${AddOnResourcesFolderAbsolute}" "${ResourceObjectsDir}" "${CMAKE_BINARY_DIR}/$<CONFIG>/${AC_ADDON_NAME}.bundle/Contents/Resources"
		COMMAND ${CMAKE_COMMAND} -E copy "${AC_API_DEVKIT_DIR}/Support/Inc/PkgInfo" "${CMAKE_BINARY_DIR}/$<CONFIG>/${AC_ADDON_NAME}.bundle/Contents/PkgInfo"
		COMMAND ${CMAKE_COMMAND} -E touch "${ResourceObjectsDir}/AddOnResources.stamp"
	)
endif ()

# AddOn

file (GLOB AddOnHeaderFiles
	${AddOnSourcesFolder}/*.h
	${AddOnSourcesFolder}/*.hpp
)
file (GLOB AddOnSourceFiles
	${AddOnSourcesFolder}/*.c
	${AddOnSourcesFolder}/*.cpp
)
file (GLOB AllCFiles
	${AddOnSourcesFolder}/*.c
)
set_source_files_properties(${AllCFiles} PROPERTIES LANGUAGE CXX)
set (
	AddOnFiles
	${AddOnHeaderFiles}
	${AddOnSourceFiles}
)
source_group ("Sources" FILES ${AddOnFiles})
if (WIN32)
	add_library (AddOn SHARED ${AddOnFiles})
else ()
	add_library (AddOn MODULE ${AddOnFiles})
endif ()

set_target_properties (AddOn PROPERTIES OUTPUT_NAME ${AC_ADDON_NAME})
if (WIN32)
	set_target_properties (AddOn PROPERTIES SUFFIX ".apx")
else ()
	set_target_properties (AddOn PROPERTIES BUNDLE TRUE)
	set_target_properties (AddOn PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/${AddOnResourcesFolder}/RFIX.mac/Info.plist")
	set_target_properties (AddOn PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
	if (CMAKE_GENERATOR STREQUAL Xcode)
		set_target_properties (AddOn PROPERTIES
			XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
			XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.hildgyorgy.dropview.glbexporter"
		)
	endif ()
endif ()

if (WIN32)
	target_link_options (AddOn PUBLIC "${ResourceObjectsDir}/${AC_ADDON_NAME}.res")
	target_link_options (AddOn PUBLIC /export:GetExportedFuncAddrs,@1 /export:SetImportedFuncAddrs,@2)
endif ()

target_include_directories (AddOn PUBLIC
	${AddOnSourcesFolder}
	${AC_API_DEVKIT_DIR}/Support/Inc
)
if (WIN32)
	target_link_libraries (AddOn
		"${AC_API_DEVKIT_DIR}/Support/Lib/ACAP_STAT.lib"
	)
else ()
	find_library (CocoaFramework Cocoa)
	target_link_libraries (AddOn
		"${AC_API_DEVKIT_DIR}/Support/Lib/libACAP_STAT.a"
		${CocoaFramework}
	)
endif ()

SetCompilerOptions (AddOn)
target_compile_definitions (AddOn PRIVATE DROPVIEW_VERSION="${DROPVIEW_VERSION}")
add_dependencies (AddOn AddOnResources)

get_filename_component (APIDevKitModulesDir "${AC_API_DEVKIT_DIR}/Support/Modules" ABSOLUTE)

file (GLOB GSIncludeFolders
	${APIDevKitModulesDir}/*
)
foreach (includeFolder ${GSIncludeFolders})
	AddGSModuleIncludeFolder (AddOn ${includeFolder})
endforeach ()

if (WIN32)
	set (APIDevKitLinkLibDir "${APIDevKitModulesDir}")
else ()
	get_filename_component (APIDevKitLinkLibDir "${AC_API_DEVKIT_DIR}/Support/Frameworks" ABSOLUTE)
endif ()

file (GLOB GSLinkLibFolders
	${APIDevKitLinkLibDir}/*
)
foreach (linkLibFolder ${GSLinkLibFolders})
	if (WIN32 OR linkLibFolder MATCHES "\\.(framework|dylib)$")
		AddGSModuleLinkLibrary (AddOn ${linkLibFolder})
	endif ()
endforeach ()
get_filename_component(new_hpp "${APIDevKitModulesDir}/GSRoot/GSNew.hpp" REALPATH)
get_filename_component(malloc_hpp "${APIDevKitModulesDir}/GSRoot/GSMalloc.hpp" REALPATH)
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(
        AddOn PRIVATE
        "SHELL:/FI \"${new_hpp}\""
        "SHELL:/FI \"${malloc_hpp}\""
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang\$")
    target_compile_options(
        AddOn PRIVATE
        "SHELL:-include \"${new_hpp}\""
        "SHELL:-include \"${malloc_hpp}\""
    )
else()
    message(FATAL_ERROR "Unknown compiler ID.")
endif()

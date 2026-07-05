cmake_minimum_required (VERSION 3.16)

function (SetCompilerOptions target acVersion)
	if (${acVersion} LESS 27)
        target_compile_features (${target} PUBLIC cxx_std_14)
    elseif (${acVersion} LESS 29)
        target_compile_features (${target} PUBLIC cxx_std_17)
    else ()
        target_compile_features (${target} PUBLIC cxx_std_20)
    endif ()

	# Use a compile DEFINITION (not a -DDEBUG compile option): the Xcode
	# generator routes compile options into OTHER_CPLUSPLUSFLAGS only, so a
	# -DDEBUG option would never reach C files (e.g. BIMrender.c, whose
	# #ifdef DEBUG selects the modal dialog). A definition goes into
	# GCC_PREPROCESSOR_DEFINITIONS for both C and C++.
	target_compile_definitions (${target} PUBLIC "$<$<CONFIG:Debug>:DEBUG>")
	if (WIN32)
		 target_compile_options (${target} PUBLIC /W4 /WX
            /Zc:wchar_t-
            /EHsc
            -D_CRT_SECURE_NO_WARNINGS
        )
	else ()
		target_compile_options (${target} PUBLIC -Wall -Wextra -Werror
            -fvisibility=hidden
            -Wno-multichar
            -Wno-ctor-dtor-privacy
            -Wno-invalid-offsetof
            -Wno-ignored-qualifiers
            -Wno-reorder
            -Wno-overloaded-virtual
            -Wno-unused-parameter
            -Wno-unused-value
            -Wno-unused-private-field
            -Wno-deprecated
            -Wno-unknown-pragmas
            -Wno-missing-braces
            -Wno-missing-field-initializers
            -Wno-non-c-typedef-for-linkage
            -Wno-uninitialized-const-reference
            -Wno-shorten-64-to-32
            -Wno-sign-compare
            -Wno-switch
		)

		if (${acVersion} LESS_EQUAL "24")
            target_compile_options (${target} PUBLIC -Wno-non-c-typedef-for-linkage)
        endif ()
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

# User-configurable variables
set (API_DEVKIT_DIR $ENV{AC_API_DEVKIT_DIR})
set (AC_API_DEVKIT_DIR ${API_DEVKIT_DIR} CACHE PATH "API DevKit directory.")
set (AC_ADDON_NAME "${CMAKE_CURRENT_FOLDER_NAME}" CACHE STRING "Add-On name.")
set (AC_ADDON_LANGUAGE "INT" CACHE STRING "Add-On language code.")

# Check if the API DevKit directory exists and detect Archicad version
message (STATUS "APIDevKit directory: ${AC_API_DEVKIT_DIR}")
set (ACAPINC_FILE_LOCATION ${AC_API_DEVKIT_DIR}/Support/Inc/ACAPinc.h)
if (EXISTS ${ACAPINC_FILE_LOCATION})
	file (READ ${ACAPINC_FILE_LOCATION} ACAPIncContent)
	string (REGEX MATCHALL "#define[ \t]+ServerMainVers_([0-9][0-9])" VersionList ${ACAPIncContent})
	set (ARCHICAD_VERSION ${CMAKE_MATCH_1})
	message (STATUS "Archicad Version: ${ARCHICAD_VERSION}")
else ()
	message (FATAL_ERROR "Failed to detect Archicad version, please check the value of the AC_API_DEVKIT_DIR variable.")
endif ()

# General settings
set (CMAKE_SUPPRESS_REGENERATION 1)
set (CMAKE_CONFIGURATION_TYPES Debug;Release;RelWithDebInfo)
if (WIN32)
	if ("${ARCHICAD_VERSION}" STRLESS_EQUAL "28")
		set (CMAKE_GENERATOR_TOOLSET v142)
		message (STATUS "Toolset: v142 for Archicad Version: ${ARCHICAD_VERSION}")
	else ()
		set (CMAKE_GENERATOR_TOOLSET v143)
		message (STATUS "Toolset: v143 for Archicad Version: ${ARCHICAD_VERSION}")
	endif ()
	set (CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
else ()
	set (CMAKE_OSX_DEPLOYMENT_TARGET 10.15)
	# Build a universal (arm64 + x86_64) bundle for every configuration.
	# Archicad on Apple Silicon loads add-ons natively as arm64, so a single-arch
	# bundle (e.g. a Debug build, which Xcode strips to the active arch via
	# ONLY_ACTIVE_ARCH=YES) is rejected and shown as "disabled" in the Add-On
	# Manager. An explicit arch list plus ONLY_ACTIVE_ARCH=NO keeps Debug
	# universal too; Xcode then also ad-hoc signs the arm64 slice.
	set (CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
	if (CMAKE_GENERATOR STREQUAL Xcode)
		set (CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO")
	endif ()
endif ()

if (WIN32)
	add_definitions (-DUNICODE -D_UNICODE -D_ITERATOR_DEBUG_LEVEL=0)
else ()
	add_definitions (-Dmacintosh=1)
endif ()
add_definitions (-DACExtension)

project (${AC_ADDON_NAME})

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
		${AddOnResourcesFolder}/RFIX/*.grc
		${AddOnResourcesFolder}/RFIX.win/*.rc2
	)
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
		COMMAND python3 "${APIDevKitToolsFolderAbsolute}/CompileResources.py" "${AC_ADDON_LANGUAGE}" "${AC_API_DEVKIT_DIR}" "${AddOnSourcesFolderAbsolute}" "${AddOnResourcesFolderAbsolute}" "${ResourceObjectsDir}" "${ResourceObjectsDir}/${AC_ADDON_NAME}.res"
		COMMAND ${CMAKE_COMMAND} -E touch "${ResourceObjectsDir}/AddOnResources.stamp"
	)
else ()
	add_custom_command (
		OUTPUT "${ResourceObjectsDir}/AddOnResources.stamp"
		DEPENDS ${AddOnResourceFiles} ${AddOnImageFiles}
		COMMENT "Compiling resources..."
		COMMAND ${CMAKE_COMMAND} -E make_directory "${ResourceObjectsDir}"
		COMMAND python3 "${APIDevKitToolsFolderAbsolute}/CompileResources.py" "${AC_ADDON_LANGUAGE}" "${AC_API_DEVKIT_DIR}" "${AddOnSourcesFolderAbsolute}" "${AddOnResourcesFolderAbsolute}" "${ResourceObjectsDir}" "${CMAKE_BINARY_DIR}/$<CONFIG>/${AC_ADDON_NAME}.bundle/Contents/Resources"
		COMMAND ${CMAKE_COMMAND} -E copy "${AC_API_DEVKIT_DIR}/Support/Inc/PkgInfo" "${CMAKE_BINARY_DIR}/$<CONFIG>/${AC_ADDON_NAME}.bundle/Contents/PkgInfo"
		COMMAND ${CMAKE_COMMAND} -E touch "${ResourceObjectsDir}/AddOnResources.stamp"
	)
endif ()

# AddOn

file (GLOB AddOnHeaderFiles
	${AddOnSourcesFolder}/*.h
	${AddOnSourcesFolder}/*.hpp
	${AddOnSourcesFolder}/*/*.h
	${AddOnSourcesFolder}/*/*.hpp
	${AddOnSourcesFolder}/*/*/*.h
	${AddOnSourcesFolder}/*/*/*.hpp
)
file (GLOB AddOnSourceFiles
	${AddOnSourcesFolder}/*.c
	${AddOnSourcesFolder}/*.cpp
	${AddOnSourcesFolder}/*/*.c
	${AddOnSourcesFolder}/*/*.cpp
	${AddOnSourcesFolder}/*/*/*.c
	${AddOnSourcesFolder}/*/*/*.cpp
)
file (GLOB AllCFiles
	${AddOnSourcesFolder}/*.c
	${AddOnSourcesFolder}/*/*.c
	${AddOnSourcesFolder}/*/*/*.c
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

SetCompilerOptions (AddOn ${ARCHICAD_VERSION})
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

list(FILTER GSLinkLibFolders EXCLUDE REGEX ".*/\\.DS_Store$")

foreach (linkLibFolder ${GSLinkLibFolders})
	AddGSModuleLinkLibrary (AddOn ${linkLibFolder})
endforeach ()

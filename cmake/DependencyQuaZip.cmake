# Find QuaZip

find_package(QuaZip-Qt${QT_VERSION} 1.0 QUIET)

if(QuaZip-Qt${QT_VERSION}_FOUND)
    if(TARGET QuaZip::QuaZip)
        get_target_property(_quazip_link_libs QuaZip::QuaZip INTERFACE_LINK_LIBRARIES)
        if(_quazip_link_libs)
            string(REGEX REPLACE "/Applications/Xcode[^;]*libbz2\\.tbd" "-lbz2" _quazip_link_libs "${_quazip_link_libs}")
            set_target_properties(QuaZip::QuaZip PROPERTIES INTERFACE_LINK_LIBRARIES "${_quazip_link_libs}")
        endif()
    endif()
    target_link_libraries(openboard
        QuaZip::QuaZip
    )
else()
    # Try to find the package using pkg-config with several names
    find_package(PkgConfig REQUIRED)
    
    pkg_check_modules(QuaZip QUIET IMPORTED_TARGET quazip-qt${QT_VERSION})
	
    if(NOT QuaZip_FOUND)
        pkg_check_modules(QuaZip QUIET IMPORTED_TARGET quazip1-qt${QT_VERSION})
    endif()
	
    if(NOT QuaZip_FOUND)
        pkg_check_modules(QuaZip QUIET IMPORTED_TARGET libquazip${QT_VERSION}-1)
    endif()

    if(NOT QuaZip_FOUND)
        pkg_check_modules(QuaZip QUIET IMPORTED_TARGET quazip${QT_VERSION})
    endif()

    if(QuaZip_FOUND)
        message(STATUS "Found QuaZip version " ${QuaZip_VERSION})
        target_link_libraries(${PROJECT_NAME} 
            PkgConfig::QuaZip
        )
    else()
        # Just assume default directories of QuaZip < 1.0
        message(STATUS "QuaZip not found, assuming default include directory " /usr/include/quazip${QT_VERSION})
        target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
            /usr/include/quazip${QT_VERSION}
        )
        target_link_libraries(${PROJECT_NAME}
            quazip${QT_VERSION}
        )
    endif()
endif()
 

# Installation and packages: Linux .deb (CPack), macOS app bundle, Windows folder (+ Inno Setup in CI).
# ffmpeg is a dependency of the .deb; the Windows installer ships ffmpeg.exe/ffprobe.exe next to the program;
# on macOS it comes from Homebrew (see the README).
include(GNUInstallDirs)

if(LIBREDROP_GUI)
  set_target_properties(libredrop-gui PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.ohlincha.libredrop
    MACOSX_BUNDLE_BUNDLE_NAME LibreDrop
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE LibreDrop.icns)
  if(APPLE)
    configure_file(resources/icon.icns ${CMAKE_CURRENT_BINARY_DIR}/LibreDrop.icns COPYONLY)
    target_sources(libredrop-gui PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/LibreDrop.icns)
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/LibreDrop.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  elseif(WIN32)
    set(ICON_FILE ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico)
    configure_file(packaging/windows/libredrop.rc ${CMAKE_CURRENT_BINARY_DIR}/libredrop.rc @ONLY)
    target_sources(libredrop-gui PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/libredrop.rc)
  endif()
endif()

if(WIN32)
  install(TARGETS libredrop-cli RUNTIME DESTINATION .)
  if(LIBREDROP_GUI)
    install(TARGETS libredrop-gui RUNTIME DESTINATION .)
  endif()
  install(FILES LICENSE DESTINATION . RENAME LICENSE.txt)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
  set(CMAKE_INSTALL_UCRT_LIBRARIES OFF)
  include(InstallRequiredSystemLibraries)
elseif(APPLE)
  if(LIBREDROP_GUI)
    install(TARGETS libredrop-gui BUNDLE DESTINATION .)
    install(TARGETS libredrop-cli RUNTIME DESTINATION LibreDrop.app/Contents/MacOS)
  else()
    install(TARGETS libredrop-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
else()
  install(TARGETS libredrop-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  if(LIBREDROP_GUI)
    install(TARGETS libredrop-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/linux/io.github.ohlincha.libredrop.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME io.github.ohlincha.libredrop.png)
    install(FILES resources/icon.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME io.github.ohlincha.libredrop.svg)
  endif()
  install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/libredrop RENAME copyright)

  set(CPACK_GENERATOR DEB)
  set(CPACK_PACKAGE_NAME libredrop)
  set(CPACK_PACKAGE_VENDOR "LibreDrop contributors")
  set(CPACK_PACKAGE_CONTACT "LibreDrop contributors <https://github.com/ohlincha/libredrop/issues>")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Contact angle and drop shape analysis for goniometer videos and images")
  set(CPACK_PACKAGE_DESCRIPTION "Finds the surface and every drop in backlit side views automatically and measures\n"
      " contact angles (Young-Laplace, tangent, circle, ellipse, height-width), base width, height and volume,\n"
      " frame by frame through whole videos.")
  string(REPLACE ";" "" CPACK_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/ohlincha/libredrop")
  set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
  set(CPACK_STRIP_FILES ON)
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_SECTION science)
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "ffmpeg")
  set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland")
  include(CPack)
endif()

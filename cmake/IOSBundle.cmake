# iOS app bundle for the nsmbu target (ios/build_ios.sh drives the build and packs the .ipa)
set(NSMBU_IOS_BUNDLE_ID "org.nsmbrecomp.nsmbu" CACHE STRING "iOS bundle identifier")
set(NSMBU_IOS_VERSION "1.0" CACHE STRING "CFBundleShortVersionString of the iOS app")
set(NSMBU_IOS_BUILD "1" CACHE STRING "CFBundleVersion of the iOS app")
set(NSMBU_IOS_MIN "${CMAKE_OSX_DEPLOYMENT_TARGET}")
if(NOT NSMBU_IOS_MIN)
  set(NSMBU_IOS_MIN 17.0)
endif()
set_target_properties(nsmbu PROPERTIES
  OUTPUT_NAME NSMBU
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/ios/Info.plist.in"
  MACOSX_BUNDLE_GUI_IDENTIFIER "${NSMBU_IOS_BUNDLE_ID}"
  MACOSX_BUNDLE_BUNDLE_NAME "NSMBU"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${NSMBU_IOS_VERSION}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${NSMBU_IOS_BUILD}"
  XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_SOURCE_DIR}/ios/nsmbu.entitlements")
target_compile_definitions(nsmbu PRIVATE NSMBU_IOS=1)

# app icon and other bundle resources
file(GLOB NSMBU_IOS_RESOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/ios/resources/*")
if(NSMBU_IOS_RESOURCES)
  add_custom_command(TARGET nsmbu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NSMBU_IOS_RESOURCES} "$<TARGET_BUNDLE_CONTENT_DIR:nsmbu>"
    VERBATIM)
endif()

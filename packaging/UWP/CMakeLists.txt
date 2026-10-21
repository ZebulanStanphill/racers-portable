# Xbox (UWP) packaging, modelled on isle-portable's packaging/UWP. With the
# WindowsStore system name, the Visual Studio generator turns the legoracers
# target into a UWP app project: it picks up Package.appxmanifest as the app
# manifest and RacersPortable.pfx as the signing key (by file extension), and
# MSBuild writes a signed .msix to AppPackages/ when it builds the target.
#
# RacersPortable.pfx is a self-signed, password-less certificate for
# "CN=Racers Portable" (the manifest's Publisher). Keeping it fixed, with the
# rising package version below, lets a new build install as an update over an
# older one, which keeps the app's saves.

# The tile and splash images go at the package root, where the manifest's image
# references point. Source file properties are directory-scoped, so they are set
# in the legoracers target's directory.
file(GLOB UWP_ASSET_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.png")
set_source_files_properties(${UWP_ASSET_FILES}
  TARGET_DIRECTORY legoracers
  PROPERTIES VS_DEPLOYMENT_CONTENT 1
)

# Windows only installs a package over an installed one with different contents if
# its version is higher, so each CI build is numbered after its workflow run:
# <project major>.<project minor>.<run number>.0. Local builds are numbered 0.
if (DEFINED ENV{GITHUB_RUN_NUMBER})
  set(UWP_BUILD_NUMBER "$ENV{GITHUB_RUN_NUMBER}")
else()
  set(UWP_BUILD_NUMBER 0)
endif()
set(UWP_PACKAGE_VERSION "${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}.${UWP_BUILD_NUMBER}.0")
configure_file(Package.appxmanifest "${CMAKE_CURRENT_BINARY_DIR}/Package.appxmanifest" @ONLY)

target_sources(legoracers PRIVATE
  ${UWP_ASSET_FILES}
  "${CMAKE_CURRENT_BINARY_DIR}/Package.appxmanifest"
  "${CMAKE_CURRENT_SOURCE_DIR}/RacersPortable.pfx"
)

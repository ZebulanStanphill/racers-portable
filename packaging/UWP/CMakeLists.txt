# Xbox (UWP) packaging, modelled on isle-portable's packaging/UWP. With the
# WindowsStore system name, the Visual Studio generator turns the legoracers
# target into a UWP app project: it picks up Package.appxmanifest as the app
# manifest and RacersPortable.pfx as the signing key (by file extension), and
# MSBuild writes a signed .msix to AppPackages/ when it builds the target.
#
# RacersPortable.pfx is a self-signed, password-less certificate for
# "CN=Racers Portable" (the manifest's Publisher). Keeping it fixed means a new
# build installs as an update over an older one, which keeps the app's saves.

# The tile and splash images go at the package root, where the manifest's image
# references point. Source file properties are directory-scoped, so they are set
# in the legoracers target's directory.
file(GLOB UWP_ASSET_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.png")
set_source_files_properties(${UWP_ASSET_FILES}
  TARGET_DIRECTORY legoracers
  PROPERTIES VS_DEPLOYMENT_CONTENT 1
)

target_sources(legoracers PRIVATE
  ${UWP_ASSET_FILES}
  "${CMAKE_CURRENT_SOURCE_DIR}/Package.appxmanifest"
  "${CMAKE_CURRENT_SOURCE_DIR}/RacersPortable.pfx"
)

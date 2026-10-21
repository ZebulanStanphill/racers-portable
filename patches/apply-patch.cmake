# Applies PATCH_FILE to the git checkout in the current directory, for FetchContent's
# PATCH_COMMAND. The patch step can run again on an already patched checkout (for
# example after a reconfigure), so a patch that is already applied is skipped.
#
# Usage: cmake -DGIT_EXECUTABLE=<git> -DPATCH_FILE=<file> -P apply-patch.cmake

execute_process(
  COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${PATCH_FILE}"
  RESULT_VARIABLE already_applied
  OUTPUT_QUIET ERROR_QUIET
)
if (already_applied EQUAL 0)
  message(STATUS "Already applied: ${PATCH_FILE}")
  return()
endif()

execute_process(
  COMMAND "${GIT_EXECUTABLE}" apply "${PATCH_FILE}"
  RESULT_VARIABLE result
)
if (NOT result EQUAL 0)
  message(FATAL_ERROR "Could not apply ${PATCH_FILE}")
endif()
message(STATUS "Applied: ${PATCH_FILE}")

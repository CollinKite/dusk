include_guard(GLOBAL)

# Keep the tested Aurora changes reproducible without a private submodule ref.
# Never reset a developer's dependency checkout to make a patch fit.
find_package(Git REQUIRED)
set(_aurora_source "${CMAKE_CURRENT_LIST_DIR}/../extern/aurora")
set(_aurora_patch "${CMAKE_CURRENT_LIST_DIR}/patches/aurora-duo.patch")
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${_aurora_patch}"
    WORKING_DIRECTORY "${_aurora_source}"
    RESULT_VARIABLE _already_applied OUTPUT_QUIET ERROR_QUIET)
if (_already_applied EQUAL 0)
    return()
endif ()
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check "${_aurora_patch}"
    WORKING_DIRECTORY "${_aurora_source}"
    RESULT_VARIABLE _patch_check OUTPUT_QUIET ERROR_VARIABLE _patch_error)
if (NOT _patch_check EQUAL 0)
    message(FATAL_ERROR "Aurora Duo patch does not match the dependency checkout. "
        "Preserve any local edits and use the pinned submodule revision in a fresh checkout.\n${_patch_error}")
endif ()
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply "${_aurora_patch}"
    WORKING_DIRECTORY "${_aurora_source}" COMMAND_ERROR_IS_FATAL ANY)
message(STATUS "Applied Aurora Duo integration patch")

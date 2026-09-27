if(NOT DEFINED ROSE_SOURCE_DIR)
    message(FATAL_ERROR "ROSE_SOURCE_DIR was not provided.")
endif()

if(NOT DEFINED ROSE_OUTPUT_FILE)
    message(FATAL_ERROR "ROSE_OUTPUT_FILE was not provided.")
endif()

if(NOT DEFINED ROSE_PROJECT_VERSION)
    set(ROSE_PROJECT_VERSION "unknown")
endif()


# ---------------------------------------------------------------------------
# Collect Git identity when the source tree is a Git worktree.
# ---------------------------------------------------------------------------

set(rose_git_available 0)
set(rose_git_revision "source-snapshot")
set(rose_git_branch "unknown")
set(rose_git_dirty 0)

find_program(
    rose_git_executable
    NAMES git
)

if(rose_git_executable)
    execute_process(
        COMMAND
            "${rose_git_executable}"
            -C "${ROSE_SOURCE_DIR}"
            rev-parse
            --is-inside-work-tree

        RESULT_VARIABLE
            rose_git_worktree_result

        OUTPUT_VARIABLE
            rose_git_worktree_output

        ERROR_QUIET

        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(
        rose_git_worktree_result EQUAL 0
        AND rose_git_worktree_output STREQUAL "true"
    )
        set(rose_git_available 1)

        execute_process(
            COMMAND
                "${rose_git_executable}"
                -C "${ROSE_SOURCE_DIR}"
                rev-parse
                --short=12
                HEAD

            RESULT_VARIABLE
                rose_revision_result

            OUTPUT_VARIABLE
                rose_revision_output

            ERROR_QUIET

            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(rose_revision_result EQUAL 0 AND NOT rose_revision_output STREQUAL "")
            set(rose_git_revision "${rose_revision_output}")
        endif()

        execute_process(
            COMMAND
                "${rose_git_executable}"
                -C "${ROSE_SOURCE_DIR}"
                rev-parse
                --abbrev-ref
                HEAD

            RESULT_VARIABLE
                rose_branch_result

            OUTPUT_VARIABLE
                rose_branch_output

            ERROR_QUIET

            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(rose_branch_result EQUAL 0 AND NOT rose_branch_output STREQUAL "")
            set(rose_git_branch "${rose_branch_output}")
        endif()

        # Include tracked modifications and untracked source files.
        execute_process(
            COMMAND
                "${rose_git_executable}"
                -C "${ROSE_SOURCE_DIR}"
                status
                --porcelain
                --untracked-files=normal

            RESULT_VARIABLE
                rose_status_result

            OUTPUT_VARIABLE
                rose_status_output

            ERROR_QUIET

            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(
            rose_status_result EQUAL 0
            AND NOT rose_status_output STREQUAL ""
        )
            set(rose_git_dirty 1)
        endif()
    endif()
endif()


# ---------------------------------------------------------------------------
# Escape strings for C preprocessor string literals.
# ---------------------------------------------------------------------------

function(rose_escape_cpp_string input output_variable)
    set(value "${input}")

    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")

    set(
        "${output_variable}"
        "${value}"
        PARENT_SCOPE
    )
endfunction()

rose_escape_cpp_string(
    "${ROSE_PROJECT_VERSION}"
    rose_project_version_escaped
)

rose_escape_cpp_string(
    "${rose_git_revision}"
    rose_git_revision_escaped
)

rose_escape_cpp_string(
    "${rose_git_branch}"
    rose_git_branch_escaped
)


# ---------------------------------------------------------------------------
# Update only when content changes.
#
# The generation target runs every Rose build so Git state stays fresh.
# copy_if_different prevents needless recompilation when identity is unchanged.
# ---------------------------------------------------------------------------

get_filename_component(
    rose_output_directory
    "${ROSE_OUTPUT_FILE}"
    DIRECTORY
)

file(
    MAKE_DIRECTORY
    "${rose_output_directory}"
)

string(
    CONCAT
    rose_generated_content
    "#pragma once\n\n"
    "#define ROSE_BUILD_PROJECT_VERSION \"${rose_project_version_escaped}\"\n"
    "#define ROSE_BUILD_GIT_REVISION \"${rose_git_revision_escaped}\"\n"
    "#define ROSE_BUILD_GIT_BRANCH \"${rose_git_branch_escaped}\"\n"
    "#define ROSE_BUILD_GIT_AVAILABLE ${rose_git_available}\n"
    "#define ROSE_BUILD_GIT_DIRTY ${rose_git_dirty}\n"
)

set(
    rose_temporary_file
    "${ROSE_OUTPUT_FILE}.tmp"
)

file(
    WRITE
    "${rose_temporary_file}"
    "${rose_generated_content}"
)

execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        -E
        copy_if_different
        "${rose_temporary_file}"
        "${ROSE_OUTPUT_FILE}"

    RESULT_VARIABLE
        rose_copy_result
)

file(
    REMOVE
    "${rose_temporary_file}"
)

if(NOT rose_copy_result EQUAL 0)
    message(FATAL_ERROR "Failed to update Rose build identity header.")
endif()

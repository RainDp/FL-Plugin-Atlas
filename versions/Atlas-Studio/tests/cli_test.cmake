cmake_minimum_required(VERSION 3.16)

if(NOT EXISTS "${PROGRAM}" OR "${TEST_DIR}" STREQUAL "")
    message(FATAL_ERROR "Specify the built atlas executable and a test directory.")
endif()

get_filename_component(test_parent "${TEST_DIR}" ABSOLUTE)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef run_id)
set(fixture "${test_parent}/atlas-cli-${run_id}")
if(EXISTS "${fixture}")
    message(FATAL_ERROR "Test directory already exists: ${fixture}")
endif()
set(library "${fixture}/Plugin database")
set(database "${fixture}/catalog.sqlite3")
set(equalizer "Installed/Effects/VST3/Neutral Filter.fst")
set(reverb   "Installed/Effects/VST3/Room Model.fst")
set(synth    "Installed/Generators/VST3/Amber Voice.fst")
file(MAKE_DIRECTORY "${library}/Effects" "${library}/Generators")

function(preset relative_path vendor category type)
    get_filename_component(dir "${relative_path}" DIRECTORY)
    get_filename_component(name "${relative_path}" NAME_WE)
    file(MAKE_DIRECTORY "${library}/${dir}")
    file(WRITE "${library}/${relative_path}" "fixture preset: ${name}")
    file(WRITE "${library}/${dir}/${name}.nfo"
        "fileversion=2\nps_name=${name}\nps_files=1\n"
        "ps_file_name_0=${name}\nps_file_type_0=${type}\n"
        "ps_file_vendorname_0=${vendor}\nps_file_category_0=${category}\n")
endfunction()

preset("${equalizer}" "Studio One Vendor" "Fx|EQ" 1)
preset("${reverb}" "Studio Two Vendor" "Fx|Reverb" 1)
preset("${synth}" "Studio One Vendor" "Instrument|Synth" 2)

function(run_atlas command)
    execute_process(
        COMMAND "${PROGRAM}" "${command}" --library "${library}" --db "${database}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error ENCODING UTF-8
    )
    if(NOT "${status}" STREQUAL "0")
        message(FATAL_ERROR "atlas ${command} ${ARGN} (${status}): ${error}\nFixture: ${fixture}")
    endif()
    set(last_output "${output}" PARENT_SCOPE)
endfunction()

# Match preset paths so that a heading or a repeated filter value cannot count as a result.
function(expect_entry path visible)
    string(REPLACE "\\" "/" normalized "${last_output}")
    string(FIND "${normalized}" "${path}" position)
    if((visible AND position LESS 0) OR (NOT visible AND position GREATER_EQUAL 0))
        message(FATAL_ERROR "Expected visibility ${visible} for ${path}:\n${last_output}")
    endif()
endfunction()

execute_process(
    COMMAND "${PROGRAM}" invalid-command --library "${library}" --db "${database}"
    RESULT_VARIABLE invalid_status OUTPUT_QUIET ERROR_QUIET
)
if("${invalid_status}" STREQUAL "0" OR EXISTS "${database}")
    message(FATAL_ERROR "An invalid command succeeded or created a database.")
endif()

run_atlas(scan)
run_atlas(list --vendor "Studio One Vendor")
expect_entry("${equalizer}" TRUE)
expect_entry("${synth}" TRUE)
expect_entry("${reverb}" FALSE)

run_atlas(list --tag "均衡")
expect_entry("${equalizer}" TRUE)
expect_entry("${synth}" FALSE)
run_atlas(list --kind Generators)
expect_entry("${synth}" TRUE)
expect_entry("${equalizer}" FALSE)

run_atlas(tag --entry "${equalizer}" --entry "${reverb}" --add "母带")
run_atlas(list --vendor "Studio One Vendor" --tag "均衡" --tag "母带")
expect_entry("${equalizer}" TRUE)
expect_entry("${synth}" FALSE)
expect_entry("${reverb}" FALSE)
run_atlas(list --tag "母带" --tag "均衡")
expect_entry("${equalizer}" TRUE)
expect_entry("${reverb}" FALSE)

run_atlas(tag --entry "${equalizer}" --remove "均衡")
run_atlas(alias --from "Studio One Vendor" --to "统一厂商")
run_atlas(vendor --entry "${equalizer}" --name "个人厂商")
run_atlas(scan)
run_atlas(list --vendor "个人厂商" --tag "母带")
expect_entry("${equalizer}" TRUE)
expect_entry("${reverb}" FALSE)
run_atlas(list --tag "均衡")
expect_entry("${equalizer}" FALSE)
run_atlas(list --tag "母带")
expect_entry("${reverb}" TRUE)
run_atlas(list --vendor "统一厂商")
expect_entry("${synth}" TRUE)
expect_entry("${equalizer}" FALSE)

# fixture was allocated exclusively for this run as a new child of test_parent.
file(REMOVE_RECURSE "${fixture}")

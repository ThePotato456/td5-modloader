if(NOT DEFINED FIXTURE OR NOT DEFINED PROXY OR NOT DEFINED RUNTIME OR NOT DEFINED STAGE)
    message(FATAL_ERROR "Fixture test paths were not provided.")
endif()

file(REMOVE_RECURSE "${STAGE}")
file(MAKE_DIRECTORY "${STAGE}")
file(COPY_FILE "${FIXTURE}" "${STAGE}/BTD5-Win.exe")
file(SHA256 "${FIXTURE}" FIXTURE_HASH_BEFORE)

execute_process(
    COMMAND "${STAGE}/BTD5-Win.exe"
    WORKING_DIRECTORY "${STAGE}"
    RESULT_VARIABLE VANILLA_RESULT
)
if(NOT VANILLA_RESULT EQUAL 0)
    message(FATAL_ERROR "Vanilla fixture failed with ${VANILLA_RESULT}.")
endif()

file(COPY "${PROXY}" "${RUNTIME}" DESTINATION "${STAGE}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "BTD5ML_DATA_ROOT=${STAGE}/data" "${STAGE}/BTD5-Win.exe"
    WORKING_DIRECTORY "${STAGE}"
    RESULT_VARIABLE PROXY_RESULT
)
if(NOT PROXY_RESULT EQUAL 0)
    message(FATAL_ERROR "Proxied fixture failed with ${PROXY_RESULT}.")
endif()

set(RUNTIME_LOG "${STAGE}/data/logs/runtime.jsonl")
if(NOT EXISTS "${RUNTIME_LOG}")
    message(FATAL_ERROR "The runtime did not create its structured log.")
endif()
file(READ "${RUNTIME_LOG}" RUNTIME_LOG_CONTENT)
if(NOT RUNTIME_LOG_CONTENT MATCHES "compatibility_check_pending_phase_3")
    message(FATAL_ERROR "The runtime did not reach the compatibility-check state.")
endif()

file(SHA256 "${STAGE}/BTD5-Win.exe" FIXTURE_HASH_AFTER)
if(NOT FIXTURE_HASH_BEFORE STREQUAL FIXTURE_HASH_AFTER)
    message(FATAL_ERROR "The fixture executable changed during proxy installation.")
endif()

file(REMOVE "${STAGE}/wininet.dll" "${STAGE}/btd5loader_runtime.dll")
if(EXISTS "${STAGE}/wininet.dll" OR EXISTS "${STAGE}/btd5loader_runtime.dll")
    message(FATAL_ERROR "Proxy uninstall simulation left loader files behind.")
endif()

file(REMOVE "${STAGE}/btd5loader-bootstrap.log")
file(COPY "${PROXY}" DESTINATION "${STAGE}")
execute_process(
    COMMAND "${STAGE}/BTD5-Win.exe"
    WORKING_DIRECTORY "${STAGE}"
    RESULT_VARIABLE FAILURE_RESULT
)
if(NOT FAILURE_RESULT EQUAL 0)
    message(FATAL_ERROR "The proxy did not forward WinINet after runtime startup failed.")
endif()
if(NOT EXISTS "${STAGE}/btd5loader-bootstrap.log")
    message(FATAL_ERROR "A missing runtime did not produce a bootstrap diagnostic.")
endif()
file(READ "${STAGE}/btd5loader-bootstrap.log" BOOTSTRAP_LOG_CONTENT)
if(NOT BOOTSTRAP_LOG_CONTENT MATCHES "runtime_load_failed")
    message(FATAL_ERROR "The bootstrap diagnostic did not explain the runtime failure.")
endif()
file(REMOVE "${STAGE}/wininet.dll")

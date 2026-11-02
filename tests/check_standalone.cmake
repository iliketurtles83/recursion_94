file(MAKE_DIRECTORY "${CHECK_DIR}")
configure_file("${EXECUTABLE}" "${CHECK_DIR}/DATA-RUN.exe" COPYONLY)
execute_process(COMMAND "${CHECK_DIR}/DATA-RUN.exe" --hidden --startup-check
  WORKING_DIRECTORY "${CHECK_DIR}" RESULT_VARIABLE result
  OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
message("${output}${errors}")
if(NOT result EQUAL 0 OR NOT output MATCHES "resources: using embedded"
    OR NOT output MATCHES "8 interactive frames passed")
  message(FATAL_ERROR "Standalone startup failed: ${result}")
endif()

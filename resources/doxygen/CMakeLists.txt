# Enable operator IN_LIST to avoid problems with CMake version 3.12
if(POLICY CMP0057)
    cmake_policy(SET CMP0057 NEW)
endif()

find_package(Doxygen)
# Add a target to generate API documentation with Doxygen
if(DOXYGEN_FOUND)

    # We use the doxygen command of CMake instead of using the separate config file
    set(DOXYGEN_PROJECT_NAME "Storm")
    set(DOXYGEN_PROJECT_BRIEF "A Modern Probabilistic Model Checker")
    set(DOXYGEN_BRIEF_MEMBER_DESC YES)
    set(DOXYGEN_REPEAT_BRIEF YES)
    set(DOXYGEN_JAVADOC_AUTOBRIEF YES)
    set(DOXYGEN_QT_AUTOBRIEF YES)
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_SOURCE_BROWSER YES)
    set(DOXYGEN_GENERATE_TREEVIEW YES)
    set(DOXYGEN_CASE_SENSE_NAMES NO)
    set(DOXYGEN_HTML_TIMESTAMP YES)
    set(DOXYGEN_CREATE_SUBDIRS YES)
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/doc")
    doxygen_add_docs(
        doc
        "${PROJECT_SOURCE_DIR}/src"
        COMMENT "Generating API documentation with Doxygen"
    )

    # These commands can be used if the separate config files should be used
    #set(CMAKE_DOXYGEN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/doc")
    #string(REGEX REPLACE ";" " " CMAKE_DOXYGEN_INPUT_LIST "${PROJECT_SOURCE_DIR}/src")
    #configure_file("${CMAKE_CURRENT_SOURCE_DIR}/resources/doxygen/Doxyfile.in.new" "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile" @ONLY)
    #add_custom_target(doc ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile" DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile" COMMENT "Generating API documentation with Doxygen" VERBATIM)
endif(DOXYGEN_FOUND)

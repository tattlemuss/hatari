set(FRAMEWORKS_PATH "${CMAKE_SOURCE_DIR}/src/Hatari.app/Contents/Frameworks")

if (EXISTS "${FRAMEWORKS_PATH}")
    execute_process(COMMAND /bin/chmod -R u+w "${FRAMEWORKS_PATH}")
    file(REMOVE_RECURSE "${FRAMEWORKS_PATH}")
endif()


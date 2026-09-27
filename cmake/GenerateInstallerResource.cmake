file(TO_CMAKE_PATH "${INPUT}" dll)
file(TO_CMAKE_PATH "${MANIFEST}" manifest)
file(WRITE "${OUTPUT}" "100 RCDATA \"${dll}\"\n1 24 \"${manifest}\"\n")

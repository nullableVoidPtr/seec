set(SEEC_CLANG_HEADERS
  ../../include/seec/Clang/Compile.hpp
  ../../include/seec/Clang/MappedAST.hpp
  ../../include/seec/Clang/MappedFile.hpp
  ../../include/seec/Clang/MappedLocal.hpp
  ../../include/seec/Clang/MappedModule.hpp
  ../../include/seec/Clang/MappedParam.hpp
  ../../include/seec/Clang/MappedStmt.hpp
  ../../include/seec/Clang/MDNames.hpp
  ../../include/seec/Clang/Search.hpp
  ../../include/seec/Clang/SubRangeRecorder.hpp
  MappedLLVMValue.hpp
)

set(SEEC_CLANG_SOURCES
  Compile.cpp
  MappedAST.cpp
  MappedFile.cpp
  MappedLLVMValue.cpp
  MappedLocal.cpp
  MappedModule.cpp
  MappedParam.cpp
  MappedStmt.cpp
  Search.cpp
  SubRangeRecorder.cpp
)

if (${CMAKE_GENERATOR} MATCHES "Xcode")
  add_library(SeeCClang ${SEEC_CLANG_HEADERS} ${SEEC_CLANG_SOURCES})
else()
  add_library(SeeCClang ${SEEC_CLANG_SOURCES})
endif()

INSTALL(TARGETS SeeCClang
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)


set(SEEC_CLANG_MAPPED_TRACE_HEADERS
  ../../include/seec/Clang/GraphExpansion.hpp
  ../../include/seec/Clang/GraphLayout.hpp
  ../../include/seec/Clang/MappedAllocaState.hpp
  ../../include/seec/Clang/MappedFunctionState.hpp
  ../../include/seec/Clang/MappedGlobalVariable.hpp
  ../../include/seec/Clang/MappedMallocState.hpp
  ../../include/seec/Clang/MappedProcessState.hpp
  ../../include/seec/Clang/MappedProcessTrace.hpp
  ../../include/seec/Clang/MappedRuntimeErrorState.hpp
  ../../include/seec/Clang/MappedStateMovement.hpp
  ../../include/seec/Clang/MappedStreamState.hpp
  ../../include/seec/Clang/MappedThreadState.hpp
  ../../include/seec/Clang/MappedValue.hpp
  ../../include/seec/Clang/PrintOnlinePythonTutorTrace.hpp
  ../../include/seec/Clang/TypeMatch.hpp
)

set(SEEC_CLANG_MAPPED_TRACE_SOURCES
  GraphExpansion.cpp
  GraphLayout.cpp
  MappedAllocaState.cpp
  MappedFunctionState.cpp
  MappedGlobalVariable.cpp
  MappedMallocState.cpp
  MappedProcessState.cpp
  MappedProcessTrace.cpp
  MappedRuntimeErrorState.cpp
  MappedStateMovement.cpp
  MappedStreamState.cpp
  MappedThreadState.cpp
  MappedValue.cpp
  PrintOnlinePythonTutorTrace.cpp
  TypeMatch.cpp
)

add_library(SeeCClangMappedTrace ${SEEC_CLANG_MAPPED_TRACE_HEADERS} ${SEEC_CLANG_MAPPED_TRACE_SOURCES})

INSTALL(TARGETS SeeCClangMappedTrace
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

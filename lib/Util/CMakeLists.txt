set(HEADERS
  ../../include/seec/Util/CheckNew.hpp
  ../../include/seec/Util/ConstExprCString.hpp
  ../../include/seec/Util/ConstExprMath.hpp
  ../../include/seec/Util/DefaultArgPromotion.hpp
  ../../include/seec/Util/Error.hpp
  ../../include/seec/Util/Fallthrough.hpp
  ../../include/seec/Util/FixedWidthIntTypes.hpp
  ../../include/seec/Util/FunctionTraits.hpp
  ../../include/seec/Util/IndexTypesForLLVMObjects.hpp
  ../../include/seec/Util/LockedObjectAccessor.hpp
  ../../include/seec/Util/MakeFunction.hpp
  ../../include/seec/Util/Maybe.hpp
  ../../include/seec/Util/ModuleIndex.hpp
  ../../include/seec/Util/Observer.hpp
  ../../include/seec/Util/Printing.hpp
  ../../include/seec/Util/Range.hpp
  ../../include/seec/Util/Resources.hpp
  ../../include/seec/Util/Reverse.hpp
  ../../include/seec/Util/ScopeExit.hpp
  ../../include/seec/Util/Serialization.hpp
  ../../include/seec/Util/TemplateSequence.hpp
  ../../include/seec/Util/UpcomingStandardFeatures.hpp
  ../../include/seec/Util/ValueConversion.hpp
  )

set(SOURCES
  Error.cpp
  Printing.cpp
  Resources.cpp
  )

add_library(SeeCUtil ${HEADERS} ${SOURCES})

INSTALL(TARGETS SeeCUtil
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)


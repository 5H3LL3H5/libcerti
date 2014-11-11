OPTION(BUILD_LEGACY_LIBRTI
             "Build CERTI legacy libRTI" OFF)

IF (NOT BUILD_LEGACY_LIBRTI AND WIN32)
   OPTION(CERTI_RTING_DLL_USE_LIB_PREFIX
          "Use lib prefix for DLL (Windows Only)" 
          OFF)
ENDIF()

OPTION(BUILD_DOC
             "Build documentation (using doxygen)" OFF)

OPTION(FORCE_NO_X11
              "Force not to use X11 (i.e. no Billard GUI)" OFF)

# On demand of Eric Noulard, the unsafe tsc clocksource
OPTION(ENABLE_TSC_CLOCK
             "Enable the usage of the timestamp counter as clock source (use with care)" OFF)

# The communication channel to RTIA
OPTION(RTIA_USE_TCP
           "Force TCP socket usage between RTIA and FederateAmbassador (default is Unix Socket)" OFF)
IF(RTIA_USE_TCP)
    ADD_DEFINITIONS(-DRTIA_USE_TCP)
ENDIF(RTIA_USE_TCP)

OPTION(RTIA_CONSOLE_SHOW
       "Windows specific: if set to ON the RTIA console will be shown" OFF)
IF(RTIA_CONSOLE_SHOW)
    ADD_DEFINITIONS(-DRTIA_CONSOLE_SHOW)
ENDIF(RTIA_CONSOLE_SHOW)

# The new NULL Prime message protocol
OPTION(CERTI_USE_NULL_PRIME_MESSAGE_PROTOCOL
         "NULL PRIME MESSAGE protocol is an enhanced version of the CMB NULL MESSAGE protocol (experimental)" OFF)
IF(CERTI_USE_NULL_PRIME_MESSAGE_PROTOCOL)
    ADD_DEFINITIONS(-DCERTI_USE_NULL_PRIME_MESSAGE_PROTOCOL)
ENDIF(CERTI_USE_NULL_PRIME_MESSAGE_PROTOCOL)

# The CERTI Realtime extensions
OPTION(CERTI_REALTIME_EXTENSIONS
         "CERTI proposed realtime extension to HLA API" OFF)
IF(CERTI_REALTIME_EXTENSIONS)
    ADD_DEFINITIONS(-DCERTI_REALTIME_EXTENSIONS)
ENDIF(CERTI_REALTIME_EXTENSIONS)

# default behaviour is to build library as shared on all platform
OPTION(BUILD_SHARED
             "Build libraries as shared library" ON)

OPTION(USE_FULL_RPATH
             "Use the full RPATH" OFF)

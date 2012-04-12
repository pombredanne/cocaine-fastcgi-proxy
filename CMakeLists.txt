CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
PROJECT(Cocaine)

SET(COCAINE_VERSION 0.7)

CONFIGURE_FILE(
    "${PROJECT_SOURCE_DIR}/config.hpp.in"
    "${PROJECT_SOURCE_DIR}/config.hpp")

ADD_LIBRARY(
    cocaine-fastcgi MODULE
        module)

SET_TARGET_PROPERTIES(
    cocaine-fastcgi PROPERTIES
        PREFIX "")

TARGET_LINK_LIBRARIES(
    cocaine-fastcgi
        cocaine-dealer
        fastcgi-daemon2)

INSTALL(
    TARGETS
        cocaine-fastcgi
    LIBRARY DESTINATION lib/fastcgi2 COMPONENT runtime)

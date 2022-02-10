set(XMRIG_WEBSOCKET_LIBRARY "xmrig-websocket")

include(FetchContent)

# === boost ===
find_package(Boost REQUIRED COMPONENTS filesystem system thread regex)

# === websocket++ ===
FetchContent_Declare(websocketpp
GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git
  GIT_TAG 0.8.2)
FetchContent_GetProperties(websocketpp)
if(NOT websocketpp_POPULATED)
  FetchContent_Populate(websocketpp)
  add_subdirectory(${websocketpp_SOURCE_DIR} ${websocketpp_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
# add interface library with all websocketpp dependencies
add_library(${XMRIG_WEBSOCKET_LIBRARY} INTERFACE)
target_include_directories(${XMRIG_WEBSOCKET_LIBRARY} INTERFACE ${websocketpp_SOURCE_DIR})
target_link_libraries(${XMRIG_WEBSOCKET_LIBRARY} INTERFACE Boost::system Boost::thread Boost::regex)

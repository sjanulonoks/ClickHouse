# 'clickhouse' binary is a multi purpose tool,
# that contain multiple execution modes (client, server, etc.)
# each of them is built and linked as a separate library, defined below.

add_library(clickhouse-server
    Server.cpp
    HTTPHandler.cpp
    TCPHandler.cpp
    InterserverIOHTTPHandler.cpp
    MetricsTransmitter.cpp
    ConfigReloader.cpp
    StatusFile.cpp
    ReplicasStatusHandler.cpp)
target_link_libraries(clickhouse-server daemon clickhouse_storages_system clickhouse_functions clickhouse_aggregate_functions clickhouse_table_functions)

add_library(clickhouse-local LocalServer.cpp)
target_link_libraries(clickhouse-local dbms clickhouse-server clickhouse_functions clickhouse_aggregate_functions clickhouse_table_functions)

add_library(clickhouse-extract-from-config ExtractFromConfig.cpp)
target_link_libraries(clickhouse-extract-from-config dbms ${Boost_PROGRAM_OPTIONS_LIBRARY})

add_library (clickhouse-client Client.cpp)
target_link_libraries (clickhouse-client dbms clickhouse_functions clickhouse_aggregate_functions ${LINE_EDITING_LIBS} ${Boost_PROGRAM_OPTIONS_LIBRARY})
install (FILES clickhouse-client.xml DESTINATION ${CLICKHOUSE_ETC_DIR}/clickhouse-client COMPONENT clickhouse-client RENAME config.xml)

add_library (clickhouse-benchmark Benchmark.cpp)
target_link_libraries (clickhouse-benchmark dbms ${Boost_PROGRAM_OPTIONS_LIBRARY})

add_library (clickhouse-performance-test PerformanceTest.cpp)
target_link_libraries (clickhouse-performance-test dbms ${Boost_PROGRAM_OPTIONS_LIBRARY})


add_executable(clickhouse main.cpp)
target_link_libraries(clickhouse
    clickhouse-server
    clickhouse-client
    clickhouse-local
    clickhouse-benchmark
    clickhouse-performance-test
    clickhouse-extract-from-config)
INSTALL(TARGETS clickhouse RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT clickhouse)
# make symbolic links to concrete clickhouse applications
macro(install_symlink_to_clickhouse app)
    INSTALL(CODE "execute_process(COMMAND ln -sf clickhouse ${app} WORKING_DIRECTORY \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_BINDIR}\" )" COMPONENT ${app})
endmacro(install_symlink_to_clickhouse)

install_symlink_to_clickhouse(clickhouse-server)
install_symlink_to_clickhouse(clickhouse-client)
install_symlink_to_clickhouse(clickhouse-local)
install_symlink_to_clickhouse(clickhouse-benchmark)
install_symlink_to_clickhouse(clickhouse-performance-test)

INSTALL(
    FILES config.xml users.xml
    DESTINATION  ${CLICKHOUSE_ETC_DIR}/clickhouse-server
    COMPONENT clickhouse)

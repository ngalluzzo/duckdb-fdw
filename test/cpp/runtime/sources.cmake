# Remote Runtime owns its transport and executor fixture implementations.
set(REMOTE_RUNTIME_CURL_TEST_SUPPORT_SOURCES
    test/cpp/runtime/support/controlled_socket_service.cpp
    test/cpp/runtime/support/runtime_http_test_support.cpp
    test/cpp/runtime/support/private_curl_probe.cpp)
set(REMOTE_RUNTIME_EXECUTOR_TEST_SUPPORT_SOURCES
    test/cpp/runtime/support/http_scan_executor_test_support.cpp)
# Narrow Runtime-owned inventory for the private controlled product oracle.
# Root composition consumes this variable instead of listing Runtime sources.
set(REMOTE_RUNTIME_LOOPBACK_PRODUCT_SOURCES
    test/cpp/runtime/support/loopback_curl_runtime.cpp)

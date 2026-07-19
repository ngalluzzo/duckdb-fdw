# Remote Runtime owns these execution services. The narrower groups support
# focused consumers without importing the complete transport graph.
set(REMOTE_RUNTIME_INTERFACE_SOURCES
    src/runtime/api/authorization.cpp
    src/runtime/api/execution_error.cpp)
set(REMOTE_RUNTIME_PAGINATION_SOURCES
    src/runtime/pagination/uri_reference.cpp
    src/runtime/pagination/link_header.cpp
    src/runtime/pagination/link_pagination.cpp
    src/runtime/policy/scan_resource_accounting.cpp)
set(REMOTE_RUNTIME_EXECUTOR_SOURCES
    ${REMOTE_RUNTIME_PAGINATION_SOURCES}
    src/runtime/authentication/fixed_github_user_bearer_authenticator.cpp
    src/runtime/decoding/json_decoder.cpp
    src/runtime/execution/http_plan_admission.cpp
    src/runtime/execution/http_paginated_scan.cpp
    src/runtime/execution/http_scan_executor.cpp)
set(REMOTE_RUNTIME_SOURCES
    ${REMOTE_RUNTIME_EXECUTOR_SOURCES}
    src/runtime/policy/network_policy.cpp
    src/runtime/transport/http_chunk_decoder.cpp
    src/runtime/transport/curl_response_accumulator.cpp
    src/runtime/transport/curl_transfer.cpp
    src/runtime/transport/curl_http_transport.cpp
    src/runtime/transport/http_runtime.cpp)

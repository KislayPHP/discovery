extern "C" {
#include "php.h"
#include "ext/standard/info.h"
#include "Zend/zend_API.h"
#include "Zend/zend_exceptions.h"
#include "Zend/zend_interfaces.h"
}

#include "php_kislayphp_discovery.h"

#include <chrono>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#ifdef _WIN32
  #include <windows.h>
  #ifndef PTHREAD_WIN32_COMPAT
  #define PTHREAD_WIN32_COMPAT
  typedef SRWLOCK pthread_rwlock_t;
  #define pthread_rwlock_init(l, a)  (InitializeSRWLock(l), 0)
  #define pthread_rwlock_destroy(l)  ((void)0)
  #define pthread_rwlock_rdlock(l)   AcquireSRWLockShared(l)
  #define pthread_rwlock_wrlock(l)   AcquireSRWLockExclusive(l)
  #define pthread_rwlock_unlock_rd(l) ReleaseSRWLockShared(l)
  #define pthread_rwlock_unlock_wr(l) ReleaseSRWLockExclusive(l)
  #endif
#else
  #include <pthread.h>
  #define pthread_rwlock_unlock_rd(l) pthread_rwlock_unlock(l)
  #define pthread_rwlock_unlock_wr(l) pthread_rwlock_unlock(l)
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <unistd.h>
#endif

#ifdef KISLAYPHP_RPC
#include <grpcpp/grpcpp.h>

#include "discovery.grpc.pb.h"
#endif

#ifndef zend_call_method_with_0_params
static inline void kislayphp_call_method_with_0_params(
    zend_object *obj,
    zend_class_entry *obj_ce,
    zend_function **fn_proxy,
    const char *function_name,
    zval *retval) {
    zend_call_method(obj, obj_ce, fn_proxy, function_name, std::strlen(function_name), retval, 0, nullptr, nullptr);
}

#define zend_call_method_with_0_params(obj, obj_ce, fn_proxy, function_name, retval) \
    kislayphp_call_method_with_0_params(obj, obj_ce, fn_proxy, function_name, retval)
#endif

#ifndef zend_call_method_with_1_params
static inline void kislayphp_call_method_with_1_params(
    zend_object *obj,
    zend_class_entry *obj_ce,
    zend_function **fn_proxy,
    const char *function_name,
    zval *retval,
    zval *param1) {
    zend_call_method(obj, obj_ce, fn_proxy, function_name, std::strlen(function_name), retval, 1, param1, nullptr);
}

#define zend_call_method_with_1_params(obj, obj_ce, fn_proxy, function_name, retval, param1) \
    kislayphp_call_method_with_1_params(obj, obj_ce, fn_proxy, function_name, retval, param1)
#endif

#ifndef zend_call_method_with_2_params
static inline void kislayphp_call_method_with_2_params(
    zend_object *obj,
    zend_class_entry *obj_ce,
    zend_function **fn_proxy,
    const char *function_name,
    zval *retval,
    zval *param1,
    zval *param2) {
    zend_call_method(obj, obj_ce, fn_proxy, function_name, std::strlen(function_name), retval, 2, param1, param2);
}

#define zend_call_method_with_2_params(obj, obj_ce, fn_proxy, function_name, retval, param1, param2) \
    kislayphp_call_method_with_2_params(obj, obj_ce, fn_proxy, function_name, retval, param1, param2)
#endif

static zend_class_entry *kislayphp_discovery_ce;
static zend_class_entry *kislayphp_discovery_client_ce;

static zend_long kislayphp_env_long(const char *name, zend_long fallback) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return static_cast<zend_long>(std::strtoll(value, nullptr, 10));
}

static bool kislayphp_env_bool(const char *name, bool fallback) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    if (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 || std::strcmp(value, "TRUE") == 0) {
        return true;
    }
    if (std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 || std::strcmp(value, "FALSE") == 0) {
        return false;
    }
    return fallback;
}

static std::string kislayphp_env_string(const char *name, const std::string &fallback) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return std::string(value);
}

static bool kislayphp_call_php_function(const char *function_name,
                                        uint32_t argc,
                                        zval *argv,
                                        zval *retval) {
    if (retval != nullptr) {
        ZVAL_UNDEF(retval);
    }

    zval callable;
    ZVAL_STRING(&callable, function_name);
    const int result = call_user_function(EG(function_table), nullptr, &callable, retval, argc, argv);
    zval_ptr_dtor(&callable);

    return result == SUCCESS && EG(exception) == nullptr;
}

static std::string kislayphp_json_escape(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buffer[7];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", ch);
                    escaped += buffer;
                } else {
                    escaped.push_back(static_cast<char>(ch));
                }
        }
    }
    return escaped;
}

static std::string kislayphp_url_encode(const std::string &value) {
    static const char hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size() * 3);
    for (unsigned char ch : value) {
        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded.push_back(static_cast<char>(ch));
        } else if (ch == ' ') {
            encoded.push_back('+');
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[(ch >> 4) & 0x0F]);
            encoded.push_back(hex[ch & 0x0F]);
        }
    }
    return encoded;
}

static std::string kislayphp_url_decode(const std::string &value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (ch == '+') {
            decoded.push_back(' ');
            continue;
        }
        if (ch == '%' && i + 2 < value.size()) {
            const char hi = value[i + 1];
            const char lo = value[i + 2];
            auto to_hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
                if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                return -1;
            };
            const int hi_val = to_hex(hi);
            const int lo_val = to_hex(lo);
            if (hi_val >= 0 && lo_val >= 0) {
                decoded.push_back(static_cast<char>((hi_val << 4) | lo_val));
                i += 2;
                continue;
            }
        }
        decoded.push_back(ch);
    }
    return decoded;
}

static void kislayphp_parse_form_pairs(const std::string &encoded,
                                       std::unordered_map<std::string, std::string> &params) {
    params.clear();
    if (encoded.empty()) {
        return;
    }

    size_t start = 0;
    while (start <= encoded.size()) {
        size_t end = encoded.find('&', start);
        if (end == std::string::npos) {
            end = encoded.size();
        }

        const std::string part = encoded.substr(start, end - start);
        if (!part.empty()) {
            size_t sep = part.find('=');
            if (sep == std::string::npos) {
                params[kislayphp_url_decode(part)] = "";
            } else {
                params[kislayphp_url_decode(part.substr(0, sep))] =
                    kislayphp_url_decode(part.substr(sep + 1));
            }
        }

        if (end == encoded.size()) {
            break;
        }
        start = end + 1;
    }
}

struct kislayphp_http_endpoint_t {
    std::string host;
    int port;
    std::string base_path;
};

static bool kislayphp_parse_http_endpoint(const std::string &base_url,
                                          kislayphp_http_endpoint_t *endpoint,
                                          std::string *error_out = nullptr) {
    if (endpoint == nullptr) {
        if (error_out != nullptr) {
            *error_out = "internal error: endpoint is null";
        }
        return false;
    }

    const std::string prefix("http://");
    if (base_url.rfind(prefix, 0) != 0) {
        if (error_out != nullptr) {
            *error_out = "Only http:// endpoints are supported";
        }
        return false;
    }

    std::string rest = base_url.substr(prefix.size());
    std::string host_port;
    std::string path = "/";
    size_t slash = rest.find('/');
    if (slash == std::string::npos) {
        host_port = rest;
    } else {
        host_port = rest.substr(0, slash);
        path = rest.substr(slash);
    }

    if (host_port.empty()) {
        if (error_out != nullptr) {
            *error_out = "Endpoint host is required";
        }
        return false;
    }

    int port = 80;
    std::string host = host_port;
    size_t colon = host_port.rfind(':');
    if (colon != std::string::npos && colon + 1 < host_port.size()) {
        host = host_port.substr(0, colon);
        port = std::atoi(host_port.substr(colon + 1).c_str());
    }

    if (host.empty() || port <= 0) {
        if (error_out != nullptr) {
            *error_out = "Invalid endpoint host or port";
        }
        return false;
    }

    if (path.empty()) {
        path = "/";
    }
    if (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }

    endpoint->host = host;
    endpoint->port = port;
    endpoint->base_path = path;
    return true;
}

static zend_long kislayphp_sanitize_heartbeat_timeout_ms(zend_long value, const char *source) {
    if (value < 1000) {
        php_error_docref(nullptr, E_WARNING, "%s: heartbeat timeout %lldms is too low; using 1000ms",
                         source, static_cast<long long>(value));
        return 1000;
    }
    return value;
}

#ifdef KISLAYPHP_RPC
static bool kislayphp_rpc_enabled() {
    return kislayphp_env_bool("KISLAY_RPC_ENABLED", false);
}

static zend_long kislayphp_rpc_timeout_ms() {
    zend_long timeout = kislayphp_env_long("KISLAY_RPC_TIMEOUT_MS", 200);
    return timeout > 0 ? timeout : 200;
}

static std::string kislayphp_rpc_discovery_endpoint() {
    return kislayphp_env_string("KISLAY_RPC_DISCOVERY_ENDPOINT", "127.0.0.1:9090");
}

static kislay::discovery::v1::DiscoveryService::Stub *kislayphp_rpc_discovery_stub(const std::string &endpoint) {
    static std::mutex lock;
    static std::string cached_endpoint;
    static std::shared_ptr<grpc::Channel> channel;
    static std::unique_ptr<kislay::discovery::v1::DiscoveryService::Stub> stub;
    std::lock_guard<std::mutex> guard(lock);
    if (!stub || cached_endpoint != endpoint) {
        channel = grpc::CreateChannel(endpoint, grpc::InsecureChannelCredentials());
        stub = kislay::discovery::v1::DiscoveryService::NewStub(channel);
        cached_endpoint = endpoint;
    }
    return stub.get();
}

static bool kislayphp_rpc_discovery_register(const std::string &service,
                                             const std::string &instance_id,
                                             const std::string &url,
                                             const std::unordered_map<std::string, std::string> &metadata,
                                             std::string *error) {
    auto *stub = kislayphp_rpc_discovery_stub(kislayphp_rpc_discovery_endpoint());
    if (!stub) {
        if (error) {
            *error = "RPC stub unavailable";
        }
        return false;
    }

    kislay::discovery::v1::RegisterRequest request;
    request.set_service_name(service);
    request.set_instance_id(instance_id);
    request.set_url(url);
    for (const auto &entry : metadata) {
        (*request.mutable_metadata())[entry.first] = entry.second;
    }

    kislay::discovery::v1::RegisterResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->Register(&context, request, &response);
    if (!status.ok()) {
        if (error) {
            *error = status.error_message();
        }
        return false;
    }
    if (!response.ok()) {
        if (error) {
            *error = response.error();
        }
        return false;
    }
    return true;
}

static bool kislayphp_rpc_discovery_deregister(const std::string &service,
                                               const std::string &instance_id,
                                               std::string *error) {
    auto *stub = kislayphp_rpc_discovery_stub(kislayphp_rpc_discovery_endpoint());
    if (!stub) {
        if (error) {
            *error = "RPC stub unavailable";
        }
        return false;
    }

    kislay::discovery::v1::DeregisterRequest request;
    request.set_service_name(service);
    request.set_instance_id(instance_id);

    kislay::discovery::v1::DeregisterResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->Deregister(&context, request, &response);
    if (!status.ok()) {
        if (error) {
            *error = status.error_message();
        }
        return false;
    }
    if (!response.ok()) {
        if (error) {
            *error = response.error();
        }
        return false;
    }
    return true;
}

static bool kislayphp_rpc_discovery_resolve(const std::string &service,
                                            php_kislayphp_discovery_t::ServiceInstance *instance,
                                            std::string *error) {
    auto *stub = kislayphp_rpc_discovery_stub(kislayphp_rpc_discovery_endpoint());
    if (!stub) {
        if (error) {
            *error = "RPC stub unavailable";
        }
        return false;
    }

    kislay::discovery::v1::ResolveRequest request;
    request.set_service_name(service);

    kislay::discovery::v1::ResolveResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->Resolve(&context, request, &response);
    if (!status.ok()) {
        if (error) {
            *error = status.error_message();
        }
        return false;
    }
    if (!response.ok()) {
        if (error) {
            *error = response.error();
        }
        return false;
    }
    if (instance) {
        const auto &remote = response.instance();
        instance->service_name = remote.service_name();
        instance->instance_id = remote.instance_id();
        instance->url = remote.url();
        instance->status = remote.status();
        instance->last_heartbeat_ms = remote.last_heartbeat_ms();
        instance->metadata.clear();
        for (const auto &entry : remote.metadata()) {
            instance->metadata[entry.first] = entry.second;
        }
    }
    return true;
}

static bool kislayphp_rpc_discovery_list(zval *return_value, std::string *error) {
    auto *stub = kislayphp_rpc_discovery_stub(kislayphp_rpc_discovery_endpoint());
    if (!stub) {
        if (error) {
            *error = "RPC stub unavailable";
        }
        return false;
    }

    kislay::discovery::v1::ListServicesRequest request;
    kislay::discovery::v1::ListServicesResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->ListServices(&context, request, &response);
    if (!status.ok()) {
        if (error) {
            *error = status.error_message();
        }
        return false;
    }

    array_init(return_value);
    for (const auto &name : response.service_names()) {
        add_assoc_string(return_value, name.c_str(), "");
    }
    return true;
}

static bool kislayphp_rpc_discovery_list_instances(const std::string &service, zval *return_value, std::string *error) {
    auto *stub = kislayphp_rpc_discovery_stub(kislayphp_rpc_discovery_endpoint());
    if (!stub) {
        if (error) {
            *error = "RPC stub unavailable";
        }
        return false;
    }

    kislay::discovery::v1::ListInstancesRequest request;
    request.set_service_name(service);
    kislay::discovery::v1::ListInstancesResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->ListInstances(&context, request, &response);
    if (!status.ok()) {
        if (error) {
            *error = status.error_message();
        }
        return false;
    }

    array_init(return_value);
    for (const auto &remote : response.instances()) {
        php_kislayphp_discovery_t::ServiceInstance instance;
        instance.service_name = remote.service_name();
        instance.instance_id = remote.instance_id();
        instance.url = remote.url();
        instance.status = remote.status();
        instance.last_heartbeat_ms = remote.last_heartbeat_ms();
        instance.metadata.clear();
        for (const auto &entry : remote.metadata()) {
            instance.metadata[entry.first] = entry.second;
        }
        kislayphp_add_instance_array(return_value, instance);
    }
    return true;
}

static bool kislayphp_rpc_discovery_heartbeat(const std::string &service, const std::string &instance_id, std::string *error) {
    auto *stub = kislayphp_rpc_discovery_stub(kislayphp_rpc_discovery_endpoint());
    if (!stub) {
        if (error) {
            *error = "RPC stub unavailable";
        }
        return false;
    }

    kislay::discovery::v1::HeartbeatRequest request;
    request.set_service_name(service);
    request.set_instance_id(instance_id);
    kislay::discovery::v1::HeartbeatResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->Heartbeat(&context, request, &response);
    if (!status.ok()) {
        if (error) {
            *error = status.error_message();
        }
        return false;
    }
    if (!response.ok()) {
        if (error) {
            *error = response.error();
        }
        return false;
    }
    return true;
}

static bool kislayphp_rpc_discovery_set_status(const std::string &service,
                                               const std::string &instance_id,
                                               const std::string &status_value,
                                               std::string *error) {
    auto *stub = kislayphp_rpc_discovery_stub(kislayphp_rpc_discovery_endpoint());
    if (!stub) {
        if (error) {
            *error = "RPC stub unavailable";
        }
        return false;
    }

    kislay::discovery::v1::SetStatusRequest request;
    request.set_service_name(service);
    request.set_instance_id(instance_id);
    request.set_status(status_value);
    kislay::discovery::v1::SetStatusResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->SetStatus(&context, request, &response);
    if (!status.ok()) {
        if (error) {
            *error = status.error_message();
        }
        return false;
    }
    if (!response.ok()) {
        if (error) {
            *error = response.error();
        }
        return false;
    }
    return true;
}
#endif

typedef struct _php_kislayphp_discovery_t {
    struct ServiceInstance {
        std::string service_name;
        std::string instance_id;
        std::string url;
        std::string status;
        std::unordered_map<std::string, std::string> metadata;
        long long last_heartbeat_ms;
        int weight;
    };

    std::unordered_map<std::string, std::string> services;
    std::unordered_map<std::string, std::unordered_map<std::string, ServiceInstance>> instances;
    std::unordered_map<std::string, size_t> rr_index;
    std::string balancer_type;
    std::string storage_backend;
    bool redis_enabled;
    std::string redis_host;
    zend_long redis_port;
    zend_long redis_db;
    zend_long redis_timeout_ms;
    std::string redis_password;
    std::string redis_prefix;
    pthread_rwlock_t lock;
    zval bus;
    bool has_bus;
    zval client;
    bool has_client;
    zend_long heartbeat_timeout_ms;
    zend_long max_instances_per_service;
    std::string remote_base_url;
    std::string listen_host;
    zend_long listen_port;
    bool has_remote_base_url;
    bool has_listen_config;
    zend_object std;
} php_kislayphp_discovery_t;

static zend_object_handlers kislayphp_discovery_handlers;

static inline php_kislayphp_discovery_t *php_kislayphp_discovery_from_obj(zend_object *obj) {
    return reinterpret_cast<php_kislayphp_discovery_t *>(
        reinterpret_cast<char *>(obj) - XtOffsetOf(php_kislayphp_discovery_t, std));
}

static zend_object *kislayphp_discovery_create_object(zend_class_entry *ce) {
    php_kislayphp_discovery_t *obj = static_cast<php_kislayphp_discovery_t *>(
        ecalloc(1, sizeof(php_kislayphp_discovery_t) + zend_object_properties_size(ce)));
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    new (&obj->services) std::unordered_map<std::string, std::string>();
    new (&obj->instances) std::unordered_map<std::string, std::unordered_map<std::string, php_kislayphp_discovery_t::ServiceInstance>>();
    new (&obj->rr_index) std::unordered_map<std::string, size_t>();
    new (&obj->balancer_type) std::string("weighted_random");
    new (&obj->storage_backend) std::string(kislayphp_env_string("KISLAY_DISCOVERY_STORAGE", "memory"));
    obj->redis_enabled = obj->storage_backend == "redis";
    new (&obj->redis_host) std::string(kislayphp_env_string("KISLAY_DISCOVERY_REDIS_HOST", "127.0.0.1"));
    obj->redis_port = kislayphp_env_long("KISLAY_DISCOVERY_REDIS_PORT", 6379);
    if (obj->redis_port < 1) {
        obj->redis_port = 6379;
    }
    obj->redis_db = kislayphp_env_long("KISLAY_DISCOVERY_REDIS_DB", 0);
    if (obj->redis_db < 0) {
        obj->redis_db = 0;
    }
    obj->redis_timeout_ms = kislayphp_env_long("KISLAY_DISCOVERY_REDIS_TIMEOUT_MS", 200);
    if (obj->redis_timeout_ms < 1) {
        obj->redis_timeout_ms = 200;
    }
    new (&obj->redis_password) std::string(kislayphp_env_string("KISLAY_DISCOVERY_REDIS_PASSWORD", ""));
    new (&obj->redis_prefix) std::string(kislayphp_env_string("KISLAY_DISCOVERY_REDIS_PREFIX", "kislay:discovery"));
    pthread_rwlock_init(&obj->lock, nullptr);
    ZVAL_UNDEF(&obj->bus);
    obj->has_bus = false;
    ZVAL_UNDEF(&obj->client);
    obj->has_client = false;
    obj->heartbeat_timeout_ms = kislayphp_sanitize_heartbeat_timeout_ms(
        kislayphp_env_long("KISLAY_DISCOVERY_HEARTBEAT_TIMEOUT_MS", 90000),
        "Kislay\\Discovery\\ServiceRegistry::__construct");
    obj->max_instances_per_service = kislayphp_env_long("KISLAY_DISCOVERY_MAX_INSTANCES_PER_SERVICE", 1024);
    if (obj->max_instances_per_service < 1) {
        obj->max_instances_per_service = 1024;
    }
    new (&obj->remote_base_url) std::string();
    new (&obj->listen_host) std::string("127.0.0.1");
    obj->listen_port = 0;
    obj->has_remote_base_url = false;
    obj->has_listen_config = false;
    obj->std.handlers = &kislayphp_discovery_handlers;
    return &obj->std;
}

static void kislayphp_discovery_free_obj(zend_object *object) {
    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(object);
    if (obj->has_bus) {
        zval_ptr_dtor(&obj->bus);
    }
    if (obj->has_client) {
        zval_ptr_dtor(&obj->client);
    }
    obj->redis_prefix.~basic_string();
    obj->redis_password.~basic_string();
    obj->redis_host.~basic_string();
    obj->storage_backend.~basic_string();
    obj->listen_host.~basic_string();
    obj->remote_base_url.~basic_string();
    obj->rr_index.~unordered_map();
    obj->balancer_type.~basic_string();
    obj->instances.~unordered_map();
    obj->services.~unordered_map();
    pthread_rwlock_destroy(&obj->lock);
    zend_object_std_dtor(&obj->std);
}

static void kislayphp_discovery_emit(php_kislayphp_discovery_t *obj,
                                     const char *event,
                                     const std::string &name,
                                     const std::string &url) {
    if (!obj->has_bus) {
        return;
    }
    if (Z_TYPE(obj->bus) != IS_OBJECT) {
        return;
    }

    zval payload;
    array_init(&payload);
    add_assoc_string(&payload, "name", name.c_str());
    add_assoc_string(&payload, "url", url.c_str());

    zval event_name;
    ZVAL_STRING(&event_name, event);

    zval retval;
    ZVAL_UNDEF(&retval);
    zend_call_method_with_2_params(Z_OBJ(obj->bus), Z_OBJCE(obj->bus), nullptr, "emit", &retval, &event_name, &payload);

    zval_ptr_dtor(&event_name);
    zval_ptr_dtor(&payload);
    if (!Z_ISUNDEF(retval)) {
        zval_ptr_dtor(&retval);
    }
}

static long long kislayphp_now_ms() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

static std::string kislayphp_upper(std::string value) {
    for (char &c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}

static bool kislayphp_is_valid_status(const std::string &status) {
    return status == "UP" || status == "DOWN" || status == "OUT_OF_SERVICE" || status == "UNKNOWN";
}

static void kislayphp_parse_metadata_array(zval *metadata_zv, std::unordered_map<std::string, std::string> &metadata) {
    metadata.clear();
    if (metadata_zv == nullptr || Z_TYPE_P(metadata_zv) != IS_ARRAY) {
        return;
    }
    HashTable *ht = Z_ARRVAL_P(metadata_zv);
    zval *entry = nullptr;
    zend_string *key = nullptr;
    ZEND_HASH_FOREACH_STR_KEY_VAL(ht, key, entry) {
        if (key == nullptr || entry == nullptr) {
            continue;
        }
        zend_string *val_str = zval_get_string(entry);
        metadata[std::string(ZSTR_VAL(key), ZSTR_LEN(key))] = std::string(ZSTR_VAL(val_str), ZSTR_LEN(val_str));
        zend_string_release(val_str);
    } ZEND_HASH_FOREACH_END();
}

static void kislayphp_add_instance_array(zval *target, const php_kislayphp_discovery_t::ServiceInstance &instance) {
    zval item;
    array_init(&item);
    add_assoc_string(&item, "service", instance.service_name.c_str());
    add_assoc_string(&item, "instanceId", instance.instance_id.c_str());
    add_assoc_string(&item, "url", instance.url.c_str());
    add_assoc_string(&item, "status", instance.status.c_str());
    add_assoc_long(&item, "lastHeartbeat", static_cast<zend_long>(instance.last_heartbeat_ms));

    zval meta;
    array_init(&meta);
    for (const auto &entry : instance.metadata) {
        add_assoc_string(&meta, entry.first.c_str(), entry.second.c_str());
    }
    add_assoc_zval(&item, "metadata", &meta);
    add_next_index_zval(target, &item);
}

static bool kislayphp_object_has_method(zval *object, const char *method_name) {
    if (object == nullptr || Z_TYPE_P(object) != IS_OBJECT || method_name == nullptr) {
        return false;
    }
    std::string lookup(method_name);
    for (char &ch : lookup) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return zend_hash_str_exists(&Z_OBJCE_P(object)->function_table, lookup.c_str(), lookup.size());
}

static bool kislayphp_call_object_method(zval *object,
                                         const char *method_name,
                                         uint32_t argc,
                                         zval *argv,
                                         zval *retval) {
    if (retval != nullptr) {
        ZVAL_UNDEF(retval);
    }
    if (!kislayphp_object_has_method(object, method_name)) {
        return false;
    }

    zval callable;
    array_init(&callable);
    zval obj_copy;
    ZVAL_COPY(&obj_copy, object);
    add_next_index_zval(&callable, &obj_copy);
    add_next_index_string(&callable, method_name);

    int call_result = call_user_function(EG(function_table), nullptr, &callable, retval, argc, argv);
    zval_ptr_dtor(&callable);
    return call_result == SUCCESS;
}

static size_t kislayphp_random_index(size_t size) {
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, size - 1);
    return dist(rng);
}

static bool kislayphp_discovery_prune_stale_locked(
    php_kislayphp_discovery_t *obj,
    const std::string *only_service,
    std::vector<std::pair<std::string, std::string>> *stale_instances = nullptr) {
    const long long now_ms = kislayphp_now_ms();
    bool removed_any = false;

    for (auto service_it = obj->instances.begin(); service_it != obj->instances.end();) {
        if (only_service != nullptr && service_it->first != *only_service) {
            ++service_it;
            continue;
        }

        auto &service_instances = service_it->second;
        for (auto inst_it = service_instances.begin(); inst_it != service_instances.end();) {
            const auto &instance = inst_it->second;
            const bool is_fresh = (now_ms - instance.last_heartbeat_ms) <= static_cast<long long>(obj->heartbeat_timeout_ms);
            if (is_fresh) {
                ++inst_it;
                continue;
            }

            if (stale_instances != nullptr) {
                stale_instances->push_back({service_it->first, instance.url});
            }
            inst_it = service_instances.erase(inst_it);
            removed_any = true;
        }

        if (service_instances.empty()) {
            obj->services.erase(service_it->first);
            obj->rr_index.erase(service_it->first);
            service_it = obj->instances.erase(service_it);
            continue;
        }

        obj->services[service_it->first] = service_instances.begin()->second.url;
        ++service_it;
    }

    return removed_any;
}

static bool kislayphp_select_healthy_instance(php_kislayphp_discovery_t *obj,
                                              const std::string &service,
                                              const std::string &hash_key,
                                              php_kislayphp_discovery_t::ServiceInstance *selected) {
    auto service_it = obj->instances.find(service);
    if (service_it == obj->instances.end() || service_it->second.empty()) {
        return false;
    }

    const long long now_ms = kislayphp_now_ms();
    std::vector<const php_kislayphp_discovery_t::ServiceInstance *> healthy;
    healthy.reserve(service_it->second.size());
    for (const auto &instance_it : service_it->second) {
        const auto &instance = instance_it.second;
        const bool is_fresh = (now_ms - instance.last_heartbeat_ms) <= static_cast<long long>(obj->heartbeat_timeout_ms);
        if (instance.status == "UP" && is_fresh) {
            healthy.push_back(&instance);
        }
    }
    if (healthy.empty()) {
        return false;
    }

    // Consistent hash: if a routing key is provided, always route to the same instance
    if (!hash_key.empty()) {
        size_t h = std::hash<std::string>{}(hash_key);
        *selected = *healthy[h % healthy.size()];
        return true;
    }

    const std::string &balancer = obj->balancer_type;

    if (balancer == "round_robin") {
        size_t index = obj->rr_index[service] % healthy.size();
        obj->rr_index[service] = (index + 1) % healthy.size();
        *selected = *healthy[index];
        return true;
    }

    if (balancer == "random") {
        size_t idx = kislayphp_random_index(healthy.size());
        *selected = *healthy[idx];
        return true;
    }

    // Default: weighted_random
    int total_weight = 0;
    for (const auto *inst : healthy) total_weight += inst->weight;
    if (total_weight <= 0) {
        *selected = *healthy[kislayphp_random_index(healthy.size())];
        return true;
    }
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, total_weight - 1);
    int r = dist(rng);
    int cumulative = 0;
    for (const auto *inst : healthy) {
        cumulative += inst->weight;
        if (r < cumulative) { *selected = *inst; return true; }
    }
    *selected = *healthy.back();
    return true;
}

static bool kislayphp_json_encode_zval(zval *value,
                                       std::string *json_out,
                                       std::string *error_out = nullptr) {
    zval args[1];
    zval retval;
    ZVAL_COPY(&args[0], value);
    ZVAL_UNDEF(&retval);

    const bool called = kislayphp_call_php_function("json_encode", 1, args, &retval);
    zval_ptr_dtor(&args[0]);

    if (!called || Z_TYPE(retval) != IS_STRING) {
        if (!Z_ISUNDEF(retval)) {
            zval_ptr_dtor(&retval);
        }
        if (error_out != nullptr) {
            *error_out = "Failed to encode JSON";
        }
        return false;
    }

    if (json_out != nullptr) {
        json_out->assign(Z_STRVAL(retval), Z_STRLEN(retval));
    }
    zval_ptr_dtor(&retval);
    return true;
}

static bool kislayphp_json_decode_assoc(const std::string &json,
                                        zval *return_value,
                                        std::string *error_out = nullptr) {
    zval args[2];
    zval retval;
    ZVAL_STRINGL(&args[0], json.c_str(), json.size());
    ZVAL_TRUE(&args[1]);
    ZVAL_UNDEF(&retval);

    const bool called = kislayphp_call_php_function("json_decode", 2, args, &retval);
    zval_ptr_dtor(&args[0]);

    if (!called || Z_ISUNDEF(retval)) {
        if (!Z_ISUNDEF(retval)) {
            zval_ptr_dtor(&retval);
        }
        if (error_out != nullptr) {
            *error_out = "Failed to decode JSON";
        }
        return false;
    }

    ZVAL_COPY(return_value, &retval);
    zval_ptr_dtor(&retval);
    return true;
}

static void kislayphp_discovery_metadata_from_params(
    const std::unordered_map<std::string, std::string> &params,
    std::unordered_map<std::string, std::string> &metadata) {
    metadata.clear();
    for (const auto &entry : params) {
        if (entry.first.rfind("meta.", 0) == 0 && entry.first.size() > 5) {
            metadata[entry.first.substr(5)] = entry.second;
        }
    }
}

#ifndef _WIN32
static std::string kislayphp_discovery_redis_services_key(php_kislayphp_discovery_t *obj);
static std::string kislayphp_discovery_redis_instances_key(php_kislayphp_discovery_t *obj, const std::string &service);
static bool kislayphp_discovery_instance_to_json(const php_kislayphp_discovery_t::ServiceInstance &instance,
                                                 std::string *json_out,
                                                 std::string *error_out);
static bool kislayphp_discovery_redis_hget(php_kislayphp_discovery_t *obj,
                                           const std::string &key,
                                           const std::string &field,
                                           std::string *value_out,
                                           bool *exists_out,
                                           std::string *error_out);
static bool kislayphp_discovery_redis_hlen(php_kislayphp_discovery_t *obj,
                                           const std::string &key,
                                           long long *count_out,
                                           std::string *error_out);
static bool kislayphp_discovery_redis_simple_write(php_kislayphp_discovery_t *obj,
                                                   const std::vector<std::string> &parts,
                                                   std::string *error_out);
static bool kislayphp_discovery_redis_sync_service(php_kislayphp_discovery_t *obj,
                                                   const std::string &service,
                                                   std::string *error_out);
static bool kislayphp_discovery_redis_sync_all_services(php_kislayphp_discovery_t *obj,
                                                        std::string *error_out);
static void kislayphp_discovery_warn_redis_fallback(const std::string &error);
#else
static std::string kislayphp_discovery_redis_services_key(php_kislayphp_discovery_t *) { return ""; }
static std::string kislayphp_discovery_redis_instances_key(php_kislayphp_discovery_t *, const std::string &) { return ""; }
static bool kislayphp_discovery_instance_to_json(const php_kislayphp_discovery_t::ServiceInstance &, std::string *, std::string *) { return false; }
static bool kislayphp_discovery_redis_hget(php_kislayphp_discovery_t *, const std::string &, const std::string &, std::string *, bool *, std::string *) { return false; }
static bool kislayphp_discovery_redis_hlen(php_kislayphp_discovery_t *, const std::string &, long long *, std::string *) { return false; }
static bool kislayphp_discovery_redis_simple_write(php_kislayphp_discovery_t *, const std::vector<std::string> &, std::string *) { return false; }
static bool kislayphp_discovery_redis_sync_service(php_kislayphp_discovery_t *, const std::string &, std::string *) { return false; }
static bool kislayphp_discovery_redis_sync_all_services(php_kislayphp_discovery_t *, std::string *) { return false; }
static void kislayphp_discovery_warn_redis_fallback(const std::string &) {}
#endif

static bool kislayphp_discovery_register_local(
    php_kislayphp_discovery_t *obj,
    const std::string &service,
    const std::string &service_url,
    const std::unordered_map<std::string, std::string> &metadata,
    const std::string &instance) {
    int inst_weight = 1;
    auto weight_it = metadata.find("weight");
    if (weight_it != metadata.end()) {
        inst_weight = std::max(1, std::atoi(weight_it->second.c_str()));
    }

    if (obj->redis_enabled) {
        std::string redis_error;
        bool exists = false;
        std::string existing_payload;
        const std::string instances_key = kislayphp_discovery_redis_instances_key(obj, service);
        if (kislayphp_discovery_redis_hget(obj, instances_key, instance, &existing_payload, &exists, &redis_error)) {
            if (!exists) {
                long long instance_count = 0;
                if (kislayphp_discovery_redis_hlen(obj, instances_key, &instance_count, &redis_error)
                    && instance_count >= obj->max_instances_per_service) {
                    return false;
                }
            }
        } else {
            kislayphp_discovery_warn_redis_fallback(redis_error);
        }
    }

    std::vector<std::pair<std::string, std::string>> stale_instances;
    pthread_rwlock_wrlock(&obj->lock);
    kislayphp_discovery_prune_stale_locked(obj, &service, &stale_instances);
    auto &service_instances = obj->instances[service];
    if (service_instances.find(instance) == service_instances.end()
        && service_instances.size() >= static_cast<size_t>(obj->max_instances_per_service)) {
        pthread_rwlock_unlock_wr(&obj->lock);
        for (const auto &stale : stale_instances) {
            kislayphp_discovery_emit(obj, "discovery.heartbeat.timeout", stale.first, stale.second);
        }
        return false;
    }
    php_kislayphp_discovery_t::ServiceInstance record;
    record.service_name = service;
    record.instance_id = instance;
    record.url = service_url;
    record.status = "UP";
    record.metadata = metadata;
    record.weight = inst_weight;
    record.last_heartbeat_ms = kislayphp_now_ms();
    service_instances[instance] = record;
    obj->services[service] = service_url;
    pthread_rwlock_unlock_wr(&obj->lock);
    for (const auto &stale : stale_instances) {
        kislayphp_discovery_emit(obj, "discovery.heartbeat.timeout", stale.first, stale.second);
    }

    if (obj->redis_enabled) {
        std::string redis_error;
        std::string payload_json;
        if (!kislayphp_discovery_instance_to_json(record, &payload_json, &redis_error)
            || !kislayphp_discovery_redis_simple_write(obj, {"HSET", kislayphp_discovery_redis_instances_key(obj, service), instance, payload_json}, &redis_error)
            || !kislayphp_discovery_redis_simple_write(obj, {"HSET", kislayphp_discovery_redis_services_key(obj), service, service_url}, &redis_error)) {
            kislayphp_discovery_warn_redis_fallback(redis_error);
        }
    }
    return true;
}

static bool kislayphp_discovery_deregister_local(
    php_kislayphp_discovery_t *obj,
    const std::string &service,
    const std::string &instance_id,
    std::string *removed_url = nullptr) {
    std::string url;

    if (obj->redis_enabled) {
        std::string redis_error;
        if (!instance_id.empty()) {
            if (!kislayphp_discovery_redis_simple_write(obj,
                                                        {"HDEL", kislayphp_discovery_redis_instances_key(obj, service), instance_id},
                                                        &redis_error)) {
                kislayphp_discovery_warn_redis_fallback(redis_error);
            } else {
                long long remaining = 0;
                if (kislayphp_discovery_redis_hlen(obj, kislayphp_discovery_redis_instances_key(obj, service), &remaining, &redis_error)) {
                    if (remaining == 0) {
                        (void)kislayphp_discovery_redis_simple_write(obj, {"HDEL", kislayphp_discovery_redis_services_key(obj), service}, nullptr);
                    } else {
                        (void)kislayphp_discovery_redis_sync_service(obj, service, nullptr);
                    }
                }
            }
        } else {
            if (!kislayphp_discovery_redis_simple_write(obj, {"DEL", kislayphp_discovery_redis_instances_key(obj, service)}, &redis_error)
                || !kislayphp_discovery_redis_simple_write(obj, {"HDEL", kislayphp_discovery_redis_services_key(obj), service}, &redis_error)) {
                kislayphp_discovery_warn_redis_fallback(redis_error);
            }
        }
    }

    pthread_rwlock_wrlock(&obj->lock);
    auto svc_it = obj->instances.find(service);
    if (svc_it != obj->instances.end()) {
        if (!instance_id.empty()) {
            auto inst_it = svc_it->second.find(instance_id);
            if (inst_it != svc_it->second.end()) {
                url = inst_it->second.url;
                svc_it->second.erase(inst_it);
            }
        } else {
            auto first_it = svc_it->second.begin();
            if (first_it != svc_it->second.end()) {
                url = first_it->second.url;
            }
            svc_it->second.clear();
        }

        if (svc_it->second.empty()) {
            obj->instances.erase(svc_it);
            obj->services.erase(service);
        } else {
            obj->services[service] = svc_it->second.begin()->second.url;
        }
    } else {
        auto it = obj->services.find(service);
        if (it != obj->services.end()) {
            url = it->second;
            obj->services.erase(it);
        }
    }
    pthread_rwlock_unlock_wr(&obj->lock);

    if (removed_url != nullptr) {
        *removed_url = url;
    }
    return true;
}

static void kislayphp_discovery_list_local(php_kislayphp_discovery_t *obj, zval *return_value) {
    array_init(return_value);
    if (obj->redis_enabled) {
        std::string redis_error;
        if (!kislayphp_discovery_redis_sync_all_services(obj, &redis_error)) {
            kislayphp_discovery_warn_redis_fallback(redis_error);
        }
    }
    std::vector<std::pair<std::string, std::string>> stale_instances;
    pthread_rwlock_wrlock(&obj->lock);
    kislayphp_discovery_prune_stale_locked(obj, nullptr, &stale_instances);
    for (const auto &entry : obj->services) {
        add_assoc_string(return_value, entry.first.c_str(), entry.second.c_str());
    }
    pthread_rwlock_unlock_wr(&obj->lock);
    for (const auto &stale : stale_instances) {
        kislayphp_discovery_emit(obj, "discovery.heartbeat.timeout", stale.first, stale.second);
    }
}

static bool kislayphp_discovery_resolve_local(php_kislayphp_discovery_t *obj,
                                              const std::string &service,
                                              const std::string &hash_key,
                                              std::string *resolved_url) {
    bool found = false;
    std::string value;
    std::vector<std::pair<std::string, std::string>> stale_instances;

    if (obj->redis_enabled) {
        std::string redis_error;
        if (!kislayphp_discovery_redis_sync_service(obj, service, &redis_error)) {
            kislayphp_discovery_warn_redis_fallback(redis_error);
        }
    }

    pthread_rwlock_wrlock(&obj->lock);
    kislayphp_discovery_prune_stale_locked(obj, &service, &stale_instances);
    php_kislayphp_discovery_t::ServiceInstance selected;
    auto service_instances_it = obj->instances.find(service);
    if (service_instances_it != obj->instances.end() && !service_instances_it->second.empty()) {
        if (kislayphp_select_healthy_instance(obj, service, hash_key, &selected)) {
            value = selected.url;
            found = true;
        }
    } else {
        auto it = obj->services.find(service);
        if (it != obj->services.end()) {
            value = it->second;
            found = true;
        }
    }
    pthread_rwlock_unlock_wr(&obj->lock);

    for (const auto &stale : stale_instances) {
        kislayphp_discovery_emit(obj, "discovery.heartbeat.timeout", stale.first, stale.second);
    }

    if (resolved_url != nullptr) {
        *resolved_url = value;
    }
    return found;
}

static void kislayphp_discovery_list_instances_local(php_kislayphp_discovery_t *obj,
                                                     const std::string &service,
                                                     zval *return_value) {
    array_init(return_value);
    if (obj->redis_enabled) {
        std::string redis_error;
        if (!kislayphp_discovery_redis_sync_service(obj, service, &redis_error)) {
            kislayphp_discovery_warn_redis_fallback(redis_error);
        }
    }
    std::vector<std::pair<std::string, std::string>> stale_instances;
    pthread_rwlock_wrlock(&obj->lock);
    kislayphp_discovery_prune_stale_locked(obj, &service, &stale_instances);
    auto service_it = obj->instances.find(service);
    if (service_it != obj->instances.end()) {
        for (const auto &instance_it : service_it->second) {
            kislayphp_add_instance_array(return_value, instance_it.second);
        }
    }
    pthread_rwlock_unlock_wr(&obj->lock);
    for (const auto &stale : stale_instances) {
        kislayphp_discovery_emit(obj, "discovery.heartbeat.timeout", stale.first, stale.second);
    }
}

static void kislayphp_discovery_resolve_all_local(php_kislayphp_discovery_t *obj,
                                                  const std::string &service,
                                                  zval *return_value) {
    array_init(return_value);
    std::vector<std::pair<int, std::string>> up_instances;

    if (obj->redis_enabled) {
        std::string redis_error;
        if (!kislayphp_discovery_redis_sync_service(obj, service, &redis_error)) {
            kislayphp_discovery_warn_redis_fallback(redis_error);
        }
    }

    std::vector<std::pair<std::string, std::string>> stale_instances;
    pthread_rwlock_wrlock(&obj->lock);
    kislayphp_discovery_prune_stale_locked(obj, &service, &stale_instances);
    auto service_it = obj->instances.find(service);
    if (service_it != obj->instances.end()) {
        for (const auto &inst_it : service_it->second) {
            const auto &inst = inst_it.second;
            if (inst.status == "UP") {
                up_instances.push_back({inst.weight, inst.url});
            }
        }
    }
    pthread_rwlock_unlock_wr(&obj->lock);
    for (const auto &stale : stale_instances) {
        kislayphp_discovery_emit(obj, "discovery.heartbeat.timeout", stale.first, stale.second);
    }

    std::sort(up_instances.begin(), up_instances.end(),
              [](const std::pair<int, std::string> &a, const std::pair<int, std::string> &b) {
                  return a.first > b.first;
              });

    for (const auto &entry : up_instances) {
        add_next_index_string(return_value, entry.second.c_str());
    }
}

static bool kislayphp_discovery_heartbeat_local(php_kislayphp_discovery_t *obj,
                                                const std::string &service,
                                                const std::string &instance_id) {
    bool updated = false;
    pthread_rwlock_wrlock(&obj->lock);
    auto service_it = obj->instances.find(service);
    if (service_it != obj->instances.end() && !service_it->second.empty()) {
        if (!instance_id.empty()) {
            auto inst_it = service_it->second.find(instance_id);
            if (inst_it != service_it->second.end()) {
                inst_it->second.last_heartbeat_ms = kislayphp_now_ms();
                inst_it->second.status = "UP";
                updated = true;
            }
        } else {
            for (auto &inst_it : service_it->second) {
                inst_it.second.last_heartbeat_ms = kislayphp_now_ms();
                inst_it.second.status = "UP";
            }
            updated = true;
        }
    }
    pthread_rwlock_unlock_wr(&obj->lock);

    if (updated && obj->redis_enabled) {
        std::string redis_error;
        pthread_rwlock_wrlock(&obj->lock);
        auto service_it = obj->instances.find(service);
        if (service_it != obj->instances.end()) {
            for (const auto &inst_it : service_it->second) {
                if (!instance_id.empty() && inst_it.first != instance_id) {
                    continue;
                }
                std::string payload_json;
                if (kislayphp_discovery_instance_to_json(inst_it.second, &payload_json, &redis_error)
                    && kislayphp_discovery_redis_simple_write(obj, {"HSET", kislayphp_discovery_redis_instances_key(obj, service), inst_it.first, payload_json}, &redis_error)
                    && kislayphp_discovery_redis_simple_write(obj, {"HSET", kislayphp_discovery_redis_services_key(obj), service, inst_it.second.url}, &redis_error)) {
                    continue;
                }
                kislayphp_discovery_warn_redis_fallback(redis_error);
                break;
            }
        }
        pthread_rwlock_unlock_wr(&obj->lock);
    }
    return updated;
}

static bool kislayphp_discovery_set_status_local(php_kislayphp_discovery_t *obj,
                                                 const std::string &service,
                                                 const std::string &normalized_status,
                                                 const std::string &instance_id) {
    bool updated = false;
    pthread_rwlock_wrlock(&obj->lock);
    auto service_it = obj->instances.find(service);
    if (service_it != obj->instances.end() && !service_it->second.empty()) {
        if (!instance_id.empty()) {
            auto inst_it = service_it->second.find(instance_id);
            if (inst_it != service_it->second.end()) {
                inst_it->second.status = normalized_status;
                updated = true;
            }
        } else {
            for (auto &inst_it : service_it->second) {
                inst_it.second.status = normalized_status;
            }
            updated = true;
        }
    }
    pthread_rwlock_unlock_wr(&obj->lock);
    if (updated) {
        if (obj->redis_enabled) {
            std::string redis_error;
            pthread_rwlock_wrlock(&obj->lock);
            auto service_it = obj->instances.find(service);
            if (service_it != obj->instances.end()) {
                for (const auto &inst_it : service_it->second) {
                    if (!instance_id.empty() && inst_it.first != instance_id) {
                        continue;
                    }
                    std::string payload_json;
                    if (kislayphp_discovery_instance_to_json(inst_it.second, &payload_json, &redis_error)
                        && kislayphp_discovery_redis_simple_write(obj, {"HSET", kislayphp_discovery_redis_instances_key(obj, service), inst_it.first, payload_json}, &redis_error)
                        && kislayphp_discovery_redis_simple_write(obj, {"HSET", kislayphp_discovery_redis_services_key(obj), service, inst_it.second.url}, &redis_error)) {
                        continue;
                    }
                    kislayphp_discovery_warn_redis_fallback(redis_error);
                    break;
                }
            }
            pthread_rwlock_unlock_wr(&obj->lock);
        }
        kislayphp_discovery_emit(obj, "discovery.status.change", service, normalized_status);
    }
    return updated;
}

struct kislayphp_http_request_t {
    std::string method;
    std::string path;
    std::string body;
    std::unordered_map<std::string, std::string> params;
};

struct kislayphp_http_response_t {
    int status;
    std::string content_type;
    std::string body;
};

static std::string kislayphp_http_status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

static std::string kislayphp_http_join_path(const std::string &base_path, const std::string &path) {
    if (base_path.empty() || base_path == "/") {
        return path;
    }
    if (!path.empty() && path[0] == '/') {
        return base_path + path;
    }
    return base_path + "/" + path;
}

static std::string kislayphp_discovery_build_form_body(
    const std::unordered_map<std::string, std::string> &fields,
    const std::unordered_map<std::string, std::string> &metadata = {}) {
    std::ostringstream body;
    bool first = true;
    auto append_pair = [&](const std::string &key, const std::string &value) {
        if (!first) {
            body << "&";
        }
        first = false;
        body << kislayphp_url_encode(key) << "=" << kislayphp_url_encode(value);
    };

    for (const auto &entry : fields) {
        append_pair(entry.first, entry.second);
    }
    for (const auto &entry : metadata) {
        append_pair("meta." + entry.first, entry.second);
    }

    return body.str();
}

#ifndef _WIN32
static void kislayphp_socket_close(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

static bool kislayphp_socket_send_all(int fd, const std::string &payload, std::string *error_out = nullptr) {
    size_t sent = 0;
    while (sent < payload.size()) {
        ssize_t written = send(fd, payload.data() + sent, payload.size() - sent, 0);
        if (written <= 0) {
            if (error_out != nullptr) {
                *error_out = "Failed to write to socket";
            }
            return false;
        }
        sent += static_cast<size_t>(written);
    }
    return true;
}

struct kislayphp_redis_reply_t {
    char type = 0;
    std::string str;
    long long integer = 0;
    std::vector<kislayphp_redis_reply_t> elements;
    bool is_nil = false;
};

static bool kislayphp_socket_set_timeout(int fd, zend_long timeout_ms) {
    struct timeval tv;
    tv.tv_sec = static_cast<int>(timeout_ms / 1000);
    tv.tv_usec = static_cast<int>((timeout_ms % 1000) * 1000);
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0
        && setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

static bool kislayphp_redis_read_exact(int fd, size_t count, std::string *out, std::string *error_out = nullptr) {
    out->clear();
    out->reserve(count);
    while (out->size() < count) {
        char buffer[4096];
        size_t remaining = count - out->size();
        size_t want = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        ssize_t n = recv(fd, buffer, want, 0);
        if (n <= 0) {
            if (error_out != nullptr) {
                *error_out = "Failed to read Redis payload";
            }
            return false;
        }
        out->append(buffer, static_cast<size_t>(n));
    }
    return true;
}

static bool kislayphp_redis_read_line(int fd, std::string *line_out, std::string *error_out = nullptr) {
    line_out->clear();
    char ch = '\0';
    while (true) {
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n <= 0) {
            if (error_out != nullptr) {
                *error_out = "Failed to read Redis response";
            }
            return false;
        }
        if (ch == '\r') {
            ssize_t next = recv(fd, &ch, 1, 0);
            if (next <= 0 || ch != '\n') {
                if (error_out != nullptr) {
                    *error_out = "Malformed Redis line ending";
                }
                return false;
            }
            return true;
        }
        line_out->push_back(ch);
    }
}

static bool kislayphp_redis_read_reply(int fd, kislayphp_redis_reply_t *reply, std::string *error_out = nullptr) {
    char prefix = '\0';
    if (recv(fd, &prefix, 1, 0) <= 0) {
        if (error_out != nullptr) {
            *error_out = "Failed to read Redis reply prefix";
        }
        return false;
    }

    reply->type = prefix;
    reply->str.clear();
    reply->integer = 0;
    reply->elements.clear();
    reply->is_nil = false;

    std::string line;
    if (prefix == '+' || prefix == '-' || prefix == ':' || prefix == '$' || prefix == '*') {
        if (!kislayphp_redis_read_line(fd, &line, error_out)) {
            return false;
        }
    }

    switch (prefix) {
        case '+':
            reply->str = line;
            return true;
        case '-':
            reply->str = line;
            if (error_out != nullptr) {
                *error_out = line;
            }
            return false;
        case ':':
            reply->integer = std::strtoll(line.c_str(), nullptr, 10);
            return true;
        case '$': {
            long long len = std::strtoll(line.c_str(), nullptr, 10);
            if (len < 0) {
                reply->is_nil = true;
                return true;
            }
            if (!kislayphp_redis_read_exact(fd, static_cast<size_t>(len), &reply->str, error_out)) {
                return false;
            }
            std::string crlf;
            return kislayphp_redis_read_exact(fd, 2, &crlf, error_out) && crlf == "\r\n";
        }
        case '*': {
            long long count = std::strtoll(line.c_str(), nullptr, 10);
            if (count < 0) {
                reply->is_nil = true;
                return true;
            }
            reply->elements.reserve(static_cast<size_t>(count));
            for (long long i = 0; i < count; ++i) {
                kislayphp_redis_reply_t item;
                if (!kislayphp_redis_read_reply(fd, &item, error_out)) {
                    return false;
                }
                reply->elements.push_back(std::move(item));
            }
            return true;
        }
        default:
            if (error_out != nullptr) {
                *error_out = "Unsupported Redis reply type";
            }
            return false;
    }
}

static std::string kislayphp_redis_encode_command(const std::vector<std::string> &parts) {
    std::ostringstream out;
    out << "*" << parts.size() << "\r\n";
    for (const auto &part : parts) {
        out << "$" << part.size() << "\r\n" << part << "\r\n";
    }
    return out.str();
}

static bool kislayphp_discovery_redis_connect(php_kislayphp_discovery_t *obj, int *fd_out, std::string *error_out = nullptr) {
    if (!obj->redis_enabled) {
        return false;
    }

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = nullptr;
    std::string port = std::to_string(obj->redis_port);
    if (getaddrinfo(obj->redis_host.c_str(), port.c_str(), &hints, &result) != 0) {
        if (error_out != nullptr) {
            *error_out = "Failed to resolve Redis host";
        }
        return false;
    }

    int fd = -1;
    for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        kislayphp_socket_set_timeout(fd, obj->redis_timeout_ms);
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        kislayphp_socket_close(fd);
        fd = -1;
    }
    freeaddrinfo(result);

    if (fd < 0) {
        if (error_out != nullptr) {
            *error_out = "Failed to connect to Redis";
        }
        return false;
    }

    auto run_simple = [&](const std::vector<std::string> &parts) -> bool {
        kislayphp_redis_reply_t reply;
        std::string command = kislayphp_redis_encode_command(parts);
        if (!kislayphp_socket_send_all(fd, command, error_out)) {
            return false;
        }
        if (!kislayphp_redis_read_reply(fd, &reply, error_out)) {
            return false;
        }
        return reply.type == '+' || reply.type == ':' || reply.type == '$';
    };

    if (!obj->redis_password.empty()) {
        if (!run_simple({"AUTH", obj->redis_password})) {
            kislayphp_socket_close(fd);
            return false;
        }
    }
    if (obj->redis_db > 0) {
        if (!run_simple({"SELECT", std::to_string(obj->redis_db)})) {
            kislayphp_socket_close(fd);
            return false;
        }
    }

    *fd_out = fd;
    return true;
}

static bool kislayphp_discovery_redis_command(php_kislayphp_discovery_t *obj,
                                              const std::vector<std::string> &parts,
                                              kislayphp_redis_reply_t *reply,
                                              std::string *error_out = nullptr) {
    int fd = -1;
    if (!kislayphp_discovery_redis_connect(obj, &fd, error_out)) {
        return false;
    }
    std::string command = kislayphp_redis_encode_command(parts);
    bool ok = kislayphp_socket_send_all(fd, command, error_out)
        && kislayphp_redis_read_reply(fd, reply, error_out);
    kislayphp_socket_close(fd);
    return ok;
}

static std::string kislayphp_discovery_redis_services_key(php_kislayphp_discovery_t *obj) {
    return obj->redis_prefix + ":services";
}

static std::string kislayphp_discovery_redis_instances_key(php_kislayphp_discovery_t *obj, const std::string &service) {
    return obj->redis_prefix + ":instances:" + service;
}

static bool kislayphp_discovery_instance_to_json(const php_kislayphp_discovery_t::ServiceInstance &instance,
                                                 std::string *json_out,
                                                 std::string *error_out = nullptr) {
    zval payload;
    array_init(&payload);
    add_assoc_string(&payload, "service", instance.service_name.c_str());
    add_assoc_string(&payload, "instanceId", instance.instance_id.c_str());
    add_assoc_string(&payload, "url", instance.url.c_str());
    add_assoc_string(&payload, "status", instance.status.c_str());
    add_assoc_long(&payload, "lastHeartbeat", static_cast<zend_long>(instance.last_heartbeat_ms));
    add_assoc_long(&payload, "weight", instance.weight);

    zval metadata;
    array_init(&metadata);
    for (const auto &entry : instance.metadata) {
        add_assoc_string(&metadata, entry.first.c_str(), entry.second.c_str());
    }
    add_assoc_zval(&payload, "metadata", &metadata);

    bool ok = kislayphp_json_encode_zval(&payload, json_out, error_out);
    zval_ptr_dtor(&payload);
    return ok;
}

static bool kislayphp_discovery_instance_from_json(const std::string &json,
                                                   php_kislayphp_discovery_t::ServiceInstance *instance,
                                                   std::string *error_out = nullptr) {
    zval decoded;
    ZVAL_UNDEF(&decoded);
    if (!kislayphp_json_decode_assoc(json, &decoded, error_out)) {
        return false;
    }
    if (Z_TYPE(decoded) != IS_ARRAY) {
        zval_ptr_dtor(&decoded);
        if (error_out != nullptr) {
            *error_out = "Redis instance payload must decode to an array";
        }
        return false;
    }

    auto get_string = [&](const char *key) -> std::string {
        zval *value = zend_hash_str_find(Z_ARRVAL(decoded), key, std::strlen(key));
        if (value == nullptr) {
            return "";
        }
        zend_string *str = zval_get_string(value);
        std::string out(ZSTR_VAL(str), ZSTR_LEN(str));
        zend_string_release(str);
        return out;
    };

    instance->service_name = get_string("service");
    instance->instance_id = get_string("instanceId");
    instance->url = get_string("url");
    instance->status = get_string("status");

    zval *heartbeat = zend_hash_str_find(Z_ARRVAL(decoded), "lastHeartbeat", sizeof("lastHeartbeat") - 1);
    instance->last_heartbeat_ms = heartbeat ? zval_get_long(heartbeat) : 0;
    zval *weight = zend_hash_str_find(Z_ARRVAL(decoded), "weight", sizeof("weight") - 1);
    instance->weight = weight ? std::max(1, static_cast<int>(zval_get_long(weight))) : 1;

    instance->metadata.clear();
    zval *metadata = zend_hash_str_find(Z_ARRVAL(decoded), "metadata", sizeof("metadata") - 1);
    if (metadata != nullptr && Z_TYPE_P(metadata) == IS_ARRAY) {
        zval *entry = nullptr;
        zend_string *key = nullptr;
        ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(metadata), key, entry) {
            if (key == nullptr || entry == nullptr) {
                continue;
            }
            zend_string *val = zval_get_string(entry);
            instance->metadata[std::string(ZSTR_VAL(key), ZSTR_LEN(key))] = std::string(ZSTR_VAL(val), ZSTR_LEN(val));
            zend_string_release(val);
        } ZEND_HASH_FOREACH_END();
    }

    zval_ptr_dtor(&decoded);
    return !instance->service_name.empty() && !instance->instance_id.empty() && !instance->url.empty();
}

static bool kislayphp_discovery_redis_hgetall(php_kislayphp_discovery_t *obj,
                                              const std::string &key,
                                              std::unordered_map<std::string, std::string> *pairs,
                                              std::string *error_out = nullptr) {
    kislayphp_redis_reply_t reply;
    if (!kislayphp_discovery_redis_command(obj, {"HGETALL", key}, &reply, error_out)) {
        return false;
    }
    if (reply.type != '*' || (reply.elements.size() % 2) != 0) {
        if (error_out != nullptr) {
            *error_out = "Unexpected Redis HGETALL reply";
        }
        return false;
    }
    pairs->clear();
    for (size_t i = 0; i < reply.elements.size(); i += 2) {
        (*pairs)[reply.elements[i].str] = reply.elements[i + 1].str;
    }
    return true;
}

static bool kislayphp_discovery_redis_hget(php_kislayphp_discovery_t *obj,
                                           const std::string &key,
                                           const std::string &field,
                                           std::string *value_out,
                                           bool *exists_out,
                                           std::string *error_out = nullptr) {
    kislayphp_redis_reply_t reply;
    if (!kislayphp_discovery_redis_command(obj, {"HGET", key, field}, &reply, error_out)) {
        return false;
    }
    if (reply.type != '$') {
        if (error_out != nullptr) {
            *error_out = "Unexpected Redis HGET reply";
        }
        return false;
    }
    *exists_out = !reply.is_nil;
    value_out->assign(reply.str);
    return true;
}

static bool kislayphp_discovery_redis_hlen(php_kislayphp_discovery_t *obj,
                                           const std::string &key,
                                           long long *count_out,
                                           std::string *error_out = nullptr) {
    kislayphp_redis_reply_t reply;
    if (!kislayphp_discovery_redis_command(obj, {"HLEN", key}, &reply, error_out)) {
        return false;
    }
    if (reply.type != ':') {
        if (error_out != nullptr) {
            *error_out = "Unexpected Redis HLEN reply";
        }
        return false;
    }
    *count_out = reply.integer;
    return true;
}

static bool kislayphp_discovery_redis_simple_write(php_kislayphp_discovery_t *obj,
                                                   const std::vector<std::string> &parts,
                                                   std::string *error_out = nullptr) {
    kislayphp_redis_reply_t reply;
    if (!kislayphp_discovery_redis_command(obj, parts, &reply, error_out)) {
        return false;
    }
    return reply.type == '+' || reply.type == ':' || reply.type == '$';
}

static bool kislayphp_discovery_redis_sync_service(php_kislayphp_discovery_t *obj,
                                                   const std::string &service,
                                                   std::string *error_out = nullptr) {
    if (!obj->redis_enabled) {
        return false;
    }

    std::unordered_map<std::string, std::string> raw_instances;
    if (!kislayphp_discovery_redis_hgetall(obj,
                                           kislayphp_discovery_redis_instances_key(obj, service),
                                           &raw_instances,
                                           error_out)) {
        return false;
    }

    std::unordered_map<std::string, php_kislayphp_discovery_t::ServiceInstance> fresh_instances;
    std::vector<std::pair<std::string, std::string>> stale_instances;
    const long long now_ms = kislayphp_now_ms();
    for (const auto &entry : raw_instances) {
        php_kislayphp_discovery_t::ServiceInstance instance;
        if (!kislayphp_discovery_instance_from_json(entry.second, &instance, error_out)) {
            continue;
        }
        const bool is_fresh = (now_ms - instance.last_heartbeat_ms) <= static_cast<long long>(obj->heartbeat_timeout_ms);
        if (!is_fresh) {
            stale_instances.push_back({service, instance.url});
            (void)kislayphp_discovery_redis_simple_write(obj,
                                                         {"HDEL", kislayphp_discovery_redis_instances_key(obj, service), entry.first},
                                                         nullptr);
            continue;
        }
        fresh_instances[entry.first] = instance;
    }

    if (fresh_instances.empty()) {
        (void)kislayphp_discovery_redis_simple_write(obj, {"HDEL", kislayphp_discovery_redis_services_key(obj), service}, nullptr);
        (void)kislayphp_discovery_redis_simple_write(obj, {"DEL", kislayphp_discovery_redis_instances_key(obj, service)}, nullptr);
        pthread_rwlock_wrlock(&obj->lock);
        obj->instances.erase(service);
        obj->services.erase(service);
        obj->rr_index.erase(service);
        pthread_rwlock_unlock_wr(&obj->lock);
    } else {
        std::string primary_url = fresh_instances.begin()->second.url;
        (void)kislayphp_discovery_redis_simple_write(obj, {"HSET", kislayphp_discovery_redis_services_key(obj), service, primary_url}, nullptr);
        pthread_rwlock_wrlock(&obj->lock);
        obj->instances[service] = fresh_instances;
        obj->services[service] = primary_url;
        pthread_rwlock_unlock_wr(&obj->lock);
    }

    for (const auto &stale : stale_instances) {
        kislayphp_discovery_emit(obj, "discovery.heartbeat.timeout", stale.first, stale.second);
    }
    return true;
}

static bool kislayphp_discovery_redis_sync_all_services(php_kislayphp_discovery_t *obj,
                                                        std::string *error_out = nullptr) {
    if (!obj->redis_enabled) {
        return false;
    }

    std::unordered_map<std::string, std::string> services;
    if (!kislayphp_discovery_redis_hgetall(obj, kislayphp_discovery_redis_services_key(obj), &services, error_out)) {
        return false;
    }

    pthread_rwlock_wrlock(&obj->lock);
    obj->services.clear();
    obj->instances.clear();
    obj->rr_index.clear();
    pthread_rwlock_unlock_wr(&obj->lock);

    for (const auto &entry : services) {
        std::string ignored_error;
        if (!kislayphp_discovery_redis_sync_service(obj, entry.first, &ignored_error) && error_out != nullptr && error_out->empty()) {
            *error_out = ignored_error;
        }
    }
    return true;
}

static void kislayphp_discovery_warn_redis_fallback(const std::string &error) {
    php_error_docref(nullptr, E_WARNING, "Redis discovery backend failed (%s); using in-memory registry state", error.c_str());
}

static bool kislayphp_http_parse_response(const std::string &raw,
                                          int *status_out,
                                          std::string *body_out,
                                          std::string *error_out = nullptr) {
    size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        if (error_out != nullptr) {
            *error_out = "Malformed HTTP response";
        }
        return false;
    }

    size_t line_end = raw.find("\r\n");
    if (line_end == std::string::npos) {
        if (error_out != nullptr) {
            *error_out = "Malformed HTTP status line";
        }
        return false;
    }

    std::string status_line = raw.substr(0, line_end);
    size_t first_space = status_line.find(' ');
    size_t second_space = status_line.find(' ', first_space == std::string::npos ? 0 : first_space + 1);
    if (first_space == std::string::npos || second_space == std::string::npos) {
        if (error_out != nullptr) {
            *error_out = "Invalid HTTP status line";
        }
        return false;
    }

    if (status_out != nullptr) {
        *status_out = std::atoi(status_line.substr(first_space + 1, second_space - first_space - 1).c_str());
    }
    if (body_out != nullptr) {
        *body_out = raw.substr(header_end + 4);
    }
    return true;
}

static bool kislayphp_http_request(const std::string &base_url,
                                   const std::string &method,
                                   const std::string &path,
                                   const std::string &body,
                                   int *status_out,
                                   std::string *response_body,
                                   std::string *error_out = nullptr) {
    kislayphp_http_endpoint_t endpoint;
    if (!kislayphp_parse_http_endpoint(base_url, &endpoint, error_out)) {
        return false;
    }

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = nullptr;
    std::string port = std::to_string(endpoint.port);
    if (getaddrinfo(endpoint.host.c_str(), port.c_str(), &hints, &result) != 0) {
        if (error_out != nullptr) {
            *error_out = "Failed to resolve registry host";
        }
        return false;
    }

    int fd = -1;
    for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        kislayphp_socket_close(fd);
        fd = -1;
    }
    freeaddrinfo(result);

    if (fd < 0) {
        if (error_out != nullptr) {
            *error_out = "Failed to connect to registry";
        }
        return false;
    }

    std::string request_path = kislayphp_http_join_path(endpoint.base_path, path);
    std::ostringstream request;
    request << method << " " << request_path << " HTTP/1.1\r\n";
    request << "Host: " << endpoint.host << ":" << endpoint.port << "\r\n";
    request << "Connection: close\r\n";
    if (method == "POST") {
        request << "Content-Type: application/x-www-form-urlencoded\r\n";
        request << "Content-Length: " << body.size() << "\r\n";
    }
    request << "\r\n";
    if (method == "POST") {
        request << body;
    }

    std::string raw_request = request.str();
    if (!kislayphp_socket_send_all(fd, raw_request, error_out)) {
        kislayphp_socket_close(fd);
        return false;
    }

    std::string raw_response;
    char buffer[4096];
    while (true) {
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n < 0) {
            kislayphp_socket_close(fd);
            if (error_out != nullptr) {
                *error_out = "Failed to read registry response";
            }
            return false;
        }
        if (n == 0) {
            break;
        }
        raw_response.append(buffer, static_cast<size_t>(n));
    }
    kislayphp_socket_close(fd);

    return kislayphp_http_parse_response(raw_response, status_out, response_body, error_out);
}

static bool kislayphp_http_read_request(int client_fd,
                                        kislayphp_http_request_t *request,
                                        std::string *error_out = nullptr) {
    std::string raw;
    char buffer[4096];
    size_t header_end = std::string::npos;
    while ((header_end = raw.find("\r\n\r\n")) == std::string::npos) {
        ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            if (error_out != nullptr) {
                *error_out = "Failed to read request headers";
            }
            return false;
        }
        raw.append(buffer, static_cast<size_t>(n));
        if (raw.size() > 1024 * 1024) {
            if (error_out != nullptr) {
                *error_out = "Request headers too large";
            }
            return false;
        }
    }

    std::string header_block = raw.substr(0, header_end);
    std::string body = raw.substr(header_end + 4);
    size_t line_end = header_block.find("\r\n");
    if (line_end == std::string::npos) {
        if (error_out != nullptr) {
            *error_out = "Malformed request line";
        }
        return false;
    }

    std::string request_line = header_block.substr(0, line_end);
    size_t first_space = request_line.find(' ');
    size_t second_space = request_line.find(' ', first_space == std::string::npos ? 0 : first_space + 1);
    if (first_space == std::string::npos || second_space == std::string::npos) {
        if (error_out != nullptr) {
            *error_out = "Invalid request line";
        }
        return false;
    }

    request->method = request_line.substr(0, first_space);
    std::string target = request_line.substr(first_space + 1, second_space - first_space - 1);

    size_t content_length = 0;
    std::istringstream header_stream(header_block.substr(line_end + 2));
    std::string header_line;
    while (std::getline(header_stream, header_line)) {
        if (!header_line.empty() && header_line.back() == '\r') {
            header_line.pop_back();
        }
        size_t colon = header_line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string name = header_line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        std::string value = header_line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') {
            value.erase(value.begin());
        }
        if (name == "content-length") {
            content_length = static_cast<size_t>(std::strtoull(value.c_str(), nullptr, 10));
        }
    }

    while (body.size() < content_length) {
        ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            if (error_out != nullptr) {
                *error_out = "Failed to read request body";
            }
            return false;
        }
        body.append(buffer, static_cast<size_t>(n));
    }
    if (body.size() > content_length) {
        body.resize(content_length);
    }
    request->body = body;

    std::string query;
    size_t qmark = target.find('?');
    if (qmark == std::string::npos) {
        request->path = target;
    } else {
        request->path = target.substr(0, qmark);
        query = target.substr(qmark + 1);
    }

    std::unordered_map<std::string, std::string> params;
    if (!query.empty()) {
        kislayphp_parse_form_pairs(query, params);
    }
    if (request->method == "POST" && !body.empty()) {
        std::unordered_map<std::string, std::string> body_params;
        kislayphp_parse_form_pairs(body, body_params);
        params.insert(body_params.begin(), body_params.end());
    }
    request->params = params;
    return true;
}

static bool kislayphp_http_send_response(int client_fd,
                                         int status,
                                         const std::string &content_type,
                                         const std::string &body,
                                         std::string *error_out = nullptr) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << " " << kislayphp_http_status_text(status) << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << body;
    return kislayphp_socket_send_all(client_fd, response.str(), error_out);
}
#else
static bool kislayphp_http_request(const std::string &base_url,
                                   const std::string &method,
                                   const std::string &path,
                                   const std::string &body,
                                   int *status_out,
                                   std::string *response_body,
                                   std::string *error_out = nullptr) {
    (void) base_url; (void) method; (void) path; (void) body; (void) status_out; (void) response_body;
    if (error_out != nullptr) {
        *error_out = "Discovery standalone HTTP mode is not supported on Windows yet";
    }
    return false;
}

static bool kislayphp_http_read_request(int client_fd,
                                        kislayphp_http_request_t *request,
                                        std::string *error_out = nullptr) {
    (void) client_fd; (void) request;
    if (error_out != nullptr) {
        *error_out = "Discovery standalone HTTP mode is not supported on Windows yet";
    }
    return false;
}

static bool kislayphp_http_send_response(int client_fd,
                                         int status,
                                         const std::string &content_type,
                                         const std::string &body,
                                         std::string *error_out = nullptr) {
    (void) client_fd; (void) status; (void) content_type; (void) body;
    if (error_out != nullptr) {
        *error_out = "Discovery standalone HTTP mode is not supported on Windows yet";
    }
    return false;
}
#endif

static bool kislayphp_discovery_server_handle_request(php_kislayphp_discovery_t *obj,
                                                      const kislayphp_http_request_t &request,
                                                      kislayphp_http_response_t *response,
                                                      std::string *error_out = nullptr) {
    if (response == nullptr) {
        if (error_out != nullptr) {
            *error_out = "internal response object is null";
        }
        return false;
    }

    response->status = 200;
    response->content_type = "application/json";
    response->body = "{\"ok\":true}";

    if (request.method == "GET" && request.path == "/health") {
        response->content_type = "text/plain";
        response->body = "OK";
        return true;
    }

    if (request.method == "GET" && request.path == "/list") {
        zval payload;
        ZVAL_UNDEF(&payload);
        kislayphp_discovery_list_local(obj, &payload);
        const bool encoded = kislayphp_json_encode_zval(&payload, &response->body, error_out);
        zval_ptr_dtor(&payload);
        if (!encoded) {
            response->status = 500;
            response->body = "{\"error\":\"json_encode failed\"}";
        }
        return encoded;
    }

    if (request.method == "GET" && request.path == "/instances") {
        auto name_it = request.params.find("name");
        if (name_it == request.params.end() || name_it->second.empty()) {
            response->status = 400;
            response->body = "{\"error\":\"name is required\"}";
            return true;
        }

        zval payload;
        ZVAL_UNDEF(&payload);
        kislayphp_discovery_list_instances_local(obj, name_it->second, &payload);
        const bool encoded = kislayphp_json_encode_zval(&payload, &response->body, error_out);
        zval_ptr_dtor(&payload);
        if (!encoded) {
            response->status = 500;
            response->body = "{\"error\":\"json_encode failed\"}";
        }
        return encoded;
    }

    if (request.method == "GET" && request.path == "/resolve-all") {
        auto name_it = request.params.find("name");
        if (name_it == request.params.end() || name_it->second.empty()) {
            response->status = 400;
            response->body = "{\"error\":\"name is required\"}";
            return true;
        }

        zval payload;
        ZVAL_UNDEF(&payload);
        kislayphp_discovery_resolve_all_local(obj, name_it->second, &payload);
        const bool encoded = kislayphp_json_encode_zval(&payload, &response->body, error_out);
        zval_ptr_dtor(&payload);
        if (!encoded) {
            response->status = 500;
            response->body = "{\"error\":\"json_encode failed\"}";
        }
        return encoded;
    }

    if (request.method == "GET" && request.path == "/resolve") {
        auto name_it = request.params.find("name");
        if (name_it == request.params.end() || name_it->second.empty()) {
            response->status = 400;
            response->body = "{\"error\":\"name is required\"}";
            return true;
        }
        std::string hash_key;
        auto hash_it = request.params.find("hashKey");
        if (hash_it != request.params.end()) {
            hash_key = hash_it->second;
        }

        std::string resolved;
        if (!kislayphp_discovery_resolve_local(obj, name_it->second, hash_key, &resolved)) {
            response->status = 404;
            response->body = "{\"error\":\"service not found\"}";
            return true;
        }

        response->content_type = "text/plain";
        response->body = resolved;
        return true;
    }

    if (request.method == "POST" && request.path == "/register") {
        auto name_it = request.params.find("name");
        auto url_it = request.params.find("url");
        if (name_it == request.params.end() || name_it->second.empty() ||
            url_it == request.params.end() || url_it->second.empty()) {
            response->status = 400;
            response->body = "{\"error\":\"name and url are required\"}";
            return true;
        }

        std::string instance_id = url_it->second;
        auto instance_it = request.params.find("instanceId");
        if (instance_it != request.params.end() && !instance_it->second.empty()) {
            instance_id = instance_it->second;
        }

        std::unordered_map<std::string, std::string> metadata;
        kislayphp_discovery_metadata_from_params(request.params, metadata);
        if (!kislayphp_discovery_register_local(obj, name_it->second, url_it->second, metadata, instance_id)) {
            response->status = 409;
            response->body = "{\"error\":\"maximum instances per service exceeded\"}";
            return true;
        }
        kislayphp_discovery_emit(obj, "discovery.register", name_it->second, url_it->second);
        response->body = "{\"ok\":true}";
        return true;
    }

    if (request.method == "POST" && request.path == "/deregister") {
        auto name_it = request.params.find("name");
        if (name_it == request.params.end() || name_it->second.empty()) {
            response->status = 400;
            response->body = "{\"error\":\"name is required\"}";
            return true;
        }

        std::string instance_id;
        auto instance_it = request.params.find("instanceId");
        if (instance_it != request.params.end()) {
            instance_id = instance_it->second;
        }

        std::string removed_url;
        kislayphp_discovery_deregister_local(obj, name_it->second, instance_id, &removed_url);
        if (!removed_url.empty()) {
            kislayphp_discovery_emit(obj, "discovery.deregister", name_it->second, removed_url);
        }
        response->body = "{\"ok\":true}";
        return true;
    }

    if (request.method == "POST" && request.path == "/heartbeat") {
        auto name_it = request.params.find("name");
        if (name_it == request.params.end() || name_it->second.empty()) {
            response->status = 400;
            response->body = "{\"error\":\"name is required\"}";
            return true;
        }

        std::string instance_id;
        auto instance_it = request.params.find("instanceId");
        if (instance_it != request.params.end()) {
            instance_id = instance_it->second;
        }

        const bool ok = kislayphp_discovery_heartbeat_local(obj, name_it->second, instance_id);
        response->status = ok ? 200 : 404;
        response->body = ok ? "{\"ok\":true}" : "{\"error\":\"service not found\"}";
        return true;
    }

    if (request.method == "POST" && request.path == "/status") {
        auto name_it = request.params.find("name");
        auto status_it = request.params.find("status");
        if (name_it == request.params.end() || name_it->second.empty() ||
            status_it == request.params.end() || status_it->second.empty()) {
            response->status = 400;
            response->body = "{\"error\":\"name and status are required\"}";
            return true;
        }

        std::string normalized = kislayphp_upper(status_it->second);
        if (!kislayphp_is_valid_status(normalized)) {
            response->status = 400;
            response->body = "{\"error\":\"invalid status\"}";
            return true;
        }

        std::string instance_id;
        auto instance_it = request.params.find("instanceId");
        if (instance_it != request.params.end()) {
            instance_id = instance_it->second;
        }

        const bool ok = kislayphp_discovery_set_status_local(obj, name_it->second, normalized, instance_id);
        response->status = ok ? 200 : 404;
        response->body = ok ? "{\"ok\":true}" : "{\"error\":\"service not found\"}";
        return true;
    }

    response->status = 404;
    response->body = "{\"error\":\"not found\"}";
    return true;
}

#ifndef _WIN32
static bool kislayphp_discovery_server_run(php_kislayphp_discovery_t *obj,
                                           std::string *error_out = nullptr) {
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *result = nullptr;
    std::string port = std::to_string(static_cast<int>(obj->listen_port));
    if (getaddrinfo(obj->listen_host.c_str(), port.c_str(), &hints, &result) != 0) {
        if (error_out != nullptr) {
            *error_out = "Failed to resolve bind address";
        }
        return false;
    }

    int server_fd = -1;
    for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        server_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (server_fd < 0) {
            continue;
        }
        int reuse = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (bind(server_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        kislayphp_socket_close(server_fd);
        server_fd = -1;
    }
    freeaddrinfo(result);

    if (server_fd < 0) {
        if (error_out != nullptr) {
            *error_out = "Failed to bind registry server socket";
        }
        return false;
    }

    if (listen(server_fd, 128) != 0) {
        kislayphp_socket_close(server_fd);
        if (error_out != nullptr) {
            *error_out = "Failed to listen on registry server socket";
        }
        return false;
    }

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            continue;
        }

        kislayphp_http_request_t request;
        kislayphp_http_response_t response;
        std::string request_error;
        if (!kislayphp_http_read_request(client_fd, &request, &request_error)) {
            kislayphp_http_send_response(client_fd, 400, "application/json", "{\"error\":\"bad request\"}", nullptr);
            kislayphp_socket_close(client_fd);
            continue;
        }

        std::string handler_error;
        if (!kislayphp_discovery_server_handle_request(obj, request, &response, &handler_error)) {
            kislayphp_http_send_response(client_fd, 500, "application/json", "{\"error\":\"internal error\"}", nullptr);
            kislayphp_socket_close(client_fd);
            continue;
        }

        kislayphp_http_send_response(client_fd, response.status, response.content_type, response.body, nullptr);
        kislayphp_socket_close(client_fd);
    }

    return true;
}
#else
static bool kislayphp_discovery_server_run(php_kislayphp_discovery_t *obj,
                                           std::string *error_out = nullptr) {
    (void) obj;
    if (error_out != nullptr) {
        *error_out = "Discovery standalone server mode is not supported on Windows yet";
    }
    return false;
}
#endif

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_void, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_construct, 0, 0, 0)
    ZEND_ARG_TYPE_INFO(0, baseUrl, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_listen, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, host, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, port, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_register, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, url, IS_STRING, 0)
    ZEND_ARG_ARRAY_INFO(0, metadata, 1)
    ZEND_ARG_TYPE_INFO(0, instanceId, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_deregister, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, instanceId, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_resolve, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_kislayphp_discovery_resolve_all, 0, 1, IS_ARRAY, 0)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_set_balancer, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, type, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_get_weight, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, url, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_set_bus, 0, 0, 1)
    ZEND_ARG_OBJ_INFO(0, bus, stdClass, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_set_client, 0, 0, 1)
    ZEND_ARG_OBJ_INFO(0, client, Kislay\\Discovery\\ClientInterface, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_set_heartbeat_timeout, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, milliseconds, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_client_register, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, url, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_client_deregister, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_client_resolve, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_list_instances, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_heartbeat, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, instanceId, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_set_status, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, status, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, instanceId, IS_STRING, 1)
ZEND_END_ARG_INFO()

PHP_METHOD(KislayPHPDiscovery, __construct) {
    char *base_url = nullptr;
    size_t base_url_len = 0;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(base_url, base_url_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    if (base_url != nullptr && base_url_len > 0) {
        obj->remote_base_url.assign(base_url, base_url_len);
        obj->has_remote_base_url = true;
    }
}

PHP_METHOD(KislayPHPDiscovery, setClient) {
    zval *client = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(client)
    ZEND_PARSE_PARAMETERS_END();

    if (client == nullptr || Z_TYPE_P(client) != IS_OBJECT) {
        zend_throw_exception(zend_ce_exception, "Client must be an object", 0);
        RETURN_FALSE;
    }

    if (!instanceof_function(Z_OBJCE_P(client), kislayphp_discovery_client_ce)) {
        zend_throw_exception(zend_ce_exception, "Client must implement Kislay\\Discovery\\ClientInterface", 0);
        RETURN_FALSE;
    }

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval_ptr_dtor(&obj->client);
        obj->has_client = false;
    }
    ZVAL_COPY(&obj->client, client);
    obj->has_client = true;
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPDiscovery, listen) {
    char *host = nullptr;
    size_t host_len = 0;
    zend_long port = 0;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(host, host_len)
        Z_PARAM_LONG(port)
    ZEND_PARSE_PARAMETERS_END();

    if (port <= 0 || port > 65535) {
        zend_throw_exception(zend_ce_exception, "Port must be between 1 and 65535", 0);
        RETURN_FALSE;
    }

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    obj->listen_host.assign(host, host_len);
    obj->listen_port = port;
    obj->has_listen_config = true;
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPDiscovery, run) {
    ZEND_PARSE_PARAMETERS_NONE();

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    if (!obj->has_listen_config || obj->listen_port <= 0) {
        zend_throw_exception(zend_ce_exception, "Call listen(host, port) before run()", 0);
        RETURN_FALSE;
    }
    if (obj->has_remote_base_url) {
        zend_throw_exception(zend_ce_exception, "Remote registry clients cannot run a local server", 0);
        RETURN_FALSE;
    }

    std::string error;
    if (!kislayphp_discovery_server_run(obj, &error)) {
        if (EG(exception) == nullptr) {
            zend_throw_exception(zend_ce_exception, error.c_str(), 0);
        }
        RETURN_FALSE;
    }

    RETURN_TRUE;
}

PHP_METHOD(KislayPHPDiscovery, register) {
    char *name = nullptr;
    size_t name_len = 0;
    char *url = nullptr;
    size_t url_len = 0;
    zval *metadata_zv = nullptr;
    char *instance_id = nullptr;
    size_t instance_id_len = 0;
    ZEND_PARSE_PARAMETERS_START(2, 4)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_STRING(url, url_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_EX(metadata_zv, 1, 0)
        Z_PARAM_STRING(instance_id, instance_id_len)
    ZEND_PARSE_PARAMETERS_END();

    std::string service(name, name_len);
    std::string service_url(url, url_len);
    std::string instance = (instance_id != nullptr && instance_id_len > 0)
        ? std::string(instance_id, instance_id_len)
        : service_url;

    std::unordered_map<std::string, std::string> metadata;
    kislayphp_parse_metadata_array(metadata_zv, metadata);

    zval *weight_zv = (metadata_zv != nullptr && Z_TYPE_P(metadata_zv) == IS_ARRAY)
        ? zend_hash_str_find(Z_ARRVAL_P(metadata_zv), "weight", sizeof("weight") - 1)
        : nullptr;
    int inst_weight = weight_zv ? (int)zval_get_long(weight_zv) : 1;
    if (inst_weight < 1) inst_weight = 1;

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));

    if (obj->has_remote_base_url) {
        std::unordered_map<std::string, std::string> fields;
        fields["name"] = service;
        fields["url"] = service_url;
        if (!instance.empty()) {
            fields["instanceId"] = instance;
        }

        int status = 0;
        std::string response_body;
        std::string error;
        const std::string body = kislayphp_discovery_build_form_body(fields, metadata);
        if (!kislayphp_http_request(obj->remote_base_url, "POST", "/register", body, &status, &response_body, &error)) {
            zend_throw_exception(zend_ce_exception, error.c_str(), 0);
            RETURN_FALSE;
        }
        if (status < 200 || status >= 300) {
            std::string message = "Remote discovery register failed with HTTP " + std::to_string(status);
            if (!response_body.empty()) {
                message += ": " + response_body;
            }
            zend_throw_exception(zend_ce_exception, message.c_str(), 0);
            RETURN_FALSE;
        }
        RETURN_TRUE;
    }

    if (!kislayphp_discovery_register_local(obj, service, service_url, metadata, instance)) {
        zend_throw_exception(zend_ce_exception, "Maximum instances per service exceeded", 0);
        RETURN_FALSE;
    }

#ifdef KISLAYPHP_RPC
    if (!obj->has_client && kislayphp_rpc_enabled()) {
        std::string error;
        if (kislayphp_rpc_discovery_register(service, instance, service_url, metadata, &error)) {
            kislayphp_discovery_emit(obj, "discovery.register", service, service_url);
            RETURN_TRUE;
        }
    }
#endif

    if (obj->has_client) {
        zval retval;
        ZVAL_UNDEF(&retval);
        bool called = false;

        if (kislayphp_object_has_method(&obj->client, "registerInstance")) {
            zval args[4];
            ZVAL_STRINGL(&args[0], name, name_len);
            ZVAL_STRINGL(&args[1], url, url_len);
            if (metadata_zv != nullptr && Z_TYPE_P(metadata_zv) == IS_ARRAY) {
                ZVAL_COPY(&args[2], metadata_zv);
            } else {
                array_init(&args[2]);
            }
            if (instance_id != nullptr && instance_id_len > 0) {
                ZVAL_STRINGL(&args[3], instance_id, instance_id_len);
            } else {
                ZVAL_NULL(&args[3]);
            }

            called = kislayphp_call_object_method(&obj->client, "registerInstance", 4, args, &retval);
            zval_ptr_dtor(&args[0]);
            zval_ptr_dtor(&args[1]);
            zval_ptr_dtor(&args[2]);
            zval_ptr_dtor(&args[3]);
        }

        if (!called) {
            zval args[2];
            ZVAL_STRINGL(&args[0], name, name_len);
            ZVAL_STRINGL(&args[1], url, url_len);
            called = kislayphp_call_object_method(&obj->client, "register", 2, args, &retval);
            zval_ptr_dtor(&args[0]);
            zval_ptr_dtor(&args[1]);
        }

        if (!called || EG(exception) != nullptr) {
            if (!Z_ISUNDEF(retval)) {
                zval_ptr_dtor(&retval);
            }
            RETURN_FALSE;
        }

        if (!Z_ISUNDEF(retval) && Z_TYPE(retval) == IS_FALSE) {
            zval_ptr_dtor(&retval);
            RETURN_FALSE;
        }
        if (!Z_ISUNDEF(retval)) {
            zval_ptr_dtor(&retval);
        }
        kislayphp_discovery_emit(obj, "discovery.register", service, service_url);
        RETURN_TRUE;
    }

    kislayphp_discovery_emit(obj, "discovery.register", service, service_url);
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPDiscovery, deregister) {
    char *name = nullptr;
    size_t name_len = 0;
    char *instance_id = nullptr;
    size_t instance_id_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(instance_id, instance_id_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    std::string key(name, name_len);
    std::string url;

    if (obj->has_remote_base_url) {
        std::unordered_map<std::string, std::string> fields;
        fields["name"] = key;
        if (instance_id != nullptr && instance_id_len > 0) {
            fields["instanceId"] = std::string(instance_id, instance_id_len);
        }

        int status = 0;
        std::string response_body;
        std::string error;
        const std::string body = kislayphp_discovery_build_form_body(fields);
        if (!kislayphp_http_request(obj->remote_base_url, "POST", "/deregister", body, &status, &response_body, &error)) {
            zend_throw_exception(zend_ce_exception, error.c_str(), 0);
            RETURN_FALSE;
        }
        if (status < 200 || status >= 300) {
            std::string message = "Remote discovery deregister failed with HTTP " + std::to_string(status);
            if (!response_body.empty()) {
                message += ": " + response_body;
            }
            zend_throw_exception(zend_ce_exception, message.c_str(), 0);
            RETURN_FALSE;
        }
        RETURN_TRUE;
    }

    if (obj->has_client) {
        zval retval;
        ZVAL_UNDEF(&retval);
        zval resolve_ret;
        ZVAL_UNDEF(&resolve_ret);

        if (kislayphp_object_has_method(&obj->client, "resolve")) {
            zval resolve_args[1];
            ZVAL_STRINGL(&resolve_args[0], name, name_len);
            bool resolved = kislayphp_call_object_method(&obj->client, "resolve", 1, resolve_args, &resolve_ret);
            zval_ptr_dtor(&resolve_args[0]);
            if (resolved && Z_TYPE(resolve_ret) == IS_STRING) {
                url.assign(Z_STRVAL(resolve_ret), Z_STRLEN(resolve_ret));
            }
            if (!Z_ISUNDEF(resolve_ret)) {
                zval_ptr_dtor(&resolve_ret);
            }
        }

        bool called = false;
        if (instance_id != nullptr && instance_id_len > 0 &&
            kislayphp_object_has_method(&obj->client, "deregisterInstance")) {
            zval args[2];
            ZVAL_STRINGL(&args[0], name, name_len);
            ZVAL_STRINGL(&args[1], instance_id, instance_id_len);
            called = kislayphp_call_object_method(&obj->client, "deregisterInstance", 2, args, &retval);
            zval_ptr_dtor(&args[0]);
            zval_ptr_dtor(&args[1]);
        }
        if (!called) {
            zval args[1];
            ZVAL_STRINGL(&args[0], name, name_len);
            called = kislayphp_call_object_method(&obj->client, "deregister", 1, args, &retval);
            zval_ptr_dtor(&args[0]);
        }

        if (!called || EG(exception) != nullptr) {
            if (!Z_ISUNDEF(retval)) {
                zval_ptr_dtor(&retval);
            }
            RETURN_FALSE;
        }

        if (!Z_ISUNDEF(retval) && Z_TYPE(retval) == IS_FALSE) {
            zval_ptr_dtor(&retval);
            RETURN_FALSE;
        }
        if (!Z_ISUNDEF(retval)) {
            zval_ptr_dtor(&retval);
        }

        if (!url.empty()) {
            kislayphp_discovery_emit(obj, "discovery.deregister", key, url);
        }
        RETURN_TRUE;
    }

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string error;
        std::string instance_value = (instance_id != nullptr && instance_id_len > 0)
            ? std::string(instance_id, instance_id_len)
            : std::string();
        if (kislayphp_rpc_discovery_deregister(key, instance_value.empty() ? std::string() : instance_value, &error)) {
            if (!url.empty()) {
                kislayphp_discovery_emit(obj, "discovery.deregister", key, url);
            }
            RETURN_TRUE;
        }
    }
#endif

    pthread_rwlock_wrlock(&obj->lock);
    auto svc_it = obj->instances.find(key);
    if (svc_it != obj->instances.end()) {
        if (instance_id != nullptr && instance_id_len > 0) {
            std::string instance_key(instance_id, instance_id_len);
            auto inst_it = svc_it->second.find(instance_key);
            if (inst_it != svc_it->second.end()) {
                url = inst_it->second.url;
                svc_it->second.erase(inst_it);
            }
        } else {
            auto first_it = svc_it->second.begin();
            if (first_it != svc_it->second.end()) {
                url = first_it->second.url;
            }
            svc_it->second.clear();
        }

        if (svc_it->second.empty()) {
            obj->instances.erase(svc_it);
            obj->services.erase(key);
        } else {
            obj->services[key] = svc_it->second.begin()->second.url;
        }
    } else {
        auto it = obj->services.find(key);
        if (it != obj->services.end()) {
            url = it->second;
            obj->services.erase(it);
        }
    }
    pthread_rwlock_unlock_wr(&obj->lock);
    if (!url.empty()) {
        kislayphp_discovery_emit(obj, "discovery.deregister", key, url);
    }
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPDiscovery, list) {
    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_remote_base_url) {
        int status = 0;
        std::string response_body;
        std::string error;
        if (!kislayphp_http_request(obj->remote_base_url, "GET", "/list", "", &status, &response_body, &error)) {
            zend_throw_exception(zend_ce_exception, error.c_str(), 0);
            RETURN_THROWS();
        }
        if (status < 200 || status >= 300) {
            std::string message = "Remote discovery list failed with HTTP " + std::to_string(status);
            if (!response_body.empty()) {
                message += ": " + response_body;
            }
            zend_throw_exception(zend_ce_exception, message.c_str(), 0);
            RETURN_THROWS();
        }
        if (!kislayphp_json_decode_assoc(response_body, return_value, &error)) {
            zend_throw_exception(zend_ce_exception, error.c_str(), 0);
            RETURN_THROWS();
        }
        return;
    }
    if (obj->has_client) {
        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_0_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "list", &retval);

        if (Z_ISUNDEF(retval)) {
            array_init(return_value);
            return;
        }
        RETVAL_ZVAL(&retval, 1, 1);
        return;
    }

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string error;
        if (kislayphp_rpc_discovery_list(return_value, &error)) {
            return;
        }
    }
#endif

    kislayphp_discovery_list_local(obj, return_value);
}

PHP_METHOD(KislayPHPDiscovery, resolve) {
    char *name = nullptr;
    size_t name_len = 0;
    char *hash_key = nullptr;
    size_t hash_key_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(hash_key, hash_key_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_remote_base_url) {
        std::string path = "/resolve?name=" + kislayphp_url_encode(std::string(name, name_len));
        if (hash_key != nullptr && hash_key_len > 0) {
            path += "&hashKey=" + kislayphp_url_encode(std::string(hash_key, hash_key_len));
        }

        int status = 0;
        std::string response_body;
        std::string error;
        if (!kislayphp_http_request(obj->remote_base_url, "GET", path, "", &status, &response_body, &error)) {
            zend_throw_exception(zend_ce_exception, error.c_str(), 0);
            RETURN_THROWS();
        }
        if (status == 404) {
            RETURN_NULL();
        }
        if (status < 200 || status >= 300) {
            std::string message = "Remote discovery resolve failed with HTTP " + std::to_string(status);
            if (!response_body.empty()) {
                message += ": " + response_body;
            }
            zend_throw_exception(zend_ce_exception, message.c_str(), 0);
            RETURN_THROWS();
        }
        RETURN_STRINGL(response_body.c_str(), response_body.size());
    }
    if (obj->has_client) {
        zval name_zv;
        ZVAL_STRINGL(&name_zv, name, name_len);

        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_1_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "resolve", &retval, &name_zv);
        zval_ptr_dtor(&name_zv);

        if (Z_ISUNDEF(retval)) {
            RETURN_NULL();
        }
        RETVAL_ZVAL(&retval, 1, 1);
        return;
    }

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        php_kislayphp_discovery_t::ServiceInstance resolved;
        std::string error;
        if (kislayphp_rpc_discovery_resolve(std::string(name, name_len), &resolved, &error)) {
            if (resolved.url.empty()) {
                RETURN_NULL();
            }
            RETURN_STRING(resolved.url.c_str());
        }
    }
#endif

    std::string resolved_url;
    std::string routing_key = (hash_key != nullptr && hash_key_len > 0)
        ? std::string(hash_key, hash_key_len)
        : std::string();
    if (!kislayphp_discovery_resolve_local(obj, std::string(name, name_len), routing_key, &resolved_url)) {
        RETURN_NULL();
    }
    RETURN_STRING(resolved_url.c_str());
}

PHP_METHOD(KislayPHPDiscovery, listInstances) {
    char *name = nullptr;
    size_t name_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_remote_base_url) {
        const std::string path = "/instances?name=" + kislayphp_url_encode(std::string(name, name_len));
        int status = 0;
        std::string response_body;
        std::string error;
        if (!kislayphp_http_request(obj->remote_base_url, "GET", path, "", &status, &response_body, &error)) {
            zend_throw_exception(zend_ce_exception, error.c_str(), 0);
            RETURN_THROWS();
        }
        if (status < 200 || status >= 300) {
            std::string message = "Remote discovery listInstances failed with HTTP " + std::to_string(status);
            if (!response_body.empty()) {
                message += ": " + response_body;
            }
            zend_throw_exception(zend_ce_exception, message.c_str(), 0);
            RETURN_THROWS();
        }
        if (!kislayphp_json_decode_assoc(response_body, return_value, &error)) {
            zend_throw_exception(zend_ce_exception, error.c_str(), 0);
            RETURN_THROWS();
        }
        return;
    }
    if (obj->has_client && kislayphp_object_has_method(&obj->client, "listInstances")) {
        zval retval;
        ZVAL_UNDEF(&retval);
        zval args[1];
        ZVAL_STRINGL(&args[0], name, name_len);
        bool called = kislayphp_call_object_method(&obj->client, "listInstances", 1, args, &retval);
        zval_ptr_dtor(&args[0]);
        if (called && EG(exception) == nullptr && !Z_ISUNDEF(retval)) {
            RETVAL_ZVAL(&retval, 1, 1);
            return;
        }
        if (!Z_ISUNDEF(retval)) {
            zval_ptr_dtor(&retval);
        }
        array_init(return_value);
        return;
    }
    array_init(return_value);

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string error;
        if (kislayphp_rpc_discovery_list_instances(std::string(name, name_len), return_value, &error)) {
            return;
        }
    }
#endif

    kislayphp_discovery_list_instances_local(obj, std::string(name, name_len), return_value);
}

PHP_METHOD(KislayPHPDiscovery, heartbeat) {
    char *name = nullptr;
    size_t name_len = 0;
    char *instance_id = nullptr;
    size_t instance_id_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(instance_id, instance_id_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    bool updated = false;
    std::string key(name, name_len);

    if (obj->has_remote_base_url) {
        std::unordered_map<std::string, std::string> fields;
        fields["name"] = key;
        if (instance_id != nullptr && instance_id_len > 0) {
            fields["instanceId"] = std::string(instance_id, instance_id_len);
        }

        int status = 0;
        std::string response_body;
        std::string error;
        const std::string body = kislayphp_discovery_build_form_body(fields);
        if (!kislayphp_http_request(obj->remote_base_url, "POST", "/heartbeat", body, &status, &response_body, &error)) {
            zend_throw_exception(zend_ce_exception, error.c_str(), 0);
            RETURN_FALSE;
        }
        if (status == 404) {
            RETURN_FALSE;
        }
        if (status < 200 || status >= 300) {
            std::string message = "Remote discovery heartbeat failed with HTTP " + std::to_string(status);
            if (!response_body.empty()) {
                message += ": " + response_body;
            }
            zend_throw_exception(zend_ce_exception, message.c_str(), 0);
            RETURN_FALSE;
        }
        RETURN_TRUE;
    }

    if (obj->has_client && kislayphp_object_has_method(&obj->client, "heartbeat")) {
        zval retval;
        ZVAL_UNDEF(&retval);
        bool called = false;
        if (instance_id != nullptr && instance_id_len > 0) {
            zval args[2];
            ZVAL_STRINGL(&args[0], name, name_len);
            ZVAL_STRINGL(&args[1], instance_id, instance_id_len);
            called = kislayphp_call_object_method(&obj->client, "heartbeat", 2, args, &retval);
            zval_ptr_dtor(&args[0]);
            zval_ptr_dtor(&args[1]);
        } else {
            zval args[1];
            ZVAL_STRINGL(&args[0], name, name_len);
            called = kislayphp_call_object_method(&obj->client, "heartbeat", 1, args, &retval);
            zval_ptr_dtor(&args[0]);
        }

        if (!called || EG(exception) != nullptr) {
            if (!Z_ISUNDEF(retval)) {
                zval_ptr_dtor(&retval);
            }
            RETURN_FALSE;
        }
        if (!Z_ISUNDEF(retval) && Z_TYPE(retval) == IS_FALSE) {
            zval_ptr_dtor(&retval);
            RETURN_FALSE;
        }
        if (!Z_ISUNDEF(retval)) {
            zval_ptr_dtor(&retval);
        }
        RETURN_TRUE;
    }

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string error;
        std::string instance_value = (instance_id != nullptr && instance_id_len > 0)
            ? std::string(instance_id, instance_id_len)
            : std::string();
        if (kislayphp_rpc_discovery_heartbeat(key, instance_value.empty() ? key : instance_value, &error)) {
            RETURN_TRUE;
        }
    }
#endif

    updated = kislayphp_discovery_heartbeat_local(obj,
                                                  key,
                                                  (instance_id != nullptr && instance_id_len > 0)
                                                      ? std::string(instance_id, instance_id_len)
                                                      : std::string());

    RETURN_BOOL(updated);
}

PHP_METHOD(KislayPHPDiscovery, setStatus) {
    char *name = nullptr;
    size_t name_len = 0;
    char *status = nullptr;
    size_t status_len = 0;
    char *instance_id = nullptr;
    size_t instance_id_len = 0;
    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_STRING(status, status_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(instance_id, instance_id_len)
    ZEND_PARSE_PARAMETERS_END();

    std::string normalized = kislayphp_upper(std::string(status, status_len));
    if (!kislayphp_is_valid_status(normalized)) {
        zend_throw_exception(zend_ce_exception, "Invalid status. Use UP, DOWN, OUT_OF_SERVICE, or UNKNOWN", 0);
        RETURN_FALSE;
    }

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    bool updated = false;
    std::string key(name, name_len);

    if (obj->has_remote_base_url) {
        std::unordered_map<std::string, std::string> fields;
        fields["name"] = key;
        fields["status"] = normalized;
        if (instance_id != nullptr && instance_id_len > 0) {
            fields["instanceId"] = std::string(instance_id, instance_id_len);
        }

        int status_code = 0;
        std::string response_body;
        std::string error;
        const std::string body = kislayphp_discovery_build_form_body(fields);
        if (!kislayphp_http_request(obj->remote_base_url, "POST", "/status", body, &status_code, &response_body, &error)) {
            zend_throw_exception(zend_ce_exception, error.c_str(), 0);
            RETURN_FALSE;
        }
        if (status_code == 404) {
            RETURN_FALSE;
        }
        if (status_code < 200 || status_code >= 300) {
            std::string message = "Remote discovery setStatus failed with HTTP " + std::to_string(status_code);
            if (!response_body.empty()) {
                message += ": " + response_body;
            }
            zend_throw_exception(zend_ce_exception, message.c_str(), 0);
            RETURN_FALSE;
        }
        RETURN_TRUE;
    }

    if (obj->has_client && kislayphp_object_has_method(&obj->client, "setStatus")) {
        zval retval;
        ZVAL_UNDEF(&retval);
        bool called = false;
        if (instance_id != nullptr && instance_id_len > 0) {
            zval args[3];
            ZVAL_STRINGL(&args[0], name, name_len);
            ZVAL_STRINGL(&args[1], normalized.c_str(), normalized.size());
            ZVAL_STRINGL(&args[2], instance_id, instance_id_len);
            called = kislayphp_call_object_method(&obj->client, "setStatus", 3, args, &retval);
            zval_ptr_dtor(&args[0]);
            zval_ptr_dtor(&args[1]);
            zval_ptr_dtor(&args[2]);
        } else {
            zval args[2];
            ZVAL_STRINGL(&args[0], name, name_len);
            ZVAL_STRINGL(&args[1], normalized.c_str(), normalized.size());
            called = kislayphp_call_object_method(&obj->client, "setStatus", 2, args, &retval);
            zval_ptr_dtor(&args[0]);
            zval_ptr_dtor(&args[1]);
        }

        if (!called || EG(exception) != nullptr) {
            if (!Z_ISUNDEF(retval)) {
                zval_ptr_dtor(&retval);
            }
            RETURN_FALSE;
        }
        if (!Z_ISUNDEF(retval) && Z_TYPE(retval) == IS_FALSE) {
            zval_ptr_dtor(&retval);
            RETURN_FALSE;
        }
        if (!Z_ISUNDEF(retval)) {
            zval_ptr_dtor(&retval);
        }
        RETURN_TRUE;
    }

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string error;
        std::string instance_value = (instance_id != nullptr && instance_id_len > 0)
            ? std::string(instance_id, instance_id_len)
            : std::string();
        if (kislayphp_rpc_discovery_set_status(key, instance_value.empty() ? key : instance_value, normalized, &error)) {
            kislayphp_discovery_emit(obj, "discovery.status.change", key, normalized);
            RETURN_TRUE;
        }
    }
#endif

    updated = kislayphp_discovery_set_status_local(obj,
                                                   key,
                                                   normalized,
                                                   (instance_id != nullptr && instance_id_len > 0)
                                                       ? std::string(instance_id, instance_id_len)
                                                       : std::string());
    RETURN_BOOL(updated);
}

PHP_METHOD(KislayPHPDiscovery, setHeartbeatTimeout) {
    zend_long milliseconds = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(milliseconds)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    pthread_rwlock_wrlock(&obj->lock);
    obj->heartbeat_timeout_ms = kislayphp_sanitize_heartbeat_timeout_ms(
        milliseconds,
        "Kislay\\Discovery\\ServiceRegistry::setHeartbeatTimeout");
    pthread_rwlock_unlock_wr(&obj->lock);
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPDiscovery, setBus) {
    zval *bus = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(bus)
    ZEND_PARSE_PARAMETERS_END();

    if (bus == nullptr || Z_TYPE_P(bus) != IS_OBJECT) {
        zend_throw_exception(zend_ce_exception, "Bus must be an object", 0);
        RETURN_FALSE;
    }

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_bus) {
        zval_ptr_dtor(&obj->bus);
        obj->has_bus = false;
    }
    ZVAL_COPY(&obj->bus, bus);
    obj->has_bus = true;
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPDiscovery, resolveAll) {
    char *name = nullptr;
    size_t name_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_remote_base_url) {
        const std::string path = "/resolve-all?name=" + kislayphp_url_encode(std::string(name, name_len));
        int status = 0;
        std::string response_body;
        std::string error;
        if (!kislayphp_http_request(obj->remote_base_url, "GET", path, "", &status, &response_body, &error)) {
            zend_throw_exception(zend_ce_exception, error.c_str(), 0);
            RETURN_THROWS();
        }
        if (status < 200 || status >= 300) {
            std::string message = "Remote discovery resolveAll failed with HTTP " + std::to_string(status);
            if (!response_body.empty()) {
                message += ": " + response_body;
            }
            zend_throw_exception(zend_ce_exception, message.c_str(), 0);
            RETURN_THROWS();
        }
        if (!kislayphp_json_decode_assoc(response_body, return_value, &error)) {
            zend_throw_exception(zend_ce_exception, error.c_str(), 0);
            RETURN_THROWS();
        }
        return;
    }
    array_init(return_value);

    std::string key(name, name_len);
    std::vector<std::pair<int, std::string>> up_instances;

    std::vector<std::pair<std::string, std::string>> stale_instances;
    pthread_rwlock_wrlock(&obj->lock);
    kislayphp_discovery_prune_stale_locked(obj, &key, &stale_instances);
    auto service_it = obj->instances.find(key);
    if (service_it != obj->instances.end()) {
        for (const auto &inst_it : service_it->second) {
            const auto &inst = inst_it.second;
            if (inst.status == "UP") {
                up_instances.push_back({inst.weight, inst.url});
            }
        }
    }
    pthread_rwlock_unlock_wr(&obj->lock);
    for (const auto &stale : stale_instances) {
        kislayphp_discovery_emit(obj, "discovery.heartbeat.timeout", stale.first, stale.second);
    }

    std::sort(up_instances.begin(), up_instances.end(),
              [](const std::pair<int,std::string> &a, const std::pair<int,std::string> &b) {
                  return a.first > b.first;
              });

    for (const auto &p : up_instances) {
        add_next_index_string(return_value, p.second.c_str());
    }
}

PHP_METHOD(KislayPHPDiscovery, setBalancer) {
    char *type = nullptr;
    size_t type_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(type, type_len)
    ZEND_PARSE_PARAMETERS_END();

    std::string balancer(type, type_len);
    if (balancer != "random" && balancer != "round_robin" &&
        balancer != "consistent_hash" && balancer != "weighted_random") {
        zend_throw_exception(zend_ce_exception,
            "Invalid balancer type. Use: random, round_robin, consistent_hash, weighted_random", 0);
        return;
    }
    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    pthread_rwlock_wrlock(&obj->lock);
    obj->balancer_type = balancer;
    pthread_rwlock_unlock_wr(&obj->lock);
}

PHP_METHOD(KislayPHPDiscovery, resetBalancer) {
    ZEND_PARSE_PARAMETERS_NONE();
    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    pthread_rwlock_wrlock(&obj->lock);
    obj->balancer_type = "weighted_random";
    pthread_rwlock_unlock_wr(&obj->lock);
}

PHP_METHOD(KislayPHPDiscovery, getWeight) {
    char *name = nullptr;
    size_t name_len = 0;
    char *url = nullptr;
    size_t url_len = 0;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_STRING(url, url_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    std::string service(name, name_len);
    std::string service_url(url, url_len);
    int weight = 1;

    if (obj->redis_enabled) {
        std::string redis_error;
        if (!kislayphp_discovery_redis_sync_service(obj, service, &redis_error)) {
            kislayphp_discovery_warn_redis_fallback(redis_error);
        }
    }

    std::vector<std::pair<std::string, std::string>> stale_instances;
    pthread_rwlock_wrlock(&obj->lock);
    kislayphp_discovery_prune_stale_locked(obj, &service, &stale_instances);
    auto service_it = obj->instances.find(service);
    if (service_it != obj->instances.end()) {
        for (const auto &inst_it : service_it->second) {
            if (inst_it.second.url == service_url) {
                weight = inst_it.second.weight;
                break;
            }
        }
    }
    pthread_rwlock_unlock_wr(&obj->lock);
    for (const auto &stale : stale_instances) {
        kislayphp_discovery_emit(obj, "discovery.heartbeat.timeout", stale.first, stale.second);
    }
    RETURN_LONG(weight);
}

static const zend_function_entry kislayphp_discovery_methods[] = {
    PHP_ME(KislayPHPDiscovery, __construct, arginfo_kislayphp_discovery_construct, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, listen, arginfo_kislayphp_discovery_listen, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, run, arginfo_kislayphp_discovery_void, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, register, arginfo_kislayphp_discovery_register, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, deregister, arginfo_kislayphp_discovery_deregister, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, list, arginfo_kislayphp_discovery_void, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, resolve, arginfo_kislayphp_discovery_resolve, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, listInstances, arginfo_kislayphp_discovery_list_instances, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, heartbeat, arginfo_kislayphp_discovery_heartbeat, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, setStatus, arginfo_kislayphp_discovery_set_status, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, setHeartbeatTimeout, arginfo_kislayphp_discovery_set_heartbeat_timeout, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, setBus, arginfo_kislayphp_discovery_set_bus, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, setClient, arginfo_kislayphp_discovery_set_client, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, resolveAll, arginfo_kislayphp_discovery_resolve_all, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, setBalancer, arginfo_kislayphp_discovery_set_balancer, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, resetBalancer, arginfo_kislayphp_discovery_void, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, getWeight, arginfo_kislayphp_discovery_get_weight, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

static const zend_function_entry kislayphp_discovery_client_methods[] = {
    ZEND_ABSTRACT_ME(KislayPHPDiscoveryClientInterface, register, arginfo_kislayphp_discovery_client_register)
    ZEND_ABSTRACT_ME(KislayPHPDiscoveryClientInterface, deregister, arginfo_kislayphp_discovery_client_deregister)
    ZEND_ABSTRACT_ME(KislayPHPDiscoveryClientInterface, resolve, arginfo_kislayphp_discovery_client_resolve)
    ZEND_ABSTRACT_ME(KislayPHPDiscoveryClientInterface, list, arginfo_kislayphp_discovery_void)
    PHP_FE_END
};

PHP_MINIT_FUNCTION(kislayphp_discovery) {
    zend_class_entry ce;
    INIT_NS_CLASS_ENTRY(ce, "Kislay\\Discovery", "ClientInterface", kislayphp_discovery_client_methods);
    kislayphp_discovery_client_ce = zend_register_internal_interface(&ce);
    zend_register_class_alias("KislayPHP\\Discovery\\ClientInterface", kislayphp_discovery_client_ce);
    INIT_NS_CLASS_ENTRY(ce, "Kislay\\Discovery", "ServiceRegistry", kislayphp_discovery_methods);
    kislayphp_discovery_ce = zend_register_internal_class(&ce);
    zend_register_class_alias("KislayPHP\\Discovery\\ServiceRegistry", kislayphp_discovery_ce);
    kislayphp_discovery_ce->create_object = kislayphp_discovery_create_object;
    std::memcpy(&kislayphp_discovery_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    kislayphp_discovery_handlers.offset = XtOffsetOf(php_kislayphp_discovery_t, std);
    kislayphp_discovery_handlers.free_obj = kislayphp_discovery_free_obj;
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(kislayphp_discovery) {
    return SUCCESS;
}

PHP_MINFO_FUNCTION(kislayphp_discovery) {
    php_info_print_table_start();
    php_info_print_table_header(2, "kislayphp_discovery support", "enabled");
    php_info_print_table_row(2, "Version", PHP_KISLAYPHP_DISCOVERY_VERSION);
    php_info_print_table_end();
}

zend_module_entry kislayphp_discovery_module_entry = {
    STANDARD_MODULE_HEADER,
    PHP_KISLAYPHP_DISCOVERY_EXTNAME,
    nullptr,
    PHP_MINIT(kislayphp_discovery),
    PHP_MSHUTDOWN(kislayphp_discovery),
    nullptr,
    nullptr,
    PHP_MINFO(kislayphp_discovery),
    PHP_KISLAYPHP_DISCOVERY_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#if defined(COMPILE_DL_KISLAYPHP_DISCOVERY) || defined(ZEND_COMPILE_DL_EXT)
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE();
#endif
extern "C" {
ZEND_GET_MODULE(kislayphp_discovery)
}
#endif

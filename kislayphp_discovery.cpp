extern "C" {
#include "php.h"
#include "ext/standard/info.h"
#include "Zend/zend_API.h"
#include "Zend/zend_exceptions.h"
#include "Zend/zend_interfaces.h"
}

#include "php_kislayphp_discovery.h"

#include <chrono>
#include <cctype>
#include <cstring>
#include <pthread.h>
#include <cstdlib>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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
    };

    std::unordered_map<std::string, std::string> services;
    std::unordered_map<std::string, std::unordered_map<std::string, ServiceInstance>> instances;
    std::unordered_map<std::string, size_t> rr_index;
    pthread_mutex_t lock;
    zval bus;
    bool has_bus;
    zval client;
    bool has_client;
    zend_long heartbeat_timeout_ms;
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
    pthread_mutex_init(&obj->lock, nullptr);
    ZVAL_UNDEF(&obj->bus);
    obj->has_bus = false;
    ZVAL_UNDEF(&obj->client);
    obj->has_client = false;
    obj->heartbeat_timeout_ms = kislayphp_sanitize_heartbeat_timeout_ms(
        kislayphp_env_long("KISLAY_DISCOVERY_HEARTBEAT_TIMEOUT_MS", 90000),
        "Kislay\\Discovery\\ServiceRegistry::__construct");
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
    obj->rr_index.~unordered_map();
    obj->instances.~unordered_map();
    obj->services.~unordered_map();
    pthread_mutex_destroy(&obj->lock);
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

static bool kislayphp_select_healthy_instance(php_kislayphp_discovery_t *obj,
                                              const std::string &service,
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

    size_t index = obj->rr_index[service] % healthy.size();
    obj->rr_index[service] = (index + 1) % healthy.size();
    *selected = *healthy[index];
    return true;
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_void, 0, 0, 0)
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
    ZEND_PARSE_PARAMETERS_NONE();
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

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));

    pthread_mutex_lock(&obj->lock);
    php_kislayphp_discovery_t::ServiceInstance record;
    record.service_name = service;
    record.instance_id = instance;
    record.url = service_url;
    record.status = "UP";
    record.metadata = metadata;
    record.last_heartbeat_ms = kislayphp_now_ms();
    obj->instances[service][instance] = record;
    obj->services[service] = service_url;
    pthread_mutex_unlock(&obj->lock);

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

    pthread_mutex_lock(&obj->lock);
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
    pthread_mutex_unlock(&obj->lock);
    if (!url.empty()) {
        kislayphp_discovery_emit(obj, "discovery.deregister", key, url);
    }
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPDiscovery, list) {
    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
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

    array_init(return_value);
    pthread_mutex_lock(&obj->lock);
    for (const auto &entry : obj->services) {
        add_assoc_string(return_value, entry.first.c_str(), entry.second.c_str());
    }
    pthread_mutex_unlock(&obj->lock);
}

PHP_METHOD(KislayPHPDiscovery, resolve) {
    char *name = nullptr;
    size_t name_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
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

    std::string value;
    bool found = false;
    pthread_mutex_lock(&obj->lock);
    std::string key(name, name_len);
    php_kislayphp_discovery_t::ServiceInstance selected;
    auto service_instances_it = obj->instances.find(key);
    if (service_instances_it != obj->instances.end() && !service_instances_it->second.empty()) {
        if (kislayphp_select_healthy_instance(obj, key, &selected)) {
            value = selected.url;
            found = true;
        }
    } else {
        auto it = obj->services.find(key);
        if (it != obj->services.end()) {
            value = it->second;
            found = true;
        }
    }
    pthread_mutex_unlock(&obj->lock);
    if (!found) {
        RETURN_NULL();
    }
    RETURN_STRING(value.c_str());
}

PHP_METHOD(KislayPHPDiscovery, listInstances) {
    char *name = nullptr;
    size_t name_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
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

    pthread_mutex_lock(&obj->lock);
    auto service_it = obj->instances.find(std::string(name, name_len));
    if (service_it != obj->instances.end()) {
        for (const auto &instance_it : service_it->second) {
            kislayphp_add_instance_array(return_value, instance_it.second);
        }
    }
    pthread_mutex_unlock(&obj->lock);
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

    pthread_mutex_lock(&obj->lock);
    auto service_it = obj->instances.find(key);
    if (service_it != obj->instances.end() && !service_it->second.empty()) {
        if (instance_id != nullptr && instance_id_len > 0) {
            auto inst_it = service_it->second.find(std::string(instance_id, instance_id_len));
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
    pthread_mutex_unlock(&obj->lock);

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
            RETURN_TRUE;
        }
    }
#endif

    pthread_mutex_lock(&obj->lock);
    auto service_it = obj->instances.find(key);
    if (service_it != obj->instances.end() && !service_it->second.empty()) {
        if (instance_id != nullptr && instance_id_len > 0) {
            auto inst_it = service_it->second.find(std::string(instance_id, instance_id_len));
            if (inst_it != service_it->second.end()) {
                inst_it->second.status = normalized;
                updated = true;
            }
        } else {
            for (auto &inst_it : service_it->second) {
                inst_it.second.status = normalized;
            }
            updated = true;
        }
    }
    pthread_mutex_unlock(&obj->lock);

    RETURN_BOOL(updated);
}

PHP_METHOD(KislayPHPDiscovery, setHeartbeatTimeout) {
    zend_long milliseconds = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(milliseconds)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    obj->heartbeat_timeout_ms = kislayphp_sanitize_heartbeat_timeout_ms(
        milliseconds,
        "Kislay\\Discovery\\ServiceRegistry::setHeartbeatTimeout");
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

static const zend_function_entry kislayphp_discovery_methods[] = {
    PHP_ME(KislayPHPDiscovery, __construct, arginfo_kislayphp_discovery_void, ZEND_ACC_PUBLIC)
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
    PHP_FE_END
};

static const zend_function_entry kislayphp_discovery_client_methods[] = {
    ZEND_ABSTRACT_ME(KislayPHPDiscoveryClientInterface, register, arginfo_kislayphp_discovery_client_register)
    ZEND_ABSTRACT_ME(KislayPHPDiscoveryClientInterface, deregister, arginfo_kislayphp_discovery_client_deregister)
    ZEND_ABSTRACT_ME(KislayPHPDiscoveryClientInterface, resolve, arginfo_kislayphp_discovery_resolve)
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

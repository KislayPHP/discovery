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
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifdef KISLAYPHP_RPC
#include <grpcpp/grpcpp.h>

#include "discovery.grpc.pb.h"
#endif

typedef struct _php_kislayphp_discovery_t php_kislayphp_discovery_t;

struct ServiceInstance {
    std::string service_name;
    std::string instance_id;
    std::string url;
    std::string health_check_url;
    std::string status;
    std::unordered_map<std::string, std::string> metadata;
    long long last_heartbeat_ms;
};

struct _php_kislayphp_discovery_t {
    std::unordered_map<std::string, std::string> services;
    std::unordered_map<std::string, std::unordered_map<std::string, ServiceInstance>> instances;
    std::unordered_map<std::string, size_t> rr_index;
    pthread_mutex_t lock;
    zval bus;
    bool has_bus;
    zval client;
    bool has_client;
    zend_long heartbeat_timeout_ms;
    
    std::atomic<bool> health_check_active;
    pthread_t health_check_thread;
    bool health_check_thread_started;
    zend_long health_check_interval_ms;
    
    zend_object std;
};

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

static long long kislayphp_now_ms() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

struct kislayphp_parsed_url_t {
    std::string scheme;
    std::string host;
    int port;
    std::string path;
};

static bool kislayphp_parse_url(const std::string &input, kislayphp_parsed_url_t *out) {
    if (out == nullptr) {
        return false;
    }

    std::string url = input;
    if (url.empty()) {
        return false;
    }

    size_t scheme_sep = url.find("://");
    if (scheme_sep == std::string::npos) {
        return false;
    }

    out->scheme = url.substr(0, scheme_sep);
    std::string rest = url.substr(scheme_sep + 3);

    size_t slash = rest.find('/');
    std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    out->path = (slash == std::string::npos) ? "/" : rest.substr(slash);
    if (out->path.empty()) {
        out->path = "/";
    }

    if (authority.empty()) {
        return false;
    }

    size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        out->host = authority.substr(0, colon);
        const std::string port_str = authority.substr(colon + 1);
        if (port_str.empty()) {
            return false;
        }
        out->port = std::atoi(port_str.c_str());
        if (out->port <= 0 || out->port > 65535) {
            return false;
        }
    } else {
        out->host = authority;
        out->port = (out->scheme == "https") ? 443 : 80;
    }

    return !out->host.empty();
}

static int kislayphp_connect_with_timeout(const std::string &host, int port, int timeout_ms) {
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    char port_buf[16];
    std::snprintf(port_buf, sizeof(port_buf), "%d", port);

    struct addrinfo *res = nullptr;
    if (getaddrinfo(host.c_str(), port_buf, &hints, &res) != 0 || res == nullptr) {
        return -1;
    }

    int socket_fd = -1;
    for (struct addrinfo *ai = res; ai != nullptr; ai = ai->ai_next) {
        socket_fd = static_cast<int>(socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (socket_fd < 0) {
            continue;
        }

        const int old_flags = fcntl(socket_fd, F_GETFL, 0);
        if (old_flags < 0) {
            close(socket_fd);
            socket_fd = -1;
            continue;
        }
        if (fcntl(socket_fd, F_SETFL, old_flags | O_NONBLOCK) != 0) {
            close(socket_fd);
            socket_fd = -1;
            continue;
        }

        int rc = connect(socket_fd, ai->ai_addr, ai->ai_addrlen);
        if (rc == 0) {
            fcntl(socket_fd, F_SETFL, old_flags);
            break;
        }
        if (errno != EINPROGRESS) {
            close(socket_fd);
            socket_fd = -1;
            continue;
        }

        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(socket_fd, &wfds);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        rc = select(socket_fd + 1, nullptr, &wfds, nullptr, &tv);
        if (rc > 0 && FD_ISSET(socket_fd, &wfds)) {
            int err = 0;
            socklen_t len = sizeof(err);
            if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                fcntl(socket_fd, F_SETFL, old_flags);
                break;
            }
        }

        close(socket_fd);
        socket_fd = -1;
    }

    freeaddrinfo(res);
    return socket_fd;
}

static bool kislayphp_send_all(int socket_fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(socket_fd, data + sent, len - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

static bool kislayphp_http_health_check(const ServiceInstance &inst) {
    std::string target_url = inst.url;
    if (!inst.health_check_url.empty()) {
        if (inst.health_check_url.rfind("http://", 0) == 0 || inst.health_check_url.rfind("https://", 0) == 0) {
            target_url = inst.health_check_url;
        } else {
            std::string base = inst.url;
            if (!base.empty() && base.back() == '/' && inst.health_check_url.front() == '/') {
                target_url = base.substr(0, base.size() - 1) + inst.health_check_url;
            } else if (!base.empty() && base.back() != '/' && inst.health_check_url.front() != '/') {
                target_url = base + "/" + inst.health_check_url;
            } else {
                target_url = base + inst.health_check_url;
            }
        }
    }

    kislayphp_parsed_url_t parsed;
    if (!kislayphp_parse_url(target_url, &parsed)) {
        return false;
    }

    const int timeout_ms = 2000;
    int socket_fd = kislayphp_connect_with_timeout(parsed.host, parsed.port, timeout_ms);
    if (socket_fd < 0) {
        return false;
    }

    if (parsed.scheme == "https") {
        close(socket_fd);
        return true;
    }

    char request[2048];
    std::snprintf(
        request,
        sizeof(request),
        "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
        parsed.path.c_str(),
        parsed.host.c_str());

    const bool sent = kislayphp_send_all(socket_fd, request, std::strlen(request));
    if (!sent) {
        close(socket_fd);
        return false;
    }

    char response[256];
    const ssize_t n = recv(socket_fd, response, sizeof(response) - 1, 0);
    close(socket_fd);
    if (n <= 0) {
        return false;
    }
    response[n] = '\0';

    const char *prefix = "HTTP/";
    if (std::strncmp(response, prefix, std::strlen(prefix)) != 0) {
        return false;
    }

    const char *sp = std::strchr(response, ' ');
    if (sp == nullptr || std::strlen(sp) < 4) {
        return false;
    }

    int status = std::atoi(sp + 1);
    return status >= 200 && status < 300;
}

static void *kislayphp_discovery_health_check_loop(void *arg) {
    php_kislayphp_discovery_t *obj = static_cast<php_kislayphp_discovery_t *>(arg);
    
    while (obj->health_check_active.load()) {
        struct timespec ts;
        ts.tv_sec = obj->health_check_interval_ms / 1000;
        ts.tv_nsec = (obj->health_check_interval_ms % 1000) * 1000000;
        nanosleep(&ts, nullptr);
        
        if (!obj->health_check_active.load()) break;
        
        std::vector<ServiceInstance> to_check;
        pthread_mutex_lock(&obj->lock);
        for (auto &svc_it : obj->instances) {
            for (auto &inst_it : svc_it.second) {
                if (!inst_it.second.health_check_url.empty()) {
                    to_check.push_back(inst_it.second);
                }
            }
        }
        pthread_mutex_unlock(&obj->lock);
        
        for (const auto &inst : to_check) {
            const bool healthy = kislayphp_http_health_check(inst);
            pthread_mutex_lock(&obj->lock);
            auto sit = obj->instances.find(inst.service_name);
            if (sit != obj->instances.end()) {
                auto iit = sit->second.find(inst.instance_id);
                if (iit != sit->second.end()) {
                    iit->second.status = healthy ? "UP" : "DOWN";
                    if (healthy) iit->second.last_heartbeat_ms = kislayphp_now_ms();
                }
            }
            pthread_mutex_unlock(&obj->lock);
        }
    }
    return nullptr;
}

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
    new (&obj->instances) std::unordered_map<std::string, std::unordered_map<std::string, ServiceInstance>>();
    new (&obj->rr_index) std::unordered_map<std::string, size_t>();
    pthread_mutex_init(&obj->lock, nullptr);
    ZVAL_UNDEF(&obj->bus);
    obj->has_bus = false;
    ZVAL_UNDEF(&obj->client);
    obj->has_client = false;
    obj->heartbeat_timeout_ms = kislayphp_env_long("KISLAY_DISCOVERY_HEARTBEAT_TIMEOUT", 30000);
    obj->health_check_interval_ms = kislayphp_env_long("KISLAY_DISCOVERY_HEALTH_CHECK_INTERVAL", 10000);
    obj->health_check_active = true;
    obj->health_check_thread_started =
        (pthread_create(&obj->health_check_thread, nullptr, kislayphp_discovery_health_check_loop, obj) == 0);
    if (!obj->health_check_thread_started) {
        obj->health_check_active = false;
        php_error_docref(nullptr, E_WARNING, "Failed to start discovery health check thread");
    }
    
    obj->std.handlers = zend_get_std_object_handlers();
    return &obj->std;
}

static void kislayphp_discovery_free_obj(zend_object *object) {
    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(object);
    obj->health_check_active = false;
    if (obj->health_check_thread_started) {
        pthread_join(obj->health_check_thread, nullptr);
    }
    if (obj->has_bus) zval_ptr_dtor(&obj->bus);
    if (obj->has_client) zval_ptr_dtor(&obj->client);
    obj->rr_index.~unordered_map();
    obj->instances.~unordered_map();
    obj->services.~unordered_map();
    pthread_mutex_destroy(&obj->lock);
    zend_object_std_dtor(&obj->std);
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
        if (key == nullptr || entry == nullptr) continue;
        zend_string *val_str = zval_get_string(entry);
        metadata[std::string(ZSTR_VAL(key), ZSTR_LEN(key))] = std::string(ZSTR_VAL(val_str), ZSTR_LEN(val_str));
        zend_string_release(val_str);
    } ZEND_HASH_FOREACH_END();
}

static bool kislayphp_select_healthy_instance(php_kislayphp_discovery_t *obj,
                                              const std::string &service,
                                              ServiceInstance *selected) {
    auto service_it = obj->instances.find(service);
    if (service_it == obj->instances.end() || service_it->second.empty()) return false;
    const long long now_ms = kislayphp_now_ms();
    std::vector<const ServiceInstance *> healthy;
    for (const auto &instance_it : service_it->second) {
        const auto &instance = instance_it.second;
        const bool is_fresh = (now_ms - instance.last_heartbeat_ms) <= static_cast<long long>(obj->heartbeat_timeout_ms);
        if (instance.status == "UP" && is_fresh) healthy.push_back(&instance);
    }
    if (healthy.empty()) return false;
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
    ZEND_ARG_TYPE_INFO(0, healthCheckUrl, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_resolve, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(KislayPHPDiscovery, register) {
    char *name = nullptr, *url = nullptr, *instance_id = nullptr, *hc_url = nullptr;
    size_t name_len = 0, url_len = 0, instance_id_len = 0, hc_url_len = 0;
    zval *metadata_zv = nullptr;
    ZEND_PARSE_PARAMETERS_START(2, 5)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_STRING(url, url_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_EX(metadata_zv, 1, 0)
        Z_PARAM_STRING(instance_id, instance_id_len)
        Z_PARAM_STRING(hc_url, hc_url_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    pthread_mutex_lock(&obj->lock);
    std::string service(name, name_len);
    std::string service_url(url, url_len);
    std::string inst = (instance_id_len > 0) ? std::string(instance_id, instance_id_len) : service_url;
    
    ServiceInstance record;
    record.service_name = service;
    record.instance_id = inst;
    record.url = service_url;
    record.health_check_url = (hc_url_len > 0) ? std::string(hc_url, hc_url_len) : "";
    record.status = "UP";
    record.last_heartbeat_ms = kislayphp_now_ms();
    kislayphp_parse_metadata_array(metadata_zv, record.metadata);
    
    obj->instances[service][inst] = record;
    obj->services[service] = service_url;
    pthread_mutex_unlock(&obj->lock);
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPDiscovery, resolve) {
    char *name = nullptr; size_t name_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();
    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    pthread_mutex_lock(&obj->lock);
    ServiceInstance selected;
    bool found = kislayphp_select_healthy_instance(obj, std::string(name, name_len), &selected);
    pthread_mutex_unlock(&obj->lock);
    if (found) RETURN_STRING(selected.url.c_str());
    RETURN_NULL();
}

PHP_METHOD(KislayPHPDiscovery, heartbeat) {
    char *name = nullptr, *instance_id = nullptr;
    size_t name_len = 0, instance_id_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(instance_id, instance_id_len)
    ZEND_PARSE_PARAMETERS_END();
    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    pthread_mutex_lock(&obj->lock);
    std::string key(name, name_len);
    auto sit = obj->instances.find(key);
    bool ok = false;
    if (sit != obj->instances.end()) {
        if (instance_id_len > 0) {
            auto iit = sit->second.find(std::string(instance_id, instance_id_len));
            if (iit != sit->second.end()) {
                iit->second.last_heartbeat_ms = kislayphp_now_ms();
                iit->second.status = "UP";
                ok = true;
            }
        } else {
            for (auto &i : sit->second) { i.second.last_heartbeat_ms = kislayphp_now_ms(); i.second.status = "UP"; }
            ok = true;
        }
    }
    pthread_mutex_unlock(&obj->lock);
    RETURN_BOOL(ok);
}

static const zend_function_entry kislayphp_discovery_methods[] = {
    PHP_ME(KislayPHPDiscovery, register, arginfo_kislayphp_discovery_register, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, resolve, arginfo_kislayphp_discovery_resolve, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, heartbeat, arginfo_kislayphp_discovery_void, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

PHP_MINIT_FUNCTION(kislayphp_discovery) {
    zend_class_entry ce;
    INIT_NS_CLASS_ENTRY(ce, "Kislay\\Discovery", "ServiceRegistry", kislayphp_discovery_methods);
    kislayphp_discovery_ce = zend_register_internal_class(&ce);
    kislayphp_discovery_ce->create_object = kislayphp_discovery_create_object;
    return SUCCESS;
}

zend_module_entry kislayphp_discovery_module_entry = {
    STANDARD_MODULE_HEADER,
    PHP_KISLAYPHP_DISCOVERY_EXTNAME,
    nullptr,
    PHP_MINIT(kislayphp_discovery),
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    PHP_KISLAYPHP_DISCOVERY_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_KISLAYPHP_DISCOVERY
extern "C" { ZEND_GET_MODULE(kislayphp_discovery) }
#endif

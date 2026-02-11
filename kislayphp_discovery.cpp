extern "C" {
#include "php.h"
#include "ext/standard/info.h"
#include "Zend/zend_API.h"
#include "Zend/zend_exceptions.h"
#include "Zend/zend_interfaces.h"
}

#include "php_kislayphp_discovery.h"

#include <cstring>
#include <pthread.h>
#include <string>
#include <unordered_map>

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

typedef struct _php_kislayphp_discovery_t {
    std::unordered_map<std::string, std::string> services;
    pthread_mutex_t lock;
    zval bus;
    bool has_bus;
    zval client;
    bool has_client;
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
    pthread_mutex_init(&obj->lock, nullptr);
    ZVAL_UNDEF(&obj->bus);
    obj->has_bus = false;
    ZVAL_UNDEF(&obj->client);
    obj->has_client = false;
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

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_void, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_register, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, url, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_deregister, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_resolve, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_set_bus, 0, 0, 1)
    ZEND_ARG_OBJ_INFO(0, bus, stdClass, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_discovery_set_client, 0, 0, 1)
    ZEND_ARG_OBJ_INFO(0, client, KislayPHP\\Discovery\\ClientInterface, 0)
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
        zend_throw_exception(zend_ce_exception, "Client must implement KislayPHP\\Discovery\\ClientInterface", 0);
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
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_STRING(url, url_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval name_zv;
        zval url_zv;
        ZVAL_STRINGL(&name_zv, name, name_len);
        ZVAL_STRINGL(&url_zv, url, url_len);

        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_2_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "register", &retval, &name_zv, &url_zv);

        zval_ptr_dtor(&name_zv);
        zval_ptr_dtor(&url_zv);

        if (!Z_ISUNDEF(retval) && Z_TYPE(retval) == IS_FALSE) {
            zval_ptr_dtor(&retval);
            RETURN_FALSE;
        }
        if (!Z_ISUNDEF(retval)) {
            zval_ptr_dtor(&retval);
        }
        kislayphp_discovery_emit(obj, "discovery.register", std::string(name, name_len), std::string(url, url_len));
        RETURN_TRUE;
    }

    pthread_mutex_lock(&obj->lock);
    obj->services[std::string(name, name_len)] = std::string(url, url_len);
    pthread_mutex_unlock(&obj->lock);
    kislayphp_discovery_emit(obj, "discovery.register", std::string(name, name_len), std::string(url, url_len));
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPDiscovery, deregister) {
    char *name = nullptr;
    size_t name_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    std::string key(name, name_len);
    std::string url;

    if (obj->has_client) {
        zval name_zv;
        ZVAL_STRINGL(&name_zv, name, name_len);

        zval resolve_ret;
        ZVAL_UNDEF(&resolve_ret);
        zend_call_method_with_1_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "resolve", &resolve_ret, &name_zv);
        if (Z_TYPE(resolve_ret) == IS_STRING) {
            url.assign(Z_STRVAL(resolve_ret), Z_STRLEN(resolve_ret));
        }
        if (!Z_ISUNDEF(resolve_ret)) {
            zval_ptr_dtor(&resolve_ret);
        }

        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_1_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "deregister", &retval, &name_zv);
        zval_ptr_dtor(&name_zv);

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

    pthread_mutex_lock(&obj->lock);
    auto it = obj->services.find(key);
    if (it != obj->services.end()) {
        url = it->second;
        obj->services.erase(it);
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

    std::string value;
    bool found = false;
    pthread_mutex_lock(&obj->lock);
    auto it = obj->services.find(std::string(name, name_len));
    if (it != obj->services.end()) {
        value = it->second;
        found = true;
    }
    pthread_mutex_unlock(&obj->lock);
    if (!found) {
        RETURN_NULL();
    }
    RETURN_STRING(value.c_str());
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
    PHP_ME(KislayPHPDiscovery, setBus, arginfo_kislayphp_discovery_set_bus, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPDiscovery, setClient, arginfo_kislayphp_discovery_set_client, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

static const zend_function_entry kislayphp_discovery_client_methods[] = {
    ZEND_ABSTRACT_ME(KislayPHPDiscoveryClientInterface, register, arginfo_kislayphp_discovery_register)
    ZEND_ABSTRACT_ME(KislayPHPDiscoveryClientInterface, deregister, arginfo_kislayphp_discovery_deregister)
    ZEND_ABSTRACT_ME(KislayPHPDiscoveryClientInterface, resolve, arginfo_kislayphp_discovery_resolve)
    ZEND_ABSTRACT_ME(KislayPHPDiscoveryClientInterface, list, arginfo_kislayphp_discovery_void)
    PHP_FE_END
};

PHP_MINIT_FUNCTION(kislayphp_discovery) {
    zend_class_entry ce;
    INIT_NS_CLASS_ENTRY(ce, "KislayPHP\\Discovery", "ClientInterface", kislayphp_discovery_client_methods);
    kislayphp_discovery_client_ce = zend_register_internal_interface(&ce);
    INIT_NS_CLASS_ENTRY(ce, "KislayPHP\\Discovery", "ServiceRegistry", kislayphp_discovery_methods);
    kislayphp_discovery_ce = zend_register_internal_class(&ce);
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

extern "C" {
#include "php.h"
#include "ext/standard/info.h"
#include "Zend/zend_exceptions.h"
#include "Zend/zend_interfaces.h"
}

#include "php_kislayphp_discovery.h"

#include <string>
#include <unordered_map>

static zend_class_entry *kislayphp_discovery_ce;

typedef struct _php_kislayphp_discovery_t {
    zend_object std;
    std::unordered_map<std::string, std::string> services;
    zval bus;
    bool has_bus;
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
    ZVAL_UNDEF(&obj->bus);
    obj->has_bus = false;
    obj->std.handlers = &kislayphp_discovery_handlers;
    return &obj->std;
}

static void kislayphp_discovery_free_obj(zend_object *object) {
    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(object);
    if (obj->has_bus) {
        zval_ptr_dtor(&obj->bus);
    }
    obj->services.~unordered_map();
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

PHP_METHOD(KislayPHPDiscovery, __construct) {
    ZEND_PARSE_PARAMETERS_NONE();
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
    obj->services[std::string(name, name_len)] = std::string(url, url_len);
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
    auto it = obj->services.find(key);
    if (it != obj->services.end()) {
        url = it->second;
        obj->services.erase(it);
    }
    if (!url.empty()) {
        kislayphp_discovery_emit(obj, "discovery.deregister", key, url);
    }
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPDiscovery, list) {
    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    array_init(return_value);
    for (const auto &entry : obj->services) {
        add_assoc_string(return_value, entry.first.c_str(), entry.second.c_str());
    }
}

PHP_METHOD(KislayPHPDiscovery, resolve) {
    char *name = nullptr;
    size_t name_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_discovery_t *obj = php_kislayphp_discovery_from_obj(Z_OBJ_P(getThis()));
    auto it = obj->services.find(std::string(name, name_len));
    if (it == obj->services.end()) {
        RETURN_NULL();
    }
    RETURN_STRING(it->second.c_str());
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
    PHP_FE_END
};

PHP_MINIT_FUNCTION(kislayphp_discovery) {
    zend_class_entry ce;
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

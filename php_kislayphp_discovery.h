#ifndef PHP_KISLAYPHP_DISCOVERY_H
#define PHP_KISLAYPHP_DISCOVERY_H

extern "C" {
#include "php.h"
}

#define PHP_KISLAYPHP_DISCOVERY_VERSION "0.0.1"
#define PHP_KISLAYPHP_DISCOVERY_EXTNAME "kislayphp_discovery"

extern zend_module_entry kislayphp_discovery_module_entry;
#define phpext_kislayphp_discovery_ptr &kislayphp_discovery_module_entry

#endif

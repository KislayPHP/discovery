PHP_ARG_ENABLE(kislayphp_discovery, whether to enable kislayphp_discovery,
[  --enable-kislayphp_discovery   Enable kislayphp_discovery support])

if test "$PHP_KISLAYPHP_DISCOVERY" != "no"; then
  PHP_REQUIRE_CXX()
  PHP_NEW_EXTENSION(kislayphp_discovery, kislayphp_discovery.cpp, $ext_shared)
fi

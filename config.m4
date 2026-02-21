PHP_ARG_ENABLE(kislayphp_discovery, whether to enable kislayphp_discovery,
[  --enable-kislayphp_discovery   Enable kislayphp_discovery support])

if test "$PHP_KISLAYPHP_DISCOVERY" != "no"; then
  PHP_REQUIRE_CXX()
  if test -f ../rpc/gen/discovery.pb.cc; then
    RPC_GEN_DIR=`pwd`/../rpc/gen
    PHP_ADD_INCLUDE($RPC_GEN_DIR)
    PHP_ADD_INCLUDE(`pwd`/../rpc)
    PKG_CHECK_MODULES([GRPC], [grpc++])
    PHP_EVAL_INCLINE($GRPC_CFLAGS)
    PHP_EVAL_LIBLINE($GRPC_LIBS, KISLAYPHP_DISCOVERY_SHARED_LIBADD)
    CXXFLAGS="$CXXFLAGS -DKISLAYPHP_RPC"
    RPC_SRCS="../rpc/gen/discovery.pb.cc ../rpc/gen/discovery.grpc.pb.cc"
  else
    AC_MSG_WARN([RPC stubs not found. Building without RPC support.])
    RPC_SRCS=""
  fi

  PHP_NEW_EXTENSION(kislayphp_discovery, kislayphp_discovery.cpp $RPC_SRCS, $ext_shared)
fi

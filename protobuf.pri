# Qt qmake integration with Google Protocol Buffers compiler protoc
#
# To compile protocol buffers with qt qmake, specify PROTOS variable and
# include this file
#
# Based on:
#   https://vilimpoc.org/blog/2013/06/09/using-google-protocol-buffers-with-qmake/

PROTOC = protoc
PROTOC_GRPC = grpc_cpp_plugin

unix {
    PKG_CONFIG = $$pkgConfigExecutable()

    # All our dependency resolution depends on pkg-config. If it isn't
    # available, the errors we will get subsequently are a lot more cryptic than
    # this.
    !system($$PKG_CONFIG --version 2>&1 > /dev/null) {
        error("pkg-config executable is not available. please install it so I can find dependencies.")
    }

    PROTOBUF_LIBS = protobuf grpc++

    !contains(QT_CONFIG, no-pkg-config) {
        CONFIG += link_pkgconfig
        PKGCONFIG += $$PROTOBUF_LIBS
    } else {
        # Some SDK builds (e.g. OS X 5.4.1) are no-pkg-config, so try to hack the linker flags in.
        QMAKE_LFLAGS += $$system($$PKG_CONFIG --libs $$PROTOBUF_LIBS)
    }

    gcc|clang {
        # Add -isystem for protobuf includes to suppress some loud compiler warnings in their headers
        PROTOBUF_CFLAGS = $$system($$PKG_CONFIG --cflags $$PROTOBUF_LIBS)
        PROTOBUF_CFLAGS ~= s/^(?!-I).*//g
        PROTOBUF_CFLAGS ~= s/^-I(.*)/-isystem \\1/g
        QMAKE_CXXFLAGS += $$PROTOBUF_CFLAGS
    }

    PROTOC_GRPC = $$system($$PKG_CONFIG --variable=exec_prefix grpc)/bin/$$PROTOC_GRPC
}

win32 {
    isEmpty(PROTOBUFDIR):error(You must pass PROTOBUFDIR=path/to/protobuf to qmake on this platform)
    INCLUDEPATH += $${PROTOBUFDIR}/include
    LIBS += -L$${PROTOBUFDIR}/lib -lprotobuf
    contains(QMAKE_HOST.os,Windows):PROTOC = $${PROTOBUFDIR}/bin/protoc.exe
}

# Compile all PROTOS. Included in target_predeps to ensure all pb headers are available for compilation.
protobuf.name = protobuf
protobuf.input = PROTOS
protobuf.output = ${QMAKE_FILE_IN_PATH}/${QMAKE_FILE_BASE}.pb.cc ${QMAKE_FILE_IN_PATH}/${QMAKE_FILE_BASE}.pb.h
protobuf.commands = $$PROTOC --cpp_out=${QMAKE_FILE_IN_PATH} --proto_path=${QMAKE_FILE_IN_PATH} ${QMAKE_FILE_NAME}
protobuf.variable_out = SOURCES
protobuf.CONFIG = target_predeps
QMAKE_EXTRA_COMPILERS += protobuf

# Targets to enable header dependencies
protobuf_header.name = protobuf header
protobuf_header.input = PROTOS
protobuf_header.output = ${QMAKE_FILE_IN_PATH}/${QMAKE_FILE_BASE}.pb.h
protobuf_header.depends = ${QMAKE_FILE_IN_PATH}/${QMAKE_FILE_BASE}.pb.cc
protobuf_header.commands = $$escape_expand(\n)
protobuf_header.variable_out = HEADERS
QMAKE_EXTRA_COMPILERS += protobuf_header

# Compile GRPC_PROTOS. These must be listed in PROTOS as well.
grpc.name = GRPC protobuf
grpc.input = GRPC_PROTOS
grpc.output = ${QMAKE_FILE_IN_PATH}/${QMAKE_FILE_BASE}.grpc.pb.cc
grpc.depends = ${QMAKE_FILE_IN_PATH}/${QMAKE_FILE_BASE}.pb.h
grpc.commands = $$PROTOC --plugin=protoc-gen-grpc=$${PROTOC_GRPC} --grpc_out=${QMAKE_FILE_IN_PATH} -I${QMAKE_FILE_IN_PATH} ${QMAKE_FILE_NAME}
grpc.variable_out = SOURCES
QMAKE_EXTRA_COMPILERS += grpc

grpc_header.name = GRPC headers
grpc_header.input = GRPC_PROTOS
grpc_header.output = ${QMAKE_FILE_IN_PATH}/${QMAKE_FILE_BASE}.grpc.pb.h
grpc_header.depends = ${QMAKE_FILE_IN_PATH}/${QMAKE_FILE_BASE}.grpc.pb.cc
grpc_header.commands = $$escape_expand(\n)
grpc_header.variable_out = HEADERS
QMAKE_EXTRA_COMPILERS += grpc_header

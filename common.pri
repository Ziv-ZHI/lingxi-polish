# ==============================================================================
#  全局公共配置：所有子工程均 include 本文件
#  提供：C++ 标准、中文编码、版本号、输出目录、burnishcore 链接方式
# ==============================================================================

LX_ROOT  = $$PWD          # 工程根目录（本文件所在目录）
LX_MAJOR = 2
LX_MINOR = 2
LX_PATCH = 0
LX_VERSION = $${LX_MAJOR}.$${LX_MINOR}.$${LX_PATCH}

QT += core
CONFIG += c++17
CONFIG -= debug_and_release

# 中文源码编码（MSVC 必须 /utf-8，MinGW 指定输入/执行字符集）
win32-msvc* {
    QMAKE_CXXFLAGS += /utf-8 /Zc:__cplusplus
    QMAKE_CFLAGS   += /utf-8
    DEFINES += _CRT_SECURE_NO_WARNINGS NOMINMAX
} else {
    QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
}

DEFINES += LX_VERSION_STR=\\\"$${LX_VERSION}\\\"
DEFINES += LX_ORG_NAME=\\\"BURNISH-LingxiPolish\\\"
DEFINES += LX_CTRL_PERIOD_MS=2        # 控制器 EtherCAT 周期，与下位机保持一致

# 统一输出目录，便于打包（build/bin、build/lib）
CONFIG(debug, debug|release) {
    LX_BUILDTAG = debug
} else {
    LX_BUILDTAG = release
}
LX_BIN_DIR = $$LX_ROOT/build/$$LX_BUILDTAG/bin
LX_LIB_DIR = $$LX_ROOT/build/$$LX_BUILDTAG/lib

# 静态库前缀/后缀：部分 Qt 版本未导出这两个变量，先兜底定义再使用
isEmpty(QMAKE_PREFIX_STATICLIB) {
    win32: QMAKE_PREFIX_STATICLIB =
    else:  QMAKE_PREFIX_STATICLIB = lib
}
isEmpty(QMAKE_EXTENSION_STATICLIB) {
    win32: QMAKE_EXTENSION_STATICLIB = lib
    else:  QMAKE_EXTENSION_STATICLIB = a
}

# 核心库头文件与链接
INCLUDEPATH += $$LX_ROOT/libs/burnishcore
LIBS += -L$$LX_LIB_DIR

# 统一链接 burnishcore 静态库（lib 工程自身除外；
# 依赖各子工程先写 TEMPLATE/TARGET 再 include 本文件）
!contains(TEMPLATE, lib) {
    LIBS += -lburnishcore
    PRE_TARGETDEPS += $$LX_LIB_DIR/$${QMAKE_PREFIX_STATICLIB}burnishcore.$${QMAKE_EXTENSION_STATICLIB}
}

OBJECTS_DIR = $$OUT_PWD/.obj
MOC_DIR     = $$OUT_PWD/.moc
RCC_DIR     = $$OUT_PWD/.rcc
UI_DIR      = $$OUT_PWD/.ui

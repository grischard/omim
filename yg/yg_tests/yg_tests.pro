TARGET = yg_tests
CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app

ROOT_DIR = ../..
DEPENDENCIES = qt_tstfrm map indexer yg platform geometry coding base expat freetype fribidi

include($$ROOT_DIR/common.pri)

QT *= opengl gui core

win32 {
  LIBS *= -lopengl32 -lshell32
  win32-g++: LIBS *= -lpthread
}
macx*: LIBS *= "-framework Foundation"

SOURCES += \
    ../../testing/testingmain.cpp \
    texture_test.cpp \
    resourcemanager_test.cpp \
    skin_loader_test.cpp \
    skin_test.cpp \
    screengl_test.cpp \
    thread_render.cpp \
    opengl_test.cpp \
    screenglglobal_test.cpp \
    glyph_cache_test.cpp \
    shape_renderer_test.cpp \

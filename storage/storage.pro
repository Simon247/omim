# Storage library.

TARGET = storage
TEMPLATE = lib
CONFIG += staticlib warn_on

ROOT_DIR = ..

include($$ROOT_DIR/common.pri)

INCLUDEPATH += $$ROOT_DIR/3party/jansson/src

HEADERS += \
  country.hpp \
  country_decl.hpp \
  country_info_getter.hpp \
  country_name_getter.hpp \
  country_polygon.hpp \
  country_tree.hpp \
  downloader_search_params.hpp \
  http_map_files_downloader.hpp \
  index.hpp \
  map_files_downloader.hpp \
  queued_country.hpp \
  storage.hpp \
  storage_defines.hpp \
  storage_helpers.hpp \

SOURCES += \
  country.cpp \
  country_decl.cpp \
  country_info_getter.cpp \
  country_name_getter.cpp \
  http_map_files_downloader.cpp \
  index.cpp \
  queued_country.cpp \
  storage.cpp \
  storage_defines.cpp \
  storage_helpers.cpp \

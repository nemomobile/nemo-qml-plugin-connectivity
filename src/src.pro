TARGET = nemoconnectivity
PLUGIN_IMPORT_PATH = org/nemomobile/connectivity

TEMPLATE = lib
CONFIG += qt plugin hide_symbols
QT += qml network dbus

target.path = $$[QT_INSTALL_QML]/$$PLUGIN_IMPORT_PATH
INSTALLS += target

qmldir.files += $$_PRO_FILE_PWD_/qmldir
qmldir.path +=  $$target.path
INSTALLS += qmldir

SOURCES += plugin.cpp connectionhelper.cpp
HEADERS += connectionhelper_p.h

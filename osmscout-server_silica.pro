# NOTICE:
#
# Application name defined in TARGET has a corresponding QML filename.
# If name defined in TARGET is changed, the following needs to be done
# to match new name:
#   - corresponding QML filename must be changed
#   - desktop icon filename must be changed
#   - desktop filename must be changed
#   - icon definition filename in desktop file must be changed
#   - translation filenames have to be changed

# The name of your application
TARGET = harbour-osmscout-server

QT += core gui network sql

CONFIG += c++11
CONFIG += sailfishapp sailfishapp_no_deploy_qml

CONFIG += use_map_qt
#CONFIG += use_map_cairo

# to disable building translations every time, comment out the
# following CONFIG line
CONFIG += sailfishapp_i18n

# installs
qml.files = qml
qml.path = /usr/share/$${TARGET}
INSTALLS += qml

stylesheets.files = stylesheets
stylesheets.path = /usr/share/$${TARGET}
INSTALLS += stylesheets

data.files = data
data.path = /usr/share/$${TARGET}
INSTALLS += data

# defines
DEFINES += APP_VERSION=\\\"$$VERSION\\\"
DEFINES += IS_SAILFISH_OS

SOURCES += \
    src/dbmaster.cpp \
    src/main.cpp \
    src/requestmapper.cpp \
    src/appsettings.cpp \
    src/dbmaster_route.cpp \
    src/dbmaster_search.cpp \
    src/dbmaster_map.cpp \
    src/osmscout-server_silica.cpp \
    src/searchresults.cpp \
    src/infohub.cpp \
    src/rollinglogger.cpp \
    src/consolelogger.cpp \
    src/routingforhuman.cpp \
    src/geomaster.cpp \
    src/config.cpp \
    src/mapmanager.cpp \
    src/filedownloader.cpp \
    src/mapmanagerfeature.cpp \
    src/sqlite/sqlite-amalgamation-3160200/sqlite3.c

OTHER_FILES += qml/osmscout-server.qml \
    qml/cover/CoverPage.qml \
    rpm/osmscout-server.spec

include(src/uhttp/uhttp.pri)
include(src/fileselector/fileselector.pri)
include(src/geocoder-nlp/geocoder-nlp.pri)

HEADERS += \
    src/dbmaster.h \
    src/requestmapper.h \
    src/appsettings.h \
    src/config.h \
    src/searchresults.h \
    src/infohub.h \
    src/rollinglogger.h \
    src/consolelogger.h \
    src/routingforhuman.h \
    src/geomaster.h \
    src/mapmanager.h \
    src/filedownloader.h \
    src/mapmanagerfeature.h \
    src/sqlite/sqlite-amalgamation-3160200/sqlite3.h \
    src/sqlite/sqlite-amalgamation-3160200/sqlite3ext.h

use_map_qt {
    DEFINES += USE_OSMSCOUT_MAP_QT
    LIBS += -losmscout_map_qt
}

use_map_cairo {
    DEFINES += USE_OSMSCOUT_MAP_CAIRO
    LIBS += -losmscout_map_cairo
    # those disappear if we use PKGCONFIG
    LIBS += -pie -rdynamic -L/usr/lib/ -lsailfishapp -lmdeclarativecache5
    CONFIG += link_pkgconfig
    PKGCONFIG += pango cairo
}

LIBS += -losmscout_map -losmscout -lmarisa -ldl

SAILFISHAPP_ICONS = 86x86 108x108 128x128 256x256

CONFIG(release, debug|release) {
    DEFINES += QT_NO_WARNING_OUTPUT QT_NO_DEBUG_OUTPUT
}

# German translation is enabled as an example. If you aren't
# planning to localize your app, remember to comment out the
# following TRANSLATIONS line. And also do not forget to
# modify the localized app name in the the .desktop file.
TRANSLATIONS += \
    translations/harbour-osmscout-server-cs.ts \
    translations/harbour-osmscout-server-de.ts \
    translations/harbour-osmscout-server-es.ts \
    translations/harbour-osmscout-server-fr.ts \
    translations/harbour-osmscout-server-nl.ts \
    translations/harbour-osmscout-server-no.ts \
    translations/harbour-osmscout-server-pl.ts \
    translations/harbour-osmscout-server-ru.ts \
    translations/harbour-osmscout-server-sv.ts

DISTFILES += \
    qml/pages/StartPage.qml \
    qml/pages/AboutPage.qml \
    harbour-osmscout-server.desktop \
    rpm/harbour-osmscout-server.yaml \
    rpm/harbour-osmscout-server.changes \
    rpm/harbour-osmscout-server.spec


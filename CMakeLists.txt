cmake_minimum_required(VERSION 3.16)
project(CyberNotePasQt VERSION 2.6.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Indispensable pour que Qt Creator / CMake trouvent les .ui, .h avec Q_OBJECT, etc.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Les .ui sont dans src/, on le dit à AUTOUIC
set(CMAKE_AUTOUIC_SEARCH_PATHS src)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui Network Svg)

qt_standard_project_setup()

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/mainwindow.ui
    src/helpdialog.cpp
    src/helpdialog.h
    src/docx_zip.cpp
    src/docx_zip.h
    src/docx_xml.cpp
    src/docx_xml.h
    src/docx_model.h
    src/docx_internal.h
    src/docx_common.cpp
    src/docx_package.cpp
    src/docx_save.cpp
    src/docx_markdown.cpp
    src/docxconvert.cpp
    src/docxconvert.h
    src/docxedit.cpp
    src/docxedit.h
    src/reportwindow.cpp
    src/reportwindow.h
    src/insertnotesdialog.cpp
    src/insertnotesdialog.h
    src/linuxtermdialog.cpp
    src/linuxtermdialog.h
    src/linuxtermdialog.ui
    src/netcattermdialog.cpp
    src/netcattermdialog.h
    src/netcattermdialog.ui
    src/ptyprocess.cpp
    src/ptyprocess.h
    src/terminalscreen.cpp
    src/terminalscreen.h
    src/terminalwidget.cpp
    src/terminalwidget.h
    src/markdownedit.cpp
    src/markdownedit.h
    src/markdownhighlighter.cpp
    src/markdownhighlighter.h
    src/markdownkeywords.h
    src/configfile.cpp
    src/configfile.h
    src/httpfileserver.cpp
    src/httpfileserver.h
    resources/resources.qrc
)

qt_add_executable(CyberNoteQt ${SOURCES})

target_link_libraries(CyberNoteQt PRIVATE Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Network Qt6::Svg util)

target_include_directories(CyberNoteQt PRIVATE src)

# Sur Kali/Linux, installe simplement les paquets suivants avant de builder :
#   sudo apt install qt6-base-dev qt6-tools-dev qt6-tools-dev-tools cmake build-essential
#
# Compilation :
#   mkdir build && cd build
#   cmake ..
#   cmake --build . -j$(nproc)
#
# Le projet s'ouvre aussi directement dans Qt Creator (File > Open File or Project > CMakeLists.txt),
# et chaque .ui s'ouvre individuellement dans Qt Designer (clic droit > "Open With" > Qt Designer,
# ou double-clic depuis Qt Creator qui lance l'éditeur de formulaire intégré = Qt Designer).

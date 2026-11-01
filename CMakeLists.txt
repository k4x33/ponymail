cmake_minimum_required(VERSION 3.21)
project(PonyMail VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

option(PONYMAIL_BUILD_TESTS "Build unit tests" ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Widgets Network Sql
    WebEngineWidgets WebChannel)

# ---------------------------------------------------------------------------
# Core library (everything except main.cpp) so tests can link against it.
# ---------------------------------------------------------------------------
set(PONYMAIL_SOURCES
    src/core/Paths.cpp
    src/core/Settings.cpp
    src/core/Util.cpp
    src/storage/Database.cpp
    src/auth/CredentialStore.cpp
    src/auth/OAuthManager.cpp
    src/gmail/GmailClient.cpp
    src/gmail/MimeMessage.cpp
    src/gmail/MimeBuilder.cpp
    src/model/Triage.cpp
    src/model/SenderResolver.cpp
    src/model/MailStore.cpp
    src/sync/SyncEngine.cpp
    src/ai/ClaudeClient.cpp
    src/ai/SummaryQueue.cpp
    src/ui/Theme.cpp
    src/ui/Icons.cpp
    src/ui/MainWindow.cpp
    src/ui/NavRail.cpp
    src/ui/MessageListModel.cpp
    src/ui/MessageListDelegate.cpp
    src/ui/MessageListView.cpp
    src/ui/ReadingPane.cpp
    src/ui/RecipientField.cpp
    src/ui/ComposeWindow.cpp
    src/ui/AiDialog.cpp
    src/ui/SettingsDialogs.cpp
)

set(PONYMAIL_HEADERS
    src/core/Paths.h
    src/core/Settings.h
    src/core/Util.h
    src/storage/Database.h
    src/auth/CredentialStore.h
    src/auth/OAuthManager.h
    src/gmail/GmailClient.h
    src/gmail/MimeMessage.h
    src/gmail/MimeBuilder.h
    src/model/Models.h
    src/model/Triage.h
    src/model/SenderResolver.h
    src/model/MailStore.h
    src/sync/SyncEngine.h
    src/ai/ClaudeClient.h
    src/ai/SummaryQueue.h
    src/ui/Theme.h
    src/ui/Icons.h
    src/ui/MainWindow.h
    src/ui/NavRail.h
    src/ui/MessageListModel.h
    src/ui/MessageListDelegate.h
    src/ui/MessageListView.h
    src/ui/ReadingPane.h
    src/ui/RecipientField.h
    src/ui/ComposeWindow.h
    src/ui/AiDialog.h
    src/ui/SettingsDialogs.h
)

add_library(ponymail_core STATIC ${PONYMAIL_SOURCES} ${PONYMAIL_HEADERS} resources/ponymail.qrc)
target_include_directories(ponymail_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ponymail_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Sql
    Qt6::WebEngineWidgets Qt6::WebChannel)
target_compile_definitions(ponymail_core PUBLIC
    PONYMAIL_VERSION="${PROJECT_VERSION}"
    QT_NO_KEYWORDS)

if(WIN32)
    target_link_libraries(ponymail_core PUBLIC advapi32)
    target_compile_definitions(ponymail_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
if(WIN32)
    set(APP_ICON_RC "${CMAKE_CURRENT_SOURCE_DIR}/resources/ponymail.rc")
    add_executable(PonyMail WIN32 src/main.cpp ${APP_ICON_RC})
else()
    add_executable(PonyMail src/main.cpp)
endif()
target_link_libraries(PonyMail PRIVATE ponymail_core)
set_target_properties(PonyMail PROPERTIES OUTPUT_NAME "PonyMail")

install(TARGETS PonyMail RUNTIME DESTINATION .)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(PONYMAIL_BUILD_TESTS)
    find_package(Qt6 COMPONENTS Test)
    if(Qt6Test_FOUND)
        enable_testing()
        add_executable(ponymail_tests tests/test_main.cpp)
        target_link_libraries(ponymail_tests PRIVATE ponymail_core Qt6::Test)
        add_test(NAME ponymail_tests COMMAND ponymail_tests)
    endif()
endif()

#pragma once
// Keeps tests away from the developer's real settings / autosave directory.
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {
struct TestEnv {
    QTemporaryDir dir;
    TestEnv() {
        qputenv("XDG_CONFIG_HOME", (dir.path() + "/config").toUtf8());
        qputenv("XDG_DATA_HOME", (dir.path() + "/data").toUtf8());
        qputenv("HOME", dir.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
    }
};
static TestEnv g_testEnv;
}  // namespace

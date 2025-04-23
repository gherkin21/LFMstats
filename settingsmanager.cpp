#include "settingsmanager.h"
#include <QDebug> // For logging
#include <QCoreApplication> // For application info used in QSettings path
#include <QStandardPaths> // For standard locations (optional)

SettingsManager::SettingsManager(QObject *parent)
    // Using INI format in user scope for better cross-platform readability/editability.
    // Automatically determines storage location based on OS and org/app names.
    : QObject(parent),
    m_settings(QSettings::IniFormat, QSettings::UserScope,
               QCoreApplication::organizationName().isEmpty() ? "YourOrgName" : QCoreApplication::organizationName(),
               QCoreApplication::applicationName().isEmpty() ? "LastFmApp" : QCoreApplication::applicationName())
{
    qInfo() << "Settings file location:" << m_settings.fileName();
    // Check if file is writable?
    if (!m_settings.isWritable()) {
        qWarning() << "Settings file is not writable! Location:" << m_settings.fileName();
    }
}

// --- Basic Settings ---
void SettingsManager::saveApiKey(const QString &apiKey) {
    m_settings.setValue(KEY_API_KEY, apiKey);
}

QString SettingsManager::apiKey() const {
    return m_settings.value(KEY_API_KEY, "").toString();
}

void SettingsManager::saveUsername(const QString &username) {
    m_settings.setValue(KEY_USERNAME, username);
}

QString SettingsManager::username() const {
    return m_settings.value(KEY_USERNAME, "").toString();
}

// --- Initial Fetch State ---
void SettingsManager::setInitialFetchComplete(bool complete) {
    // Only write if the value actually changes to minimize disk I/O
    if (m_settings.value(KEY_INITIAL_FETCH_COMPLETE, false).toBool() != complete) {
        qInfo() << "Settings: Setting initialFetchComplete to" << complete;
        m_settings.setValue(KEY_INITIAL_FETCH_COMPLETE, complete);
        m_settings.sync(); // Ensure change is written to persistent storage immediately
    }
}

bool SettingsManager::isInitialFetchComplete() const {
    // Default to false if the setting doesn't exist yet in the settings file
    return m_settings.value(KEY_INITIAL_FETCH_COMPLETE, false).toBool();
}

// --- Resume State ---
void SettingsManager::saveLastSuccessfullySavedPage(int page) {
    // Only write if the value changes
    if (m_settings.value(KEY_LAST_SAVED_PAGE, 0).toInt() != page) {
        qInfo() << "Settings: Saving lastSuccessfullySavedPage =" << page;
        m_settings.setValue(KEY_LAST_SAVED_PAGE, page);
        m_settings.sync(); // Write change immediately for robustness on resume
    }
}

int SettingsManager::loadLastSuccessfullySavedPage() const {
    // Default to 0 if not set (means start from page 1 for resume, as page numbering is 1-based)
    return m_settings.value(KEY_LAST_SAVED_PAGE, 0).toInt();
}

void SettingsManager::saveExpectedTotalPages(int totalPages) {
    // Only write if the value changes
    if (m_settings.value(KEY_EXPECTED_TOTAL_PAGES, 0).toInt() != totalPages) {
        qInfo() << "Settings: Saving expectedTotalPages =" << totalPages;
        m_settings.setValue(KEY_EXPECTED_TOTAL_PAGES, totalPages);
        m_settings.sync(); // Write change immediately
    }
}

int SettingsManager::loadExpectedTotalPages() const {
    // Default to 0 if unknown (indicates total pages haven't been determined yet)
    return m_settings.value(KEY_EXPECTED_TOTAL_PAGES, 0).toInt();
}

void SettingsManager::clearResumeState() {
    qInfo() << "Settings: Clearing resume state (lastSavedPage, expectedTotalPages).";
    bool changed = false;
    // Check if keys exist before removing to avoid unnecessary syncs
    if (m_settings.contains(KEY_LAST_SAVED_PAGE)) {
        m_settings.remove(KEY_LAST_SAVED_PAGE);
        changed = true;
    }
    if (m_settings.contains(KEY_EXPECTED_TOTAL_PAGES)) {
        m_settings.remove(KEY_EXPECTED_TOTAL_PAGES);
        changed = true;
    }
    if (changed) {
        m_settings.sync(); // Ensure removals are written
    }
}

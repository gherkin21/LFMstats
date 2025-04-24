/**
 * @file settingsmanager.cpp
 * @brief Implementation of the SettingsManager class.
 */

#include "settingsmanager.h"
#include <QCoreApplication>
#include <QDebug>
#include <QStandardPaths>

SettingsManager::SettingsManager(QObject *parent)

    : QObject(parent), m_settings(QSettings::IniFormat, QSettings::UserScope,
                                  QCoreApplication::organizationName().isEmpty()
                                      ? "gherk"
                                      : QCoreApplication::organizationName(),
                                  QCoreApplication::applicationName().isEmpty()
                                      ? "LastFmApp"
                                      : QCoreApplication::applicationName()) {
  qInfo() << "Settings file location:" << m_settings.fileName();

  if (!m_settings.isWritable()) {
    qWarning() << "Settings file is not writable! Location:"
               << m_settings.fileName();
  }
}

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

void SettingsManager::setInitialFetchComplete(bool complete) {

  if (m_settings.value(KEY_INITIAL_FETCH_COMPLETE, false).toBool() !=
      complete) {
    qInfo() << "Settings: Setting initialFetchComplete to" << complete;
    m_settings.setValue(KEY_INITIAL_FETCH_COMPLETE, complete);
    m_settings.sync();
  }
}

bool SettingsManager::isInitialFetchComplete() const {

  return m_settings.value(KEY_INITIAL_FETCH_COMPLETE, false).toBool();
}

void SettingsManager::saveLastSuccessfullySavedPage(int page) {

  if (m_settings.value(KEY_LAST_SAVED_PAGE, 0).toInt() != page) {
    qInfo() << "Settings: Saving lastSuccessfullySavedPage =" << page;
    m_settings.setValue(KEY_LAST_SAVED_PAGE, page);
    m_settings.sync();
  }
}

int SettingsManager::loadLastSuccessfullySavedPage() const {

  return m_settings.value(KEY_LAST_SAVED_PAGE, 0).toInt();
}

void SettingsManager::saveExpectedTotalPages(int totalPages) {

  if (m_settings.value(KEY_EXPECTED_TOTAL_PAGES, 0).toInt() != totalPages) {
    qInfo() << "Settings: Saving expectedTotalPages =" << totalPages;
    m_settings.setValue(KEY_EXPECTED_TOTAL_PAGES, totalPages);
    m_settings.sync();
  }
}

int SettingsManager::loadExpectedTotalPages() const {

  return m_settings.value(KEY_EXPECTED_TOTAL_PAGES, 0).toInt();
}

void SettingsManager::clearResumeState() {
  qInfo()
      << "Settings: Clearing resume state (lastSavedPage, expectedTotalPages).";
  bool changed = false;

  if (m_settings.contains(KEY_LAST_SAVED_PAGE)) {
    m_settings.remove(KEY_LAST_SAVED_PAGE);
    changed = true;
  }
  if (m_settings.contains(KEY_EXPECTED_TOTAL_PAGES)) {
    m_settings.remove(KEY_EXPECTED_TOTAL_PAGES);
    changed = true;
  }
  if (changed) {
    m_settings.sync();
  }
}

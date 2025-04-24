#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "ui_aboutpage.h"
#include "ui_artistspage.h"
#include "ui_chartspage.h"
#include "ui_databasetablepage.h"
#include "ui_generalstatspage.h"
#include "ui_trackspage.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaType>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>

#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QChart>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLegendMarker>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_fetchingComplete(false),
      m_expectedTotalPages(0), m_lastSuccessfullySavedPage(0),
      m_currentState(AppState::Idle) {
  qRegisterMetaType<ListeningStreak>("ListeningStreak");
  qRegisterMetaType<SortedCounts>("SortedCounts");

  ui->setupUi(this);
  setWindowTitle("Last.fm Scrobble Analyzer");
  updateStatusBarState();
  setupPages();

  connect(ui->menuListWidget, &QListWidget::currentItemChanged, this,
          &MainWindow::onMenuItemChanged);

  QPushButton *fetchBtn =
      aboutPage ? aboutPage->findChild<QPushButton *>("fetchButton") : nullptr;
  if (fetchBtn) {
    connect(fetchBtn, &QPushButton::clicked, this,
            &MainWindow::fetchNewScrobbles);
  } else {
    qWarning() << "Could not find fetchButton on About page!";
  }
  QPushButton *settingsBtn =
      aboutPage ? aboutPage->findChild<QPushButton *>("settingsButton")
                : nullptr;
  if (settingsBtn) {
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::setupUser);
  } else {
    qWarning() << "Could not find settingsButton on About page!";
  }

  if (m_meanRangeComboBox) {
    connect(m_meanRangeComboBox, &QComboBox::currentIndexChanged, this,
            &MainWindow::updateMeanScrobbleCalculation);
  } else {
    qWarning() << "Could not find meanRangeComboBox during setup!";
  }

  if (m_findLastPlayedButton) {
    connect(m_findLastPlayedButton, &QPushButton::clicked, this,
            &MainWindow::findLastPlayedTrack);

    if (m_artistInput)
      connect(m_artistInput, &QLineEdit::returnPressed, m_findLastPlayedButton,
              &QPushButton::click);
    if (m_trackInput)
      connect(m_trackInput, &QLineEdit::returnPressed, m_findLastPlayedButton,
              &QPushButton::click);
  } else {
    qWarning() << "Could not find findLastPlayedButton during setup!";
  }

  connect(&m_lastFmManager, &LastFmManager::pageReadyForSaving, this,
          &MainWindow::handleSavePageOfScrobbles);
  connect(&m_lastFmManager, &LastFmManager::totalPagesDetermined, this,
          &MainWindow::handleTotalPagesDetermined);
  connect(&m_lastFmManager, &LastFmManager::fetchFinished, this,
          &MainWindow::handleFetchFinished);
  connect(&m_lastFmManager, &LastFmManager::fetchError, this,
          &MainWindow::handleApiError);
  connect(&m_databaseManager, &DatabaseManager::pageSaveCompleted, this,
          &MainWindow::handlePageSaveComplete);
  connect(&m_databaseManager, &DatabaseManager::pageSaveFailed, this,
          &MainWindow::handlePageSaveFailed);
  connect(&m_databaseManager, &DatabaseManager::loadComplete, this,
          &MainWindow::handleDbLoadComplete);
  connect(&m_databaseManager, &DatabaseManager::loadError, this,
          &MainWindow::handleDbLoadError);
  connect(&m_databaseManager, &DatabaseManager::statusMessage, this,
          &MainWindow::handleDbStatusUpdate);

  connect(&m_analysisWatcher, &QFutureWatcher<AnalysisResults>::finished, this,
          &MainWindow::handleAnalysisComplete);
  connect(&m_initialDbLoadWatcher,
          &QFutureWatcher<QList<ScrobbleData>>::finished, this,
          &MainWindow::handleInitialDbLoadComplete);

  setupMenu();
  promptForSettings();
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::updateStatusBarState() {
  QString message = "Ready.";
  bool busy = false;
  switch (m_currentState) {
  case AppState::Idle:
    message = "Ready.";
    break;
  case AppState::LoadingDb:
    message = "Loading data from disk...";
    busy = true;
    break;
  case AppState::Analyzing:
    message = "Analyzing data...";
    busy = true;
    break;
  case AppState::FetchingApi:
    message = "Fetching from Last.fm...";
    busy = true;
    break;
  case AppState::SavingDb:
    message = "Saving data to disk...";
    busy = true;
    break;
  }
  ui->statusbar->showMessage(message);

  ui->menuListWidget->setEnabled(!busy);

  QPushButton *fetchBtn =
      aboutPage ? aboutPage->findChild<QPushButton *>("fetchButton") : nullptr;
  if (fetchBtn)
    fetchBtn->setEnabled(m_currentState == AppState::Idle);

  if (m_findLastPlayedButton)
    m_findLastPlayedButton->setEnabled(m_currentState == AppState::Idle &&
                                       !m_loadedScrobbles.isEmpty());
}

void MainWindow::setupPages() {
  while (ui->stackedWidget->count() > 0) {
    ui->stackedWidget->removeWidget(ui->stackedWidget->widget(0));
  }

  Ui::GeneralStatsPage ui_gs;
  generalStatsPage = new QWidget();
  ui_gs.setupUi(generalStatsPage);
  m_statsLabel = ui_gs.statsLabel;

  m_firstScrobbleLabelValue =
      generalStatsPage->findChild<QLabel *>("firstScrobbleLabelValue");
  m_lastScrobbleLabelValue =
      generalStatsPage->findChild<QLabel *>("lastScrobbleLabelValue");
  m_meanRangeComboBox =
      generalStatsPage->findChild<QComboBox *>("meanRangeComboBox");
  m_meanScrobblesResultLabel =
      generalStatsPage->findChild<QLabel *>("meanScrobblesResultLabel");
  m_longestStreakLabelValue =
      generalStatsPage->findChild<QLabel *>("longestStreakLabelValue");
  m_currentStreakLabelValue =
      generalStatsPage->findChild<QLabel *>("currentStreakLabelValue");
  m_artistInput = generalStatsPage->findChild<QLineEdit *>("artistInput");
  m_trackInput = generalStatsPage->findChild<QLineEdit *>("trackInput");
  m_findLastPlayedButton =
      generalStatsPage->findChild<QPushButton *>("findLastPlayedButton");
  m_lastPlayedResultLabel =
      generalStatsPage->findChild<QLabel *>("lastPlayedResultLabel");

  if (m_meanRangeComboBox && m_meanRangeComboBox->count() == 0) {
    m_meanRangeComboBox->addItems(
        {"Last 7 Days", "Last 30 Days", "Last 90 Days", "All Time"});
    m_meanRangeComboBox->setToolTip(
        "Calculate average scrobbles per day over selected period");
  }

  if (m_artistInput)
    m_artistInput->setPlaceholderText("Artist Name");
  if (m_trackInput)
    m_trackInput->setPlaceholderText("Track Name");

  Ui::DatabaseTablePage ui_dt;
  databaseTablePage = new QWidget();
  ui_dt.setupUi(databaseTablePage);
  m_dbTableWidget = ui_dt.dbTableWidget;
  Ui::ArtistsPage ui_a;
  artistsPage = new QWidget();
  ui_a.setupUi(artistsPage);
  m_artistListWidget = ui_a.artistListWidget;
  if (m_artistListWidget) {
    connect(m_artistListWidget, &QListWidget::itemDoubleClicked, this,
            &MainWindow::onArtistItemDoubleClicked);
  } else {
    qWarning() << "m_artistListWidget is null during connection setup!";
  }

  Ui::TracksPage ui_t;
  tracksPage = new QWidget();
  ui_t.setupUi(tracksPage);
  m_trackListWidget = ui_t.trackListWidget;
  if (m_trackListWidget) {
    connect(m_trackListWidget, &QListWidget::itemDoubleClicked, this,
            &MainWindow::onTrackItemDoubleClicked);
  } else {
    qWarning() << "m_trackListWidget is null during connection setup!";
  }

  Ui::ChartsPage ui_c;
  chartsPage = new QWidget();
  ui_c.setupUi(chartsPage);
  m_artistsChartView = chartsPage->findChild<QChartView *>("artistsChartView");
  m_tracksChartView = chartsPage->findChild<QChartView *>("tracksChartView");
  m_hourlyChartView = chartsPage->findChild<QChartView *>("hourlyChartView");
  m_weeklyChartView = chartsPage->findChild<QChartView *>("weeklyChartView");
  Ui::AboutPage ui_ab;
  aboutPage = new QWidget();
  ui_ab.setupUi(aboutPage);
  m_currentUserLabel = ui_ab.currentUserLabel;

  ui->stackedWidget->addWidget(generalStatsPage);
  ui->stackedWidget->addWidget(databaseTablePage);
  ui->stackedWidget->addWidget(artistsPage);
  ui->stackedWidget->addWidget(tracksPage);
  ui->stackedWidget->addWidget(chartsPage);
  ui->stackedWidget->addWidget(aboutPage);

  if (!m_firstScrobbleLabelValue)
    qWarning(
        "firstScrobbleLabelValue not found! Check object name in UI file.");
  if (!m_lastScrobbleLabelValue)
    qWarning("lastScrobbleLabelValue not found!");
  if (!m_meanRangeComboBox)
    qWarning("meanRangeComboBox not found!");
  if (!m_meanScrobblesResultLabel)
    qWarning("meanScrobblesResultLabel not found!");
  if (!m_longestStreakLabelValue)
    qWarning("longestStreakLabelValue not found!");
  if (!m_currentStreakLabelValue)
    qWarning("currentStreakLabelValue not found!");
  if (!m_artistInput)
    qWarning("artistInput not found!");
  if (!m_trackInput)
    qWarning("trackInput not found!");
  if (!m_findLastPlayedButton)
    qWarning("findLastPlayedButton not found!");
  if (!m_lastPlayedResultLabel)
    qWarning("lastPlayedResultLabel not found!");
  if (!m_artistsChartView)
    qWarning("artistsChartView not found!");
  if (!m_tracksChartView)
    qWarning("tracksChartView not found!");
  if (!m_hourlyChartView)
    qWarning("hourlyChartView not found!");
  if (!m_weeklyChartView)
    qWarning("weeklyChartView not found!");
}

void MainWindow::setupMenu() {
  ui->menuListWidget->clear();
  ui->menuListWidget->addItem("Dashboard / Stats");
  ui->menuListWidget->addItem("Database View");
  ui->menuListWidget->addItem("Top Artists");
  ui->menuListWidget->addItem("Top Tracks");
  ui->menuListWidget->addItem("Charts");
  ui->menuListWidget->addItem("About / Settings");
  ui->menuListWidget->setCurrentRow(0);
}

void MainWindow::onArtistItemDoubleClicked(QListWidgetItem *item) {
  if (!item)
    return;

  QString itemText = item->text();
  int lastParen = itemText.lastIndexOf('(');
  if (lastParen == -1) {
    qWarning() << "Could not parse artist item text:" << itemText;
    return;
  }

  QString artistName = itemText.left(lastParen).trimmed();
  if (artistName.isEmpty()) {
    qWarning() << "Parsed empty artist name from:" << itemText;
    return;
  }

  QString encodedArtist =
      QUrl::toPercentEncoding(artistName, QByteArray(), QByteArray("+"));

  QUrl url("https://www.last.fm/music/" + encodedArtist);

  qInfo() << "Opening artist URL:" << url.toString();
  if (!QDesktopServices::openUrl(url)) {
    qWarning() << "Failed to open URL:" << url.toString();
    QMessageBox::warning(this, "Error",
                         "Could not open the artist page in your browser.");
  }
}

void MainWindow::onTrackItemDoubleClicked(QListWidgetItem *item) {
  if (!item)
    return;

  QString itemText = item->text();

  int lastParen = itemText.lastIndexOf('(');
  if (lastParen == -1) {
    qWarning() << "Could not parse track item text (parenthesis):" << itemText;
    return;
  }

  QString fullTrackInfo = itemText.left(lastParen).trimmed();

  int separatorPos = fullTrackInfo.indexOf(" - ");
  if (separatorPos <= 0 || separatorPos >= fullTrackInfo.length() - 3) {
    qWarning() << "Could not parse track item text (separator ' - '):"
               << fullTrackInfo;
    QString encodedArtist =
        QUrl::toPercentEncoding(fullTrackInfo, QByteArray(), QByteArray("+"));
    QUrl url("https://www.last.fm/music/" + encodedArtist);
    qInfo() << "Falling back to artist URL:" << url.toString();
    if (!QDesktopServices::openUrl(url)) {
      qWarning() << "Failed to open fallback URL:" << url.toString();
      QMessageBox::warning(
          this, "Error", "Could not parse track or open page in your browser.");
    }
    return;
  }

  QString artistName = fullTrackInfo.left(separatorPos).trimmed();
  QString trackName = fullTrackInfo.mid(separatorPos + 3).trimmed();

  if (artistName.isEmpty() || trackName.isEmpty()) {
    qWarning() << "Parsed empty artist or track name from:" << fullTrackInfo;
    return;
  }

  QString encodedArtist =
      QUrl::toPercentEncoding(artistName, QByteArray(), QByteArray("+"));
  QString encodedTrack =
      QUrl::toPercentEncoding(trackName, QByteArray(), QByteArray("+"));

  QUrl url("https://www.last.fm/music/" + encodedArtist + "/_/" + encodedTrack);

  qInfo() << "Opening track URL:" << url.toString();
  if (!QDesktopServices::openUrl(url)) {
    qWarning() << "Failed to open URL:" << url.toString();
    QMessageBox::warning(this, "Error",
                         "Could not open the track page in your browser.");
  }
}

void MainWindow::promptForSettings() {
  QString username = m_settingsManager.username();
  QString apiKey = m_settingsManager.apiKey();
  bool settingsNeeded = false;
  if (username.isEmpty()) {
    settingsNeeded = true;
    bool ok;
    username = QInputDialog::getText(this, "Setup",
                                     "Username:", QLineEdit::Normal, "", &ok);
    if (ok && !username.isEmpty())
      m_settingsManager.saveUsername(username);
    else {
      QMessageBox::warning(this, "Setup", "Username required.");
      username = "";
    }
  }
  if (apiKey.isEmpty()) {
    settingsNeeded = true;
    bool ok;
    apiKey = QInputDialog::getText(this, "Setup", "API Key:", QLineEdit::Normal,
                                   "", &ok);
    if (ok && !apiKey.isEmpty())
      m_settingsManager.saveApiKey(apiKey);
    else {
      QMessageBox::warning(this, "Setup", "API Key required.");
      apiKey = "";
    }
  }

  username = m_settingsManager.username();
  apiKey = m_settingsManager.apiKey();

  if (!username.isEmpty() && !apiKey.isEmpty()) {
    ui->profileNameLabel->setText(username);
    if (m_currentUserLabel)
      m_currentUserLabel->setText(username);
    qInfo() << "[Main Window] Calling LastFmManager::setup with API Key:"
            << (apiKey.isEmpty() ? "EMPTY" : "SET") << "Username:" << username;
    m_lastFmManager.setup(apiKey, username);

    onMenuItemChanged(ui->menuListWidget->currentItem(), nullptr);
  } else {
    ui->profileNameLabel->setText("<Required>");
    if (m_currentUserLabel)
      m_currentUserLabel->setText("<Not Set>");
    m_loadedScrobbles.clear();
    m_cachedAnalysisResults.clear();
    updateUiWithAnalysisResults(AnalysisResults());
  }
}

void MainWindow::setupUser() {
  bool ok_user, ok_key;
  QString current_user = m_settingsManager.username();
  QString current_key = m_settingsManager.apiKey();
  QString username = QInputDialog::getText(
      this, "Settings", "Username:", QLineEdit::Normal, current_user, &ok_user);
  QString apiKey = QInputDialog::getText(
      this, "Settings", "API Key:", QLineEdit::Normal, current_key, &ok_key);
  bool changed = false;
  bool userChanged = false;
  if (ok_user) {
    if (!username.isEmpty()) {
      if (username != current_user) {
        m_settingsManager.saveUsername(username);
        ui->profileNameLabel->setText(username);
        if (m_currentUserLabel)
          m_currentUserLabel->setText(username);
        changed = true;
        userChanged = true;
      }
    } else {
      QMessageBox::warning(this, "Input Error", "Username empty.");
    }
  }
  if (ok_key) {
    if (!apiKey.isEmpty()) {
      if (apiKey != current_key) {
        m_settingsManager.saveApiKey(apiKey);
        changed = true;
      }
    } else {
      QMessageBox::warning(this, "Input Error", "API Key empty.");
    }
  }
  if (changed) {
    QString currentApiKey = m_settingsManager.apiKey();
    QString currentUsername = m_settingsManager.username();
    qInfo()
        << "[Main Window] Calling LastFmManager::setup from setupUser. API Key:"
        << (currentApiKey.isEmpty() ? "EMPTY" : "SET")
        << "Username:" << currentUsername;
    m_lastFmManager.setup(currentApiKey, currentUsername);
    QMessageBox::information(
        this, "Settings Updated",
        "Settings updated. Fetch if needed.\nData cleared.");
    m_loadedScrobbles.clear();
    m_cachedAnalysisResults.clear();
    if (userChanged) {
      m_settingsManager.setInitialFetchComplete(false);
      m_settingsManager.clearResumeState();
      m_lastSuccessfullySavedPage = 0;
      m_expectedTotalPages = 0;
      qInfo() << "User changed, reset state.";
    }

    m_currentState = AppState::Idle;
    updateStatusBarState();
    updateUiWithAnalysisResults(AnalysisResults());
  }
}

void MainWindow::fetchNewScrobbles() {
  if (m_currentState != AppState::Idle) {
    QMessageBox::warning(this, "Busy", "Operation already in progress.");
    return;
  }
  QString username = m_settingsManager.username();
  QString apiKey = m_settingsManager.apiKey();
  if (username.isEmpty() || apiKey.isEmpty()) {
    QMessageBox::warning(this, "Setup", "Set Username/API Key first.");
    promptForSettings();
    return;
  }

  m_fetchingComplete = false;
  bool isUpdate = m_settingsManager.isInitialFetchComplete();
  qInfo() << "================ FETCH TRIGGERED ================";
  if (isUpdate) {
    qint64 startTimestamp = m_databaseManager.getLastSyncTimestamp(username);
    m_currentState = AppState::FetchingApi;
    updateStatusBarState();
    qInfo() << "Mode: Incremental Update since" << startTimestamp;
    m_expectedTotalPages = 0;
    m_lastSuccessfullySavedPage = 0;
    m_lastFmManager.fetchScrobblesSince(startTimestamp);
  } else {
    m_lastSuccessfullySavedPage =
        m_settingsManager.loadLastSuccessfullySavedPage();
    m_expectedTotalPages = m_settingsManager.loadExpectedTotalPages();
    int startPage = m_lastSuccessfullySavedPage + 1;
    m_currentState = AppState::FetchingApi;
    updateStatusBarState();
    qInfo() << "Mode: Full Fetch/Resume from page" << startPage
            << "(Known Total:" << m_expectedTotalPages << ")";
    m_lastFmManager.startInitialOrResumeFetch(startPage, m_expectedTotalPages);
  }
  qInfo() << "==================================================";
}

void MainWindow::onMenuItemChanged(QListWidgetItem *current,
                                   QListWidgetItem *previous) {
  Q_UNUSED(previous);
  if (!current)
    return;

  int index = ui->menuListWidget->row(current);
  if (index >= 0 && index < ui->stackedWidget->count()) {
    ui->selectedTabLabel->setText("Selected: " + current->text());
    ui->stackedWidget->setCurrentIndex(index);

    if (!m_loadedScrobbles.isEmpty()) {

      if (m_cachedAnalysisResults.isEmpty() ||
          m_currentState == AppState::Analyzing) {

        if (m_currentState == AppState::Idle) {
          startAnalysisTask();
        } else {
          qDebug() << "Analysis already running or data loading, will update "
                      "view when done.";
        }
      } else {

        updateUiWithAnalysisResults(m_cachedAnalysisResults);
        updateStatusBarState();
      }
    } else {

      if (m_currentState == AppState::Idle) {
        QString username = m_settingsManager.username();
        if (!username.isEmpty()) {
          m_currentState = AppState::LoadingDb;
          updateStatusBarState();

          m_databaseManager.loadAllScrobblesAsync(username);

        } else {
          qDebug() << "Cannot load data: No username set.";
        }

      } else {
        qDebug() << "Already busy:" << static_cast<int>(m_currentState);
      }
    }
  } else {
    qWarning() << "Invalid menu index selected:" << index;
  }
}

void MainWindow::handleSavePageOfScrobbles(
    const QList<ScrobbleData> &pageScrobbles, int pageNumber) {
  qDebug() << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
           << "- [Main] Rcvd pageReady: Page" << pageNumber
           << ", Size:" << pageScrobbles.count();
  if (!pageScrobbles.isEmpty()) {
    qDebug() << "[Main] Calling DB saveAsync page" << pageNumber;
    m_databaseManager.saveScrobblesAsync(
        pageNumber, m_settingsManager.username(), pageScrobbles);
  } else if (!m_settingsManager.isInitialFetchComplete()) {
    qWarning() << "[Main] Empty Page" << pageNumber
               << " during initial fetch. Simulating completion.";
    handlePageSaveComplete(pageNumber);
  } else {
    qDebug() << "[Main] Empty Page" << pageNumber
             << " during update, skipping save call.";
  }
}

void MainWindow::handleTotalPagesDetermined(int totalPages) {
  qInfo() << "[Main] Total pages determined:" << totalPages;
  if (m_expectedTotalPages <= 0 || totalPages != m_expectedTotalPages) {
    m_expectedTotalPages = totalPages;
    if (!m_settingsManager.isInitialFetchComplete()) {
      m_settingsManager.saveExpectedTotalPages(m_expectedTotalPages);
    }
  }
}

void MainWindow::handleFetchFinished() {
  qInfo() << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
          << "- [Main] API Fetch part finished.";
  m_fetchingComplete = true;

  checkOverallCompletion();
}

void MainWindow::handleApiError(const QString &errorString) {
  qWarning() << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
             << "- [Main] API Error:" << errorString;
  m_fetchingComplete = true;
  m_currentState = AppState::Idle;
  updateStatusBarState();

  m_settingsManager.setInitialFetchComplete(false);
  qWarning() << "API Error: Marked initial fetch as incomplete.";
  ui->statusbar->showMessage("API Error.", 5000);
  QMessageBox::critical(this, "API Error", errorString);
}

void MainWindow::handlePageSaveComplete(int pageNumber) {
  qDebug() << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
           << "- [Main] DB Save Complete: Page" << pageNumber;
  m_lastSuccessfullySavedPage = qMax(m_lastSuccessfullySavedPage, pageNumber);
  if (!m_settingsManager.isInitialFetchComplete()) {
    m_settingsManager.saveLastSuccessfullySavedPage(
        m_lastSuccessfullySavedPage);
  }

  checkOverallCompletion();
}

void MainWindow::handlePageSaveFailed(int pageNumber, const QString &error) {
  qWarning() << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
             << "- [Main] DB Save FAILED: Page" << pageNumber
             << "Err:" << error;
  m_fetchingComplete = true;
  m_currentState = AppState::Idle;
  updateStatusBarState();

  m_settingsManager.setInitialFetchComplete(false);
  qWarning() << "DB Save Error: Marked initial fetch as incomplete.";
  ui->statusbar->showMessage("Database save error!", 5000);
  QMessageBox::critical(this, "DB Save Error", error);
}

void MainWindow::checkOverallCompletion() {

  if (m_currentState != AppState::FetchingApi &&
      m_currentState != AppState::SavingDb) {

    return;
  }

  bool savingDone = !m_databaseManager.isSaveInProgress();

  if (m_fetchingComplete && savingDone) {
    qInfo() << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
            << "- [Main] Fetch/Save operations fully complete.";

    m_currentState = AppState::LoadingDb;
    updateStatusBarState();

    bool hadError =
        ui->statusbar->currentMessage().contains("Error", Qt::CaseInsensitive);
    bool wasInitial = !m_settingsManager.isInitialFetchComplete();
    if (!hadError) {
      if (wasInitial) {
        if (m_expectedTotalPages > 0 &&
            m_lastSuccessfullySavedPage >= m_expectedTotalPages) {
          qInfo() << "Initial fetch fully completed.";
          m_settingsManager.setInitialFetchComplete(true);
          m_settingsManager.clearResumeState();

        } else {
          qWarning() << "Fetch finished but incomplete! Saved:"
                     << m_lastSuccessfullySavedPage
                     << "Expected:" << m_expectedTotalPages;
          m_settingsManager.setInitialFetchComplete(false);
        }
      } else {
      }
    } else {
    }

    qInfo() << "Reloading data after fetch/save completion.";
    m_loadedScrobbles.clear();
    m_cachedAnalysisResults.clear();
    m_databaseManager.loadAllScrobblesAsync(m_settingsManager.username());

  } else if (m_fetchingComplete && !savingDone) {
    qDebug() << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
             << "- Completion check: Fetch done, waiting for DB saves...";
    m_currentState = AppState::SavingDb;
    updateStatusBarState();
  } else if (!m_fetchingComplete) {
    qDebug() << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
             << "- Completion check: Still fetching...";
    m_currentState = AppState::FetchingApi;
    updateStatusBarState();
  }
}

void MainWindow::handleDbLoadComplete(const QList<ScrobbleData> &scrobbles) {
  qInfo() << "Database load complete, Scrobble count:" << scrobbles.count();
  m_loadedScrobbles = scrobbles;
  m_cachedAnalysisResults.clear();

  if (m_currentState == AppState::LoadingDb ||
      m_currentState == AppState::SavingDb ||
      m_currentState == AppState::FetchingApi) {
    startAnalysisTask();
  } else if (m_currentState == AppState::Idle) {

    startAnalysisTask();
  }

  if (!m_settingsManager.isInitialFetchComplete() && !scrobbles.isEmpty()) {
    qWarning() << "Loaded data, but initial full fetch may be incomplete.";
    QTimer::singleShot(5100, this, [this]() {
      if (this->isVisible() && !m_settingsManager.isInitialFetchComplete()) {
        QString msg = ui->statusbar->currentMessage();
      }
    });
  } else if (!m_settingsManager.isInitialFetchComplete() &&
             scrobbles.isEmpty()) {
    qInfo() << "No data loaded. Initial fetch needed.";
  }
}

void MainWindow::handleDbLoadError(const QString &error) {
  qWarning() << "Database load error:" << error;
  m_loadedScrobbles.clear();
  m_cachedAnalysisResults.clear();
  m_currentState = AppState::Idle;
  updateStatusBarState();
  updateUiWithAnalysisResults(AnalysisResults());
  QMessageBox::critical(this, "DB Load Error", error);
}

void MainWindow::startAnalysisTask() {
  if (m_currentState == AppState::Analyzing) {
    qDebug() << "Analysis task requested but already running.";
    return;
  }
  if (m_loadedScrobbles.isEmpty()) {
    qWarning() << "Analysis task requested but no data loaded.";
    m_currentState = AppState::Idle;
    updateStatusBarState();
    updateUiWithAnalysisResults(AnalysisResults());
    return;
  }

  m_currentState = AppState::Analyzing;
  updateStatusBarState();

  QList<ScrobbleData> dataToAnalyze = m_loadedScrobbles;
  AnalyticsEngine *engine = &m_analyticsEngine;

  QFuture<AnalysisResults> future =
      QtConcurrent::run([engine, dataToAnalyze]() {
        qDebug() << "[Analysis Task] Starting analysis in thread"
                 << QThread::currentThreadId();

        AnalysisResults results = engine->analyzeAll(dataToAnalyze, 100);
        qDebug() << "[Analysis Task] Analysis finished in thread"
                 << QThread::currentThreadId();
        return results;
      });
  m_analysisWatcher.setFuture(future);
}

void MainWindow::handleAnalysisComplete() {
  if (m_currentState != AppState::Analyzing) {
    qWarning() << "Analysis finished but state was not Analyzing!";
  }

  AnalysisResults results = m_analysisWatcher.result();
  qDebug() << "Analysis complete. Updating UI.";
  m_cachedAnalysisResults = results;
  m_currentState = AppState::Idle;
  updateStatusBarState();

  updateUiWithAnalysisResults(results);
}

void MainWindow::handleInitialDbLoadComplete() {
  qWarning()
      << "handleInitialDbLoadComplete called, but this watcher is deprecated.";
}

void MainWindow::updateUiWithAnalysisResults(const AnalysisResults &results) {
  int index = ui->stackedWidget->currentIndex();
  qDebug() << "Updating view for index:" << index << "with results.";

  if (results.isEmpty()) {
    qDebug() << "Results are empty, clearing views.";
  }

  switch (index) {
  case 0:
    updateGeneralStatsView(results);
    break;
  case 1:
    updateDatabaseTableView(results);
    break;
  case 2:
    updateArtistsView(results);
    break;
  case 3:
    updateTracksView(results);
    break;
  case 4:
    updateChartsView(results);
    break;
  case 5:
    updateAboutView();
    break;
  default:
    qWarning() << "UpdateDisplay invalid index:" << index;
    break;
  }

  updateStatusBarState();
}

void MainWindow::handleDbStatusUpdate(const QString &message) {
  if (message.contains("Error", Qt::CaseInsensitive)) {
    ui->statusbar->showMessage(message);
  } else if (m_currentState != AppState::Idle ||
             message.contains("Idle", Qt::CaseInsensitive) ||
             message.contains("complete", Qt::CaseInsensitive)) {
    ui->statusbar->showMessage(message);
  } else {
    ui->statusbar->showMessage(message, 2500);
  }
}

void MainWindow::updateMeanScrobbleCalculation() {
  if (m_loadedScrobbles.isEmpty() || !m_meanRangeComboBox ||
      !m_meanScrobblesResultLabel) {
    if (m_meanScrobblesResultLabel)
      m_meanScrobblesResultLabel->setText("N/A");
    return;
  }

  QString selectedRange = m_meanRangeComboBox->currentText();
  QDateTime fromDateUTC;
  QDateTime toDateUTC;

  QDateTime lastScrobbleUTC =
      m_analyticsEngine.getLastScrobbleDate(m_loadedScrobbles);
  if (lastScrobbleUTC.isNull()) {

    lastScrobbleUTC = QDateTime::currentDateTimeUtc();
  }

  toDateUTC = lastScrobbleUTC.addSecs(1);

  if (selectedRange == "Last 7 Days") {
    fromDateUTC = toDateUTC.addDays(-7);
  } else if (selectedRange == "Last 30 Days") {
    fromDateUTC = toDateUTC.addDays(-30);
  } else if (selectedRange == "Last 90 Days") {
    fromDateUTC = toDateUTC.addDays(-90);
  } else if (selectedRange == "All Time") {
    fromDateUTC = m_analyticsEngine.getFirstScrobbleDate(m_loadedScrobbles);
    if (fromDateUTC.isNull()) {
      m_meanScrobblesResultLabel->setText("Error: No Date Range");
      return;
    }
  } else {
    m_meanScrobblesResultLabel->setText("Invalid range");
    return;
  }

  qDebug() << "Calculating mean for UTC range:"
           << fromDateUTC.toString(Qt::ISODate) << "to"
           << toDateUTC.toString(Qt::ISODate);

  double mean = m_analyticsEngine.getMeanScrobblesPerDay(
      m_loadedScrobbles, fromDateUTC, toDateUTC);
  m_meanScrobblesResultLabel->setText(QString::number(mean, 'f', 2));
}

void MainWindow::findLastPlayedTrack() {
  if (!m_artistInput || !m_trackInput || !m_lastPlayedResultLabel) {
    return;
  }
  QString artist = m_artistInput->text().trimmed();
  QString track = m_trackInput->text().trimmed();
  if (artist.isEmpty() || track.isEmpty()) {
    m_lastPlayedResultLabel->setText(
        "<i style='color: red;'>Enter Artist & Track</i>");
    return;
  }
  if (m_loadedScrobbles.isEmpty()) {
    m_lastPlayedResultLabel->setText(
        "<i style='color: orange;'>No data loaded</i>");
    return;
  }

  m_lastPlayedResultLabel->setText("<i>Searching...</i>");
  QCoreApplication::processEvents();

  QDateTime lastPlayedUTC =
      m_analyticsEngine.findLastPlayed(m_loadedScrobbles, artist, track);

  if (lastPlayedUTC.isValid()) {
    m_lastPlayedResultLabel->setText(
        lastPlayedUTC.toLocalTime().toString("dd MMM yyyy 'at' hh:mm"));
  } else {
    m_lastPlayedResultLabel->setText("<i>Not found in history</i>");
  }
}

void MainWindow::updateGeneralStatsView(const AnalysisResults &results) {

  if (!m_firstScrobbleLabelValue || !m_lastScrobbleLabelValue ||
      !m_longestStreakLabelValue || !m_currentStreakLabelValue ||
      !m_meanScrobblesResultLabel || !m_lastPlayedResultLabel) {
    qWarning("GeneralStatsView: UI pointers invalid!");
    return;
  }

  QString firstDateStr = "N/A", lastDateStr = "N/A", longestStreakStr = "N/A",
          currentStreakStr = "N/A", meanStr = "N/A";

  if (!results.isEmpty()) {
    QDateTime firstDateUTC = results.value("firstDate").toDateTime();
    QDateTime lastDateUTC = results.value("lastDate").toDateTime();

    ListeningStreak streak = results.value("streak").value<ListeningStreak>();

    if (firstDateUTC.isValid())
      firstDateStr = firstDateUTC.toLocalTime().toString("dd MMM yyyy");
    if (lastDateUTC.isValid())
      lastDateStr = lastDateUTC.toLocalTime().toString("dd MMM yyyy");

    longestStreakStr = QString("%1 day(s)").arg(streak.longestStreakDays);
    if (streak.longestStreakDays > 0 && streak.longestStreakEndDate.isValid()) {
      longestStreakStr +=
          QString(" (ending %1)")
              .arg(streak.longestStreakEndDate.toString("dd MMM yy"));
    }
    currentStreakStr = QString("%1 day(s)").arg(streak.currentStreakDays);
    if (streak.currentStreakDays > 0 &&
        streak.currentStreakStartDate.isValid()) {
      currentStreakStr +=
          QString(" (since %1)")
              .arg(streak.currentStreakStartDate.toString("dd MMM yy"));
    }

    updateMeanScrobbleCalculation();

  } else {

    m_firstScrobbleLabelValue->setText(firstDateStr);
    m_lastScrobbleLabelValue->setText(lastDateStr);
    m_longestStreakLabelValue->setText(longestStreakStr);
    m_currentStreakLabelValue->setText(currentStreakStr);
    if (m_meanScrobblesResultLabel)
      m_meanScrobblesResultLabel->setText(meanStr);
    if (m_lastPlayedResultLabel)
      m_lastPlayedResultLabel->setText("");
  }

  m_firstScrobbleLabelValue->setText(firstDateStr);
  m_lastScrobbleLabelValue->setText(lastDateStr);
  m_longestStreakLabelValue->setText(longestStreakStr);
  m_currentStreakLabelValue->setText(currentStreakStr);

  if (m_lastPlayedResultLabel)
    m_lastPlayedResultLabel->setText(
        results.isEmpty() ? "" : m_lastPlayedResultLabel->text());
}

void MainWindow::updateDatabaseTableView(const AnalysisResults &results) {
  if (!m_dbTableWidget)
    return;
  m_dbTableWidget->setSortingEnabled(false);
  m_dbTableWidget->clearContents();
  m_dbTableWidget->setRowCount(0);
  if (results.isEmpty()) {
    m_dbTableWidget->setSortingEnabled(true);
    return;
  }

  SortedCounts sortedArtists =
      results.value("topArtists").value<SortedCounts>();
  m_dbTableWidget->setRowCount(sortedArtists.count());
  for (int row = 0; row < sortedArtists.count(); ++row) {
    const auto &pair = sortedArtists[row];
    QTableWidgetItem *iRank = new QTableWidgetItem();
    iRank->setData(Qt::DisplayRole, row + 1);
    QTableWidgetItem *iArtist = new QTableWidgetItem(pair.first);
    QTableWidgetItem *iCount = new QTableWidgetItem();
    iCount->setData(Qt::DisplayRole, pair.second);
    m_dbTableWidget->setItem(row, 0, iRank);
    m_dbTableWidget->setItem(row, 1, iArtist);
    m_dbTableWidget->setItem(row, 2, iCount);
  }
  m_dbTableWidget->resizeColumnsToContents();
  m_dbTableWidget->horizontalHeader()->setStretchLastSection(true);
  m_dbTableWidget->setSortingEnabled(true);
}

void MainWindow::updateArtistsView(const AnalysisResults &results) {
  if (!m_artistListWidget)
    return;
  m_artistListWidget->clear();
  if (results.isEmpty()) {
    m_artistListWidget->addItem("(No data loaded)");
    return;
  }

  SortedCounts top = results.value("topArtists").value<SortedCounts>();

  if (top.isEmpty()) {
    m_artistListWidget->addItem("(No artist data available)");
  } else {
    for (const auto &p : top)
      m_artistListWidget->addItem(
          QString("%1 (%2)").arg(p.first).arg(p.second));
  }
}

void MainWindow::updateTracksView(const AnalysisResults &results) {
  if (!m_trackListWidget)
    return;
  m_trackListWidget->clear();
  if (results.isEmpty()) {
    m_trackListWidget->addItem("(No data loaded)");
    return;
  }

  SortedCounts top = results.value("topTracks").value<SortedCounts>();

  if (top.isEmpty()) {
    m_trackListWidget->addItem("(No track data available)");
  } else {
    for (const auto &p : top)
      m_trackListWidget->addItem(QString("%1 (%2)").arg(p.first).arg(p.second));
  }
}

void MainWindow::updateChartsView(const AnalysisResults &results) {
  qDebug() << "Updating all charts with results...";
  updateArtistChart(results);
  updateTrackChart(results);
  updateHourlyChart(results);
  updateWeeklyChart(results);
}

void MainWindow::updateArtistChart(const AnalysisResults &results) {
  if (!m_artistsChartView || !m_artistsChartView->chart()) {
    return;
  }
  QChart *chart = m_artistsChartView->chart();
  chart->removeAllSeries();
  QList<QAbstractAxis *> axes;
  foreach (QAbstractAxis *a, chart->axes())
    axes.append(a);
  foreach (QAbstractAxis *a, axes)
    chart->removeAxis(a);
  qDeleteAll(axes);
  axes.clear();
  chart->setTitle("Top 10 Artists");

  if (results.isEmpty()) {
    chart->setTitle("Top 10 Artists (No Data)");

    return;
  }

  SortedCounts topDataFull = results.value("topArtists").value<SortedCounts>();
  SortedCounts topData = topDataFull.mid(0, 10);

  if (topData.isEmpty()) {
    chart->setTitle("Top 10 Artists (No Data)");
    return;
  }

  QBarSeries *series = new QBarSeries(chart);
  QBarSet *set = new QBarSet("Plays");
  QStringList cats;
  int maxV = 0;
  for (int i = topData.size() - 1; i >= 0; --i) {
    const auto &p = topData[i];
    *set << p.second;
    cats << p.first;
    if (p.second > maxV)
      maxV = p.second;
  }
  series->append(set);
  chart->addSeries(series);
  chart->setAnimationOptions(QChart::SeriesAnimations);
  QBarCategoryAxis *axX = new QBarCategoryAxis(chart);
  axX->append(cats);
  chart->addAxis(axX, Qt::AlignBottom);
  series->attachAxis(axX);
  QValueAxis *axY = new QValueAxis(chart);
  axY->setRange(0, maxV > 0 ? maxV * 1.1 : 10);
  axY->setLabelFormat("%d");
  axY->setTitleText("Play Count");
  chart->addAxis(axY, Qt::AlignLeft);
  series->attachAxis(axY);
  chart->legend()->setVisible(false);
  m_artistsChartView->setRenderHint(QPainter::Antialiasing);
}

void MainWindow::updateTrackChart(const AnalysisResults &results) {
  if (!m_tracksChartView || !m_tracksChartView->chart()) {
    return;
  }
  QChart *chart = m_tracksChartView->chart();
  chart->removeAllSeries();
  QList<QAbstractAxis *> axes;
  foreach (QAbstractAxis *a, chart->axes())
    axes.append(a);
  foreach (QAbstractAxis *a, axes)
    chart->removeAxis(a);
  qDeleteAll(axes);
  axes.clear();
  chart->setTitle("Top 10 Tracks");

  if (results.isEmpty()) {
    chart->setTitle("Top 10 Tracks (No Data)");
    return;
  }

  SortedCounts topDataFull = results.value("topTracks").value<SortedCounts>();
  SortedCounts topData = topDataFull.mid(0, 10);

  if (topData.isEmpty()) {
    chart->setTitle("Top 10 Tracks (No Data)");
    return;
  }

  QBarSeries *series = new QBarSeries(chart);
  QBarSet *set = new QBarSet("Plays");
  QStringList cats;
  int maxV = 0;
  for (int i = topData.size() - 1; i >= 0; --i) {
    const auto &p = topData[i];
    *set << p.second;
    QString label = p.first;
    if (label.length() > 35)
      label = label.left(32) + "...";
    cats << label;
    if (p.second > maxV)
      maxV = p.second;
  }
  series->append(set);
  chart->addSeries(series);
  chart->setAnimationOptions(QChart::SeriesAnimations);
  QBarCategoryAxis *axX = new QBarCategoryAxis(chart);
  axX->append(cats);
  chart->addAxis(axX, Qt::AlignBottom);
  series->attachAxis(axX);
  QValueAxis *axY = new QValueAxis(chart);
  axY->setRange(0, maxV > 0 ? maxV * 1.1 : 10);
  axY->setLabelFormat("%d");
  axY->setTitleText("Play Count");
  chart->addAxis(axY, Qt::AlignLeft);
  series->attachAxis(axY);
  chart->legend()->setVisible(false);
  m_tracksChartView->setRenderHint(QPainter::Antialiasing);
}

void MainWindow::updateHourlyChart(const AnalysisResults &results) {
  if (!m_hourlyChartView || !m_hourlyChartView->chart()) {
    return;
  }
  QChart *chart = m_hourlyChartView->chart();
  chart->removeAllSeries();
  QList<QAbstractAxis *> axes;
  foreach (QAbstractAxis *a, chart->axes())
    axes.append(a);
  foreach (QAbstractAxis *a, axes)
    chart->removeAxis(a);
  qDeleteAll(axes);
  axes.clear();
  chart->setTitle("Scrobbles per Hour of Day (Local Time)");

  if (results.isEmpty()) {
    chart->setTitle("Scrobbles per Hour (No Data)");
    return;
  }

  QVector<int> hourlyData = results.value("hourlyData").value<QVector<int>>();

  if (hourlyData.size() != 24) {
    chart->setTitle("Scrobbles per Hour (Error)");
    return;
  }

  QBarSeries *series = new QBarSeries(chart);
  QBarSet *set = new QBarSet("Scrobbles");
  QStringList cats;
  int maxV = 0;
  for (int hour = 0; hour < 24; ++hour) {
    *set << hourlyData[hour];
    cats << QStringLiteral("%1").arg(hour, 2, 10, QLatin1Char('0'));
    if (hourlyData[hour] > maxV)
      maxV = hourlyData[hour];
  }
  series->append(set);
  chart->addSeries(series);
  chart->setAnimationOptions(QChart::SeriesAnimations);
  QBarCategoryAxis *axX = new QBarCategoryAxis(chart);
  axX->append(cats);
  axX->setTitleText("Hour of Day (Local)");
  chart->addAxis(axX, Qt::AlignBottom);
  series->attachAxis(axX);
  QValueAxis *axY = new QValueAxis(chart);
  axY->setRange(0, maxV > 0 ? maxV * 1.1 : 10);
  axY->setLabelFormat("%d");
  axY->setTitleText("Total Scrobbles");
  chart->addAxis(axY, Qt::AlignLeft);
  series->attachAxis(axY);
  chart->legend()->setVisible(false);
  m_hourlyChartView->setRenderHint(QPainter::Antialiasing);
}

void MainWindow::updateWeeklyChart(const AnalysisResults &results) {
  if (!m_weeklyChartView || !m_weeklyChartView->chart()) {
    return;
  }
  QChart *chart = m_weeklyChartView->chart();
  chart->removeAllSeries();
  QList<QAbstractAxis *> axes;
  foreach (QAbstractAxis *a, chart->axes())
    axes.append(a);
  foreach (QAbstractAxis *a, axes)
    chart->removeAxis(a);
  qDeleteAll(axes);
  axes.clear();

  chart->setTitle("Scrobbles per Day of Week (Local Time)");

  if (results.isEmpty()) {
    chart->setTitle("Scrobbles per Day (No Data)");
    return;
  }

  QVector<int> weeklyData = results.value("weeklyData").value<QVector<int>>();

  if (weeklyData.size() != 7) {
    chart->setTitle("Scrobbles per Day (Error)");
    return;
  }

  QBarSeries *series = new QBarSeries(chart);
  QBarSet *set = new QBarSet("Scrobbles");
  QStringList cats = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  int maxV = 0;
  for (int day = 0; day < 7; ++day) {
    *set << weeklyData[day];
    if (weeklyData[day] > maxV)
      maxV = weeklyData[day];
  }
  series->append(set);
  chart->addSeries(series);
  chart->setAnimationOptions(QChart::SeriesAnimations);
  QBarCategoryAxis *axX = new QBarCategoryAxis(chart);
  axX->append(cats);
  axX->setTitleText("Day of Week");
  chart->addAxis(axX, Qt::AlignBottom);
  series->attachAxis(axX);
  QValueAxis *axY = new QValueAxis(chart);
  axY->setRange(0, maxV > 0 ? maxV * 1.1 : 10);
  axY->setLabelFormat("%d");
  axY->setTitleText("Total Scrobbles");
  chart->addAxis(axY, Qt::AlignLeft);
  series->attachAxis(axY);
  chart->legend()->setVisible(false);
  m_weeklyChartView->setRenderHint(QPainter::Antialiasing);
}

void MainWindow::updateAboutView() {
  if (m_currentUserLabel) {
    QString u = m_settingsManager.username();
    m_currentUserLabel->setText(u.isEmpty() ? "<Not Set>" : u);
  } else {
    qWarning() << "currentUserLabel null!";
  }
}

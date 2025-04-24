/**
 * @file main.cpp
 * @brief Main entry point for the Last.fm Scrobble Analyzer application.
 */

#include "mainwindow.h"

#include <QApplication>

/**
 * @brief Application entry point.
 * @details Initializes the QApplication, creates the main MainWindow instance,
 * shows the window, and starts the application event loop.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit code from QApplication::exec().
 */
int main(int argc, char *argv[]) {
  QApplication a(argc, argv);
  MainWindow w;
  w.show();
  return a.exec();
}

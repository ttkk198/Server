#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <thread>
#include <vector>
#include "book.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_btnAddBook_clicked();
    void on_btnRemoveBook_clicked();
    void on_btnShowCatalog_clicked();
    void on_btnClearEdit_clicked();

private:
    Ui::MainWindow *ui;
    SOCKET listenSocket;
    fd_set masterSet;
    bool serverRunning;
    std::thread serverThread;
    std::vector<Book> library;

    void runServer();
    void stopServer();
    void logMessage(const QString &msg);
    void updateServerInfo(const QString &info);
    QString getLocalIPAddress();
    void handleClientRequest(SOCKET clientSocket, const std::string& request);
    void sendFile(SOCKET clientSocket, const std::string& filename);
    void receiveFile(SOCKET clientSocket, const std::string& filename);
    void saveKatalog();
    void loadKatalog();
};

#endif // MAINWINDOW_H

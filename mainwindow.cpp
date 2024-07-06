#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QInputDialog>
#include <QMessageBox>
#include <fstream>
#include <QFileDialog>
#include <codecvt>
#include <locale>
#include <iconv.h>


// std::string convertWindows1251ToUtf8(const std::string& input) {
//     iconv_t cd = iconv_open("UTF-8", "WINDOWS-1251");
//     if (cd == (iconv_t)-1) {
//         return "";
//     }

//     size_t inBytesLeft = input.size();
//     size_t outBytesLeft = inBytesLeft * 4;
//     std::string output(outBytesLeft, '\0');

//     char* inBuf = const_cast<char*>(input.data());
//     char* outBuf = &output[0];
//     if (iconv(cd, &inBuf, &inBytesLeft, &outBuf, &outBytesLeft) == (size_t)-1) {
//         iconv_close(cd);
//         return "";
//     }

//     output.resize(output.size() - outBytesLeft);
//     iconv_close(cd);
//     return output;
// }

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , serverRunning(false)
{
    ui->setupUi(this);
    serverRunning = true;
    loadKatalog();
    serverThread = std::thread(&MainWindow::runServer, this);
}

MainWindow::~MainWindow()
{
    if (serverRunning) {
        stopServer();
    }
    if (serverThread.joinable()) {
        serverThread.join();
    }
    delete ui;
}

void MainWindow::runServer()
{
    WSADATA ws;
    if (WSAStartup(MAKEWORD(2, 2), &ws) != 0) {
        logMessage("WSAStartup failed.");
        return;
    }

    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        logMessage("Socket creation failed.");
        WSACleanup();
        return;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(54000);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        logMessage("Bind failed.");
        closesocket(listenSocket);
        WSACleanup();
        return;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        logMessage("Listen failed.");
        closesocket(listenSocket);
        WSACleanup();
        return;
    }

    QString ipAddress = getLocalIPAddress();
    updateServerInfo(QString("%1:%2").arg(ipAddress).arg(54000));

    FD_ZERO(&masterSet);
    FD_SET(listenSocket, &masterSet);
    SOCKET maxSocket = listenSocket;

    while (serverRunning) {
        fd_set copySet = masterSet;
        int socketCount = select(0, &copySet, nullptr, nullptr, nullptr);

        for (int i = 0; i < socketCount; ++i) {
            SOCKET sock = copySet.fd_array[i];

            if (sock == listenSocket) {
                SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
                if (clientSocket != INVALID_SOCKET) {
                    FD_SET(clientSocket, &masterSet);
                    if (clientSocket > maxSocket) {
                        maxSocket = clientSocket;
                    }
                    logMessage("New connection accepted.");
                }
            } else {
                const int bufSize = 4096;
                char buf[bufSize];
                ZeroMemory(buf, bufSize);

                int bytesReceived = recv(sock, buf, bufSize, 0);
                if (bytesReceived <= 0) {
                    closesocket(sock);
                    FD_CLR(sock, &masterSet);
                    logMessage("Connection closed.");
                } else {
                    std::string request(buf, bytesReceived);
                    handleClientRequest(sock, request);
                }
            }
        }
    }

    closesocket(listenSocket);
    WSACleanup();
}

std::string convertToUTF8(const std::string& str) {
    int size_needed = MultiByteToWideChar(CP_ACP, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_ACP, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    int utf8Size = WideCharToMultiByte(CP_UTF8, 0, &wstrTo[0], size_needed, NULL, 0, NULL, NULL);
    std::string strTo(utf8Size, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstrTo[0], size_needed, &strTo[0], utf8Size, NULL, NULL);
    return strTo;
}

std::string toLowerCase(const std::string& str) {
    std::string lowerStr = str;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return lowerStr;
}

void MainWindow::stopServer()
{
    serverRunning = false;
    closesocket(listenSocket);
    FD_ZERO(&masterSet);
    WSACleanup();
}

void MainWindow::logMessage(const QString &msg)
{
    ui->textEdit->append(msg);
}

void MainWindow::updateServerInfo(const QString &info)
{
    ui->labelServerInfo->setText(info);
}

QString MainWindow::getLocalIPAddress()
{
    char ac[80];
    if (gethostname(ac, sizeof(ac)) == SOCKET_ERROR) {
        return "Unknown";
    }

    struct hostent *phe = gethostbyname(ac);
    if (phe == 0) {
        return "Unknown";
    }

    struct in_addr addr;
    memcpy(&addr, phe->h_addr_list[0], sizeof(struct in_addr));
    return QString::fromStdString(inet_ntoa(addr));
}

void MainWindow::handleClientRequest(SOCKET clientSocket, const std::string& request) {
    std::string bookName = convertToUTF8(request);
    std::string filePath;
    bool BookAppear = false;
    logMessage("[Client]: " + QString::fromStdString(bookName));
    for (const auto& book : library) {
        if (toLowerCase(book.name) == toLowerCase(bookName)) {
            filePath = book.path;
            BookAppear = true;
            break;
        }
    }

    if (BookAppear) {
        sendFile(clientSocket, filePath);
    } else {
        const char* notFoundMessage = "Book not found";
        send(clientSocket, notFoundMessage, strlen(notFoundMessage), 0);
    }
}

void MainWindow::sendFile(SOCKET clientSocket, const std::string& filename) {
    std::ifstream file(filename);
    if (file) {
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> fileBuffer(fileSize);
        file.read(fileBuffer.data(), fileSize);
        send(clientSocket, fileBuffer.data(), fileSize, 0);
    } else {
        const char* notFoundMessage = "File not found";
        send(clientSocket, notFoundMessage, strlen(notFoundMessage), 0);
    }
}

// void MainWindow::sendFile(SOCKET clientSocket, const std::string& filename) {
//     std::ifstream file(filename);
//     if (file) {
//         std::stringstream ss;
//         ss << file.rdbuf();
//         std::string content = ss.str();

//         // Convert Windows-1251 to UTF-8
//         std::string utf8content = convertWindows1251ToUtf8(content);

//         // Send data
//         send(clientSocket, utf8content.data(), utf8content.size(), 0);
//     } else {
//         const char* notFoundMessage = "File not found";
//         send(clientSocket, notFoundMessage, strlen(notFoundMessage), 0);
//     }
// }
void MainWindow::on_btnAddBook_clicked()
{
    bool ok;
    QString name = QInputDialog::getText(this, tr("Add Book"), tr("Name:"), QLineEdit::Normal, "", &ok);
    if (ok && !name.isEmpty()) {

        QString author = QInputDialog::getText(this, tr("Add Book"), tr("Author:"), QLineEdit::Normal, "", &ok);
        if (!ok || author.isEmpty())
            return;

        QString path = QFileDialog::getOpenFileName(this, tr("Select Book File"), "", tr("All Files (*.*)"));
        if (path.isEmpty())
            return;

        QString publisher = QInputDialog::getText(this, tr("Add Book"), tr("Publisher:"), QLineEdit::Normal, "", &ok);
        if (!ok || publisher.isEmpty())
            return;

        int pages = QInputDialog::getInt(this, tr("Add Book"), tr("Pages:"), 0, 0, 10000, 1, &ok);
        if (!ok)
            return;

        int year = QInputDialog::getInt(this, tr("Add Book"), tr("Year:"), 2000, 0, 9999, 1, &ok);
        if (!ok)
            return;

        Book book;
        book.name = name.toStdString();
        book.auth = author.toStdString();
        book.path = path.toStdString();
        book.izd = publisher.toStdString();
        book.pages = pages;
        book.year = year;

        library.push_back(book);
        logMessage("Book added: " + name);
        saveKatalog();
    }
}


void MainWindow::on_btnRemoveBook_clicked()
{

    QStringList bookNames;
    for (const Book &book : library) {
        bookNames.append(QString::fromStdString(book.name));
    }
    if (bookNames.isEmpty()) {
        QMessageBox::warning(this, tr("Remove Book"), tr("No books available to remove."));
        return;
    }
    bool ok;
    QString name = QInputDialog::getItem(this, tr("Remove Book"), tr("Select the book to remove:"), bookNames, 0, false, &ok);

    if (ok && !name.isEmpty()) {
        auto it = std::remove_if(library.begin(), library.end(), [&name](const Book &book) { return book.name == name.toStdString(); });

        if (it != library.end()) {
            library.erase(it, library.end());
            logMessage("Book removed: " + name);
            saveKatalog();
        } else {
            QMessageBox::warning(this, tr("Remove Book"), tr("Book not found: ") + name);
        }
    }
}

void MainWindow::on_btnShowCatalog_clicked()
{
    QString catalog;
    for (const Book &book : library) {
        catalog += QString::fromStdString(book.name) + "\n";
    }
    logMessage(catalog);
}


void MainWindow::on_btnClearEdit_clicked()
{
  ui->textEdit->clear();
}


void MainWindow::saveKatalog()
{
    const std::string filename = "Katalog.bin";
    std::ofstream outFile(filename, std::ios::binary);
    if (!outFile.is_open())
    {
        return;
    }

    for (const Book &book : library) {
        size_t nameLen = book.name.size();
        size_t authLen = book.auth.size();
        size_t pathLen = book.path.size();
        size_t izdLen = book.izd.size();

        outFile.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        outFile.write(book.name.c_str(), nameLen);

        outFile.write(reinterpret_cast<const char*>(&authLen), sizeof(authLen));
        outFile.write(book.auth.c_str(), authLen);

        outFile.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
        outFile.write(book.path.c_str(), pathLen);

        outFile.write(reinterpret_cast<const char*>(&izdLen), sizeof(izdLen));
        outFile.write(book.izd.c_str(), izdLen);

        outFile.write(reinterpret_cast<const char*>(&book.pages), sizeof(book.pages));
        outFile.write(reinterpret_cast<const char*>(&book.year), sizeof(book.year));
    }
}

void MainWindow::loadKatalog()
{
    const std::string filename = "Katalog.bin";
    std::ifstream inFile(filename, std::ios::binary);
    if (!inFile.is_open())
    {
        return;
    }

    while (!inFile.eof())
    {
        Book book;
        size_t nameLen;
        size_t authLen;
        size_t pathLen;
        size_t izdLen;

        inFile.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
        if (inFile.eof()) break;

        book.name.resize(nameLen);
        inFile.read(&book.name[0], nameLen);

        inFile.read(reinterpret_cast<char*>(&authLen), sizeof(authLen));
        book.auth.resize(authLen);
        inFile.read(&book.auth[0], authLen);

        inFile.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
        book.path.resize(pathLen);
        inFile.read(&book.path[0], pathLen);

        inFile.read(reinterpret_cast<char*>(&izdLen), sizeof(izdLen));
        book.izd.resize(izdLen);
        inFile.read(&book.izd[0], izdLen);

        inFile.read(reinterpret_cast<char*>(&book.pages), sizeof(book.pages));
        inFile.read(reinterpret_cast<char*>(&book.year), sizeof(book.year));

        if (!book.name.empty()) {
            library.push_back(book);
        }
    }
}

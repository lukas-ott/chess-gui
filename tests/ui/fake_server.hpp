#pragma once

// A tiny HTTP server for UI tests: Lichess, chess.com, FIDE and the tablebase without the
// network (tests never use it).

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <map>
#include <memory>

namespace kiebitz::test {

/// Answers GET requests from a table of paths; remembers what was asked.
class FakeServer : public QObject {
public:
    FakeServer() {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* socket = m_server.nextPendingConnection();
            const auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                // A whole request: the headers, then as much body as Content-Length says.
                buffer->append(socket->readAll());
                const qsizetype end = buffer->indexOf("\r\n\r\n");
                if (end < 0) {
                    return;
                }
                qsizetype length = 0;
                for (const QByteArray& line : buffer->left(end).split('\n')) {
                    if (line.toLower().startsWith("content-length:")) {
                        length = line.mid(15).trimmed().toLongLong();
                    }
                }
                if (buffer->size() < end + 4 + length) {
                    return;
                }
                const QByteArray& request = *buffer;
                const QByteArray target = request.split(' ').value(1);
                asked << QString::fromUtf8(target);
                requests << request;
                const QByteArray path = target.split('?').value(0);
                const auto found = pages.find(path);
                const QByteArray body = found != pages.end() ? found->second : "Not found";
                QByteArray statusLine =
                    found != pages.end() ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Not Found\r\n";
                if (const auto status = statuses.find(path); status != statuses.end()) {
                    statusLine = "HTTP/1.1 " + QByteArray::number(status->second) + " X\r\n";
                }
                socket->write(statusLine + QByteArray("Content-Length: ") +
                              QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" +
                              body);
                socket->disconnectFromHost();
            });
        });
    }
    [[nodiscard]] QUrl url() const {
        return {QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort())};
    }
    std::map<QByteArray, QByteArray> pages;
    std::map<QByteArray, int> statuses; ///< paths answered with this status instead
    QStringList asked;
    QByteArrayList requests; ///< whole: method, headers, body

private:
    QTcpServer m_server;
};

} // namespace kiebitz::test

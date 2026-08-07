#pragma once

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace padmirror::network {

class LanReceiver final : public QObject {
    Q_OBJECT

public:
    explicit LanReceiver(QObject* parent = nullptr);
    ~LanReceiver() override;

    bool start(const QString& configuredPath, std::uint16_t videoPort, std::uint16_t audioPort);
    void stop();
    [[nodiscard]] bool running() const;

    static QString findUxPlay(const QString& configuredPath = {});
    static QStringList localIpv4Addresses();

signals:
    void started();
    void stopped();
    void errorOccurred(const QString& message);
    void logLine(const QString& line);

private:
    void startPreparedReceiver();

    QProcess process_;
    QProcess firewallProcess_;
    QString pendingProgram_;
    QStringList pendingArguments_;
    QProcessEnvironment pendingEnvironment_;
    bool pendingStart_ = false;
    bool stopping_ = false;
};

} // namespace padmirror::network

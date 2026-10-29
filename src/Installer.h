#pragma once
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <functional>

struct VersionInfo {
    QString id, type, url;
};

struct Job {
    QString url, path, sha1;
    qint64 size = 0;
    int tries = 0;
};

// Скачивание версий Minecraft и сборка аргументов запуска.
class Installer : public QObject {
    Q_OBJECT
public:
    explicit Installer(const QString &gameDir, QObject *parent = nullptr);

    QString gameDir() const { return m_dir; }
    void fetchManifest();
    void install(const VersionInfo &v);
    bool isInstalled(const QString &versionId) const;
    int javaMajor(const QString &versionId) const;
    // JVM + игровые аргументы (без пути к java). При ошибке возвращает пустой список.
    QStringList buildArgs(const QString &versionId, const QString &nick, int ramMb, QString *error) const;

signals:
    void manifestReady(const QList<VersionInfo> &versions);
    void progress(int done, int total);
    void installed(const QString &versionId);
    void failed(const QString &message);

private:
    void getBytes(const QUrl &url, std::function<void(const QByteArray &)> ok);
    void pump();
    void finishJob();
    QJsonObject readVersionJson(const QString &id) const;

    QString m_dir;
    QNetworkAccessManager m_net;
    QQueue<Job> m_queue;
    QString m_currentId;
    int m_active = 0, m_total = 0, m_done = 0;
    bool m_failed = false;
};

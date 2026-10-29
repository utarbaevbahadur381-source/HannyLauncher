#include "Installer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QUuid>

namespace {
constexpr int kParallel = 16;
constexpr int kMaxTries = 3;

QString osName() {
#if defined(Q_OS_WIN)
    return "windows";
#elif defined(Q_OS_MACOS)
    return "osx";
#else
    return "linux";
#endif
}

QString archName() {
    const QString a = QSysInfo::buildCpuArchitecture();
    if (a == "i386") return "x86";
    return a; // x86_64, arm64
}

// Правила Mojang: allow/disallow с условиями по ОС и архитектуре.
bool rulesAllow(const QJsonArray &rules) {
    if (rules.isEmpty()) return true;
    bool allowed = false;
    for (const QJsonValue &r : rules) {
        const QJsonObject o = r.toObject();
        bool match = !o.contains("features"); // фичи (demo, разрешение) не поддерживаем
        if (o.contains("os")) {
            const QJsonObject os = o["os"].toObject();
            if (os.contains("name") && os["name"].toString() != osName()) match = false;
            if (os.contains("arch") && os["arch"].toString() != archName()) match = false;
        }
        if (match) allowed = (o["action"].toString() == "allow");
    }
    return allowed;
}

void collectArgs(const QJsonArray &arr, QStringList &out) {
    for (const QJsonValue &v : arr) {
        if (v.isString()) {
            out << v.toString();
            continue;
        }
        const QJsonObject o = v.toObject();
        if (!rulesAllow(o["rules"].toArray())) continue;
        const QJsonValue val = o["value"];
        if (val.isString()) out << val.toString();
        else for (const QJsonValue &x : val.toArray()) out << x.toString();
    }
}
} // namespace

Installer::Installer(const QString &gameDir, QObject *parent) : QObject(parent), m_dir(gameDir) {
    QDir().mkpath(m_dir);
}

void Installer::getBytes(const QUrl &url, std::function<void(const QByteArray &)> ok) {
    QNetworkReply *r = m_net.get(QNetworkRequest(url));
    connect(r, &QNetworkReply::finished, this, [this, r, url, ok]() {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            emit failed(tr("Ошибка сети: %1\n%2").arg(url.toString(), r->errorString()));
            return;
        }
        ok(r->readAll());
    });
}

void Installer::fetchManifest() {
    getBytes(QUrl("https://launchermeta.mojang.com/mc/game/version_manifest_v2.json"),
             [this](const QByteArray &data) {
                 QList<VersionInfo> list;
                 for (const QJsonValue &v : QJsonDocument::fromJson(data).object()["versions"].toArray()) {
                     const QJsonObject o = v.toObject();
                     list.push_back({o["id"].toString(), o["type"].toString(), o["url"].toString()});
                 }
                 emit manifestReady(list);
             });
}

QJsonObject Installer::readVersionJson(const QString &id) const {
    QFile f(m_dir + "/versions/" + id + "/" + id + ".json");
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool Installer::isInstalled(const QString &id) const {
    return QFile::exists(m_dir + "/versions/" + id + "/.hanny_ok");
}

int Installer::javaMajor(const QString &id) const {
    return readVersionJson(id)["javaVersion"].toObject()["majorVersion"].toInt(8);
}

void Installer::install(const VersionInfo &v) {
    m_failed = false;
    m_currentId = v.id;
    getBytes(QUrl(v.url), [this, v](const QByteArray &vjson) {
        const QString vdir = m_dir + "/versions/" + v.id;
        QDir().mkpath(vdir);
        QFile f(vdir + "/" + v.id + ".json");
        if (f.open(QIODevice::WriteOnly)) f.write(vjson);
        f.close();

        const QJsonObject root = QJsonDocument::fromJson(vjson).object();
        QList<Job> jobs;

        const QJsonObject client = root["downloads"].toObject()["client"].toObject();
        jobs.push_back({client["url"].toString(), vdir + "/" + v.id + ".jar", client["sha1"].toString(),
                        qint64(client["size"].toDouble()), 0});

        for (const QJsonValue &lv : root["libraries"].toArray()) {
            const QJsonObject lib = lv.toObject();
            if (!rulesAllow(lib["rules"].toArray())) continue;
            const QJsonObject art = lib["downloads"].toObject()["artifact"].toObject();
            if (art["url"].toString().isEmpty()) continue;
            jobs.push_back({art["url"].toString(), m_dir + "/libraries/" + art["path"].toString(),
                            art["sha1"].toString(), qint64(art["size"].toDouble()), 0});
        }

        const QJsonObject ai = root["assetIndex"].toObject();
        const QString indexPath = m_dir + "/assets/indexes/" + ai["id"].toString() + ".json";
        getBytes(QUrl(ai["url"].toString()), [this, v, jobs, indexPath](const QByteArray &idx) mutable {
            QDir().mkpath(QFileInfo(indexPath).absolutePath());
            QFile fi(indexPath);
            if (fi.open(QIODevice::WriteOnly)) fi.write(idx);
            fi.close();

            const QJsonObject objs = QJsonDocument::fromJson(idx).object()["objects"].toObject();
            for (auto it = objs.begin(); it != objs.end(); ++it) {
                const QJsonObject o = it.value().toObject();
                const QString hash = o["hash"].toString();
                const QString sub = hash.left(2);
                jobs.push_back({"https://resources.download.minecraft.net/" + sub + "/" + hash,
                                m_dir + "/assets/objects/" + sub + "/" + hash, hash,
                                qint64(o["size"].toDouble()), 0});
            }
            m_queue = QQueue<Job>();
            for (const Job &j : jobs) m_queue.enqueue(j);
            m_total = m_queue.size();
            m_done = 0;
            m_active = 0;
            pump();
        });
    });
}

void Installer::pump() {
    while (!m_failed && m_active < kParallel && !m_queue.isEmpty()) {
        Job job = m_queue.dequeue();

        QFileInfo fi(job.path);
        if (fi.exists() && (job.size <= 0 || fi.size() == job.size)) { // уже скачано
            ++m_done;
            continue;
        }

        ++m_active;
        QNetworkReply *r = m_net.get(QNetworkRequest(QUrl(job.url)));
        connect(r, &QNetworkReply::finished, this, [this, r, job]() mutable {
            r->deleteLater();
            --m_active;
            const QByteArray data = r->readAll();
            const bool good = r->error() == QNetworkReply::NoError &&
                              (job.sha1.isEmpty() ||
                               QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex() == job.sha1.toLatin1());
            if (good) {
                QDir().mkpath(QFileInfo(job.path).absolutePath());
                QFile f(job.path);
                if (f.open(QIODevice::WriteOnly)) f.write(data);
                ++m_done;
            } else if (++job.tries < kMaxTries) {
                m_queue.enqueue(job);
            } else if (!m_failed) {
                m_failed = true;
                emit failed(tr("Не удалось скачать файл:\n%1").arg(job.url));
                return;
            }
            finishJob();
        });
    }
    emit progress(m_done, m_total);
    if (!m_failed && m_queue.isEmpty() && m_active == 0) finishJob();
}

void Installer::finishJob() {
    if (m_failed) return;
    if (m_queue.isEmpty() && m_active == 0) {
        emit progress(m_total, m_total);
        QFile ok(m_dir + "/versions/" + m_currentId + "/.hanny_ok");
        if (ok.open(QIODevice::WriteOnly)) ok.write("1");
        emit installed(m_currentId);
        return;
    }
    pump();
}

QStringList Installer::buildArgs(const QString &id, const QString &nick, int ramMb, QString *error) const {
    const QJsonObject root = readVersionJson(id);
    if (root.isEmpty()) {
        if (error) *error = tr("Не найден json версии %1").arg(id);
        return {};
    }

    // classpath
    QStringList cp;
    for (const QJsonValue &lv : root["libraries"].toArray()) {
        const QJsonObject lib = lv.toObject();
        if (!rulesAllow(lib["rules"].toArray())) continue;
        const QString path = lib["downloads"].toObject()["artifact"].toObject()["path"].toString();
        if (!path.isEmpty()) cp << m_dir + "/libraries/" + path;
    }
    cp << m_dir + "/versions/" + id + "/" + id + ".jar";

    const QString natives = m_dir + "/versions/" + id + "/natives";
    QDir().mkpath(natives);

    QStringList jvm, game;
    if (root.contains("arguments")) {
        collectArgs(root["arguments"].toObject()["jvm"].toArray(), jvm);
        collectArgs(root["arguments"].toObject()["game"].toArray(), game);
    } else { // старый формат (<= 1.12)
        jvm << "-Djava.library.path=${natives_directory}" << "-cp" << "${classpath}";
        game = root["minecraftArguments"].toString().split(' ', Qt::SkipEmptyParts);
    }

    const QString uuid = QUuid::fromRfc4122([&] {
                             QByteArray h = QCryptographicHash::hash(("OfflinePlayer:" + nick).toUtf8(),
                                                                     QCryptographicHash::Md5);
                             h[6] = char((h[6] & 0x0f) | 0x30);
                             h[8] = char((h[8] & 0x3f) | 0x80);
                             return h;
                         }()).toString(QUuid::WithoutBraces);

    const QMap<QString, QString> vars = {
        {"auth_player_name", nick},
        {"version_name", id},
        {"game_directory", m_dir},
        {"assets_root", m_dir + "/assets"},
        {"game_assets", m_dir + "/assets"},
        {"assets_index_name", root["assetIndex"].toObject()["id"].toString()},
        {"auth_uuid", uuid},
        {"auth_access_token", "0"},
        {"auth_session", "0"},
        {"clientid", ""},
        {"auth_xuid", ""},
        {"user_type", "legacy"},
        {"user_properties", "{}"},
        {"version_type", root["type"].toString()},
        {"natives_directory", natives},
        {"launcher_name", "HannyLauncher"},
        {"launcher_version", "0.1"},
        {"classpath", cp.join(QDir::listSeparator())},
        {"classpath_separator", QString(QDir::listSeparator())},
        {"library_directory", m_dir + "/libraries"},
    };

    auto subst = [&](QString s) {
        for (auto it = vars.begin(); it != vars.end(); ++it) s.replace("${" + it.key() + "}", it.value());
        return s;
    };

    QStringList args;
    args << QString("-Xmx%1M").arg(ramMb) << "-Xms512M";
    for (const QString &a : jvm) args << subst(a);
    args << root["mainClass"].toString();
    for (const QString &a : game) args << subst(a);
    return args;
}

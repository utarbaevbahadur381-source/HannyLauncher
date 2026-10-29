#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "Installer.h"

static const char *kStyle = R"(
QWidget { background:#0b0f14; color:#e6fffb; font-size:14px; }
QLabel#title { font-size:26px; font-weight:700; color:#2de2c6; }
QLabel#hint { color:#6f8f8a; font-size:12px; }
QLineEdit, QComboBox, QSpinBox { background:#121a22; border:1px solid #1f3a38; border-radius:8px; padding:7px 10px; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border:1px solid #2de2c6; }
QComboBox QAbstractItemView { background:#121a22; selection-background-color:#1b4a45; border:1px solid #1f3a38; }
QCheckBox::indicator { width:16px; height:16px; border:1px solid #2de2c6; border-radius:4px; background:#121a22; }
QCheckBox::indicator:checked { background:#2de2c6; }
QPushButton { background:#121a22; border:1px solid #1f3a38; border-radius:8px; padding:7px 12px; }
QPushButton:hover { border:1px solid #2de2c6; }
QPushButton#play { background:#2de2c6; color:#04201c; font-size:18px; font-weight:800; border:none; padding:14px; border-radius:10px; }
QPushButton#play:hover { background:#5af2d9; }
QPushButton#play:disabled { background:#1b4a45; color:#5d8a84; }
QProgressBar { background:#121a22; border:1px solid #1f3a38; border-radius:8px; text-align:center; height:18px; }
QProgressBar::chunk { background:#2de2c6; border-radius:7px; }
)";

class Window : public QWidget {
    Q_OBJECT
public:
    Window()
        : m_inst(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/game") {
        setWindowTitle("Hanny Launcher");
        setWindowIcon(QIcon(":/icon.png"));
        setFixedWidth(460);

        auto *logo = new QLabel;
        logo->setPixmap(QPixmap(":/icon.png").scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        auto *title = new QLabel("Hanny Launcher");
        title->setObjectName("title");
        auto *head = new QHBoxLayout;
        head->addWidget(logo);
        head->addWidget(title, 1);

        m_nick = new QLineEdit;
        m_nick->setPlaceholderText("Ник (3-16 символов: латиница, цифры, _)");
        m_nick->setValidator(new QRegularExpressionValidator(QRegularExpression("[A-Za-z0-9_]{0,16}"), this));

        m_version = new QComboBox;
        m_snap = new QCheckBox("Снапшоты и старые бета-версии");
        m_ram = new QSpinBox;
        m_ram->setRange(1, 64);
        m_ram->setSuffix(" ГБ ОЗУ");
        m_java = new QLineEdit;
        m_java->setPlaceholderText("Путь к Java (пусто = java из PATH)");
        auto *browse = new QPushButton("...");
        browse->setFixedWidth(44);
        auto *javaRow = new QHBoxLayout;
        javaRow->addWidget(m_java, 1);
        javaRow->addWidget(browse);

        m_hint = new QLabel;
        m_hint->setObjectName("hint");
        m_hint->setWordWrap(true);
        m_bar = new QProgressBar;
        m_bar->setVisible(false);
        m_play = new QPushButton("ИГРАТЬ");
        m_play->setObjectName("play");
        m_play->setEnabled(false);
        auto *folder = new QPushButton("Открыть папку игры");

        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(24, 20, 24, 20);
        lay->setSpacing(10);
        lay->addLayout(head);
        lay->addWidget(m_nick);
        lay->addWidget(m_version);
        lay->addWidget(m_snap);
        lay->addWidget(m_ram);
        lay->addLayout(javaRow);
        lay->addWidget(m_hint);
        lay->addWidget(m_bar);
        lay->addWidget(m_play);
        lay->addWidget(folder);

        QSettings s;
        m_nick->setText(s.value("nick", "Player").toString());
        m_ram->setValue(s.value("ram", 4).toInt());
        m_java->setText(s.value("java").toString());
        m_lastVersion = s.value("version").toString();

        connect(m_snap, &QCheckBox::toggled, this, &Window::fillVersions);
        connect(m_version, &QComboBox::currentIndexChanged, this, &Window::updateHint);
        connect(m_play, &QPushButton::clicked, this, &Window::onPlay);
        connect(folder, &QPushButton::clicked, this,
                [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_inst.gameDir())); });
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString p = QFileDialog::getOpenFileName(this, "Выбери java / javaw");
            if (!p.isEmpty()) m_java->setText(p);
        });

        connect(&m_inst, &Installer::manifestReady, this, [this](const QList<VersionInfo> &v) {
            m_versions = v;
            fillVersions();
            m_play->setEnabled(true);
        });
        connect(&m_inst, &Installer::progress, this, [this](int d, int t) {
            m_bar->setMaximum(t);
            m_bar->setValue(d);
            m_bar->setFormat(QString("Загрузка файлов: %1 / %2").arg(d).arg(t));
        });
        connect(&m_inst, &Installer::installed, this, [this](const QString &id) { launch(id); });
        connect(&m_inst, &Installer::failed, this, [this](const QString &msg) {
            setBusy(false);
            QMessageBox::warning(this, "Hanny Launcher", msg);
        });

        m_hint->setText("Загружаю список версий...");
        m_inst.fetchManifest();
    }

private:
    void fillVersions() {
        const QString keep = m_version->currentText().isEmpty() ? m_lastVersion : m_version->currentText();
        m_version->blockSignals(true);
        m_version->clear();
        for (const VersionInfo &v : m_versions)
            if (m_snap->isChecked() || v.type == "release") m_version->addItem(v.id);
        const int idx = m_version->findText(keep);
        m_version->setCurrentIndex(idx >= 0 ? idx : 0);
        m_version->blockSignals(false);
        updateHint();
    }

    void updateHint() {
        const QString id = m_version->currentText();
        m_hint->setText(m_inst.isInstalled(id)
                            ? QString("Версия %1 установлена. Java: %2+").arg(id).arg(m_inst.javaMajor(id))
                            : QString("Версия %1 будет скачана при первом запуске.").arg(id));
    }

    void setBusy(bool b) {
        m_play->setEnabled(!b);
        m_nick->setEnabled(!b);
        m_version->setEnabled(!b);
        m_bar->setVisible(b);
        if (!b) updateHint();
    }

    void onPlay() {
        if (m_nick->text().size() < 3) {
            QMessageBox::information(this, "Hanny Launcher", "Ник должен быть от 3 до 16 символов.");
            return;
        }
        QSettings s;
        s.setValue("nick", m_nick->text());
        s.setValue("ram", m_ram->value());
        s.setValue("java", m_java->text());
        s.setValue("version", m_version->currentText());

        const QString id = m_version->currentText();
        setBusy(true);
        if (m_inst.isInstalled(id)) {
            launch(id);
            return;
        }
        for (const VersionInfo &v : m_versions)
            if (v.id == id) {
                m_hint->setText("Скачиваю версию " + id + "...");
                m_inst.install(v);
                return;
            }
    }

    void launch(const QString &id) {
        QString err;
        const QStringList args = m_inst.buildArgs(id, m_nick->text(), m_ram->value() * 1024, &err);
        if (args.isEmpty()) {
            setBusy(false);
            QMessageBox::warning(this, "Hanny Launcher", err);
            return;
        }
        const QString java = m_java->text().trimmed().isEmpty() ? "java" : m_java->text().trimmed();
        const QString logPath = m_inst.gameDir() + "/hanny-last.log";

        auto *p = new QProcess(this);
        p->setWorkingDirectory(m_inst.gameDir());
        p->setProcessChannelMode(QProcess::MergedChannels);
        p->setStandardOutputFile(logPath);
        connect(p, &QProcess::errorOccurred, this, [this, p, java](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) {
                setBusy(false);
                QMessageBox::warning(this, "Hanny Launcher",
                                     "Не удалось запустить Java (" + java + ").\nУстанови JDK и/или укажи путь к нему.");
                p->deleteLater();
            }
        });
        connect(p, &QProcess::finished, this, [this, p, logPath](int code, QProcess::ExitStatus) {
            show();
            setBusy(false);
            if (code != 0)
                QMessageBox::warning(this, "Hanny Launcher",
                                     QString("Игра завершилась с кодом %1.\nЛог: %2").arg(code).arg(logPath));
            p->deleteLater();
        });
        connect(p, &QProcess::started, this, [this] {
            m_hint->setText("Игра запущена");
            hide();
        });
        p->start(java, args);
    }

    Installer m_inst;
    QList<VersionInfo> m_versions;
    QString m_lastVersion;
    QLineEdit *m_nick, *m_java;
    QComboBox *m_version;
    QCheckBox *m_snap;
    QSpinBox *m_ram;
    QLabel *m_hint;
    QProgressBar *m_bar;
    QPushButton *m_play;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("Hanny");
    QApplication::setApplicationName("HannyLauncher");
    app.setStyleSheet(kStyle);
    Window w;
    w.show();
    return app.exec();
}

#include "main.moc"

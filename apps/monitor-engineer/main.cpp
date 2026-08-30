// 工程师版上位机入口
#include "MainWindow.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "util/UserManager.h"

namespace {

// 加载编译进资源的统一主题；主题缺失不阻断启动（退回系统默认样式）
void applyTheme() {
    QFile f(QStringLiteral(":/theme/app.qss"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qApp->setStyleSheet(QString::fromUtf8(f.readAll()));
    } else {
        qWarning("主题样式加载失败，使用系统默认样式");
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("VSCR-6EUR3-Monitor"));
    QApplication::setApplicationVersion(QStringLiteral(LX_VERSION_STR));
    QApplication::setOrganizationName(QStringLiteral(LX_ORG_NAME));
    applyTheme();

    // 用户配置目录：%APPDATA%/BURNISH-LingxiPolish
    const QString cfgDir =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(cfgDir);

    lx::UserManager users;
    QString err;
    if (!users.load(cfgDir + QStringLiteral("/accounts.json"), &err)) {
        qWarning("账号库加载失败：%s", qPrintable(err));
    }

    MainWindow w(users);
    w.show();
    return app.exec();
}

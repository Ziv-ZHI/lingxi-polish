// 操作工版入口
#include "OperatorWindow.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
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

    const QString cfgDir =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(cfgDir + QStringLiteral("/recipes"));

    lx::UserManager users;
    QString err;
    if (!users.load(cfgDir + QStringLiteral("/accounts.json"), &err)) {
        qWarning("账号库加载失败：%s", qPrintable(err));
    }

    // 操作工版只需可执行权限，登录失败则退出
    bool loggedIn = false;
    for (int attempt = 0; attempt < 3 && !loggedIn; ++attempt) {
        bool ok = false;
        const QString name = QInputDialog::getText(
            nullptr, QStringLiteral("登录"), QStringLiteral("用户名："),
            QLineEdit::Normal, QString(), &ok);
        if (!ok) return 0;
        const QString pwd = QInputDialog::getText(
            nullptr, QStringLiteral("登录"), QStringLiteral("密码："),
            QLineEdit::Password, QString(), &ok);
        if (!ok) return 0;
        if (!users.login(name, pwd, &err)) {
            QMessageBox::critical(nullptr, QStringLiteral("登录失败"), err);
        } else {
            loggedIn = true;
        }
    }
    if (!loggedIn) return 0;

    OperatorWindow w(users);
    w.show();
    return app.exec();
}

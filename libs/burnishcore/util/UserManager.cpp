#include "UserManager.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace lx {
namespace {

QString hashPassword(const QString& name, const QString& password) {
    const QByteArray raw = (name + QStringLiteral(":") + password).toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex());
}

QString key(Role r) {
    switch (r) {
    case Role::Operator: return QStringLiteral("operator");
    case Role::Engineer: return QStringLiteral("engineer");
    default:             return QStringLiteral("admin");
    }
}

Role parseRole(const QString& s) {
    if (s == QStringLiteral("engineer")) return Role::Engineer;
    if (s == QStringLiteral("admin"))    return Role::Admin;
    return Role::Operator;
}

}  // namespace

QString roleName(Role r) {
    switch (r) {
    case Role::Operator: return QStringLiteral("操作工");
    case Role::Engineer: return QStringLiteral("工程师");
    default:             return QStringLiteral("管理员");
    }
}

bool UserManager::load(const QString& path, QString* err) {
    QFile f(path);
    if (!f.exists()) {
        // 首次运行：内置默认管理员，强制用户自行改密
        users_.clear();
        users_.push_back({QStringLiteral("admin"), hashPassword(QStringLiteral("admin"),
                          QStringLiteral("lingxi@2026")), Role::Admin});
        return save(path, err);
    }
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("无法打开账号文件：%1").arg(path);
        return false;
    }
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    users_.clear();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        users_.push_back({o.value(QStringLiteral("name")).toString(),
                          o.value(QStringLiteral("hash")).toString(),
                          parseRole(o.value(QStringLiteral("role")).toString())});
    }
    return true;
}

bool UserManager::save(const QString& path, QString* err) const {
    QJsonArray arr;
    for (const UserAccount& u : users_) {
        QJsonObject o;
        o[QStringLiteral("name")] = u.name;
        o[QStringLiteral("hash")] = u.passwordHash;
        o[QStringLiteral("role")] = key(u.role);
        arr.append(o);
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = QStringLiteral("无法写入账号文件：%1").arg(path);
        return false;
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}

bool UserManager::login(const QString& name, const QString& password, QString* err) {
    for (const UserAccount& u : users_) {
        if (u.name.compare(name, Qt::CaseInsensitive) != 0) continue;
        if (u.passwordHash != hashPassword(u.name, password)) {
            if (err) *err = QStringLiteral("密码错误");
            return false;
        }
        current_ = u;
        return true;
    }
    if (err) *err = QStringLiteral("用户不存在：%1").arg(name);
    return false;
}

void UserManager::logout() { current_ = UserAccount{}; }

bool UserManager::canEditParams() const {
    return current_.role == Role::Engineer || current_.role == Role::Admin;
}

bool UserManager::addUser(const QString& name, const QString& password, Role role,
                          QString* err) {
    for (const UserAccount& u : users_) {
        if (u.name.compare(name, Qt::CaseInsensitive) == 0) {
            if (err) *err = QStringLiteral("用户已存在：%1").arg(name);
            return false;
        }
    }
    users_.push_back({name, hashPassword(name, password), role});
    return true;
}

bool UserManager::removeUser(const QString& name, QString* err) {
    for (int i = 0; i < users_.size(); ++i) {
        if (users_.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            users_.removeAt(i);
            return true;
        }
    }
    if (err) *err = QStringLiteral("用户不存在：%1").arg(name);
    return false;
}

}  // namespace lx

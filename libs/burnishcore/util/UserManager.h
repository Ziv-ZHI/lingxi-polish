// 权限分级：操作工（只执行）/ 工程师（改参数）/ 管理员（管账号）
#pragma once

#include <QString>
#include <QVector>

namespace lx {

enum class Role { Operator, Engineer, Admin };

struct UserAccount {
    QString name;
    QString passwordHash;   // 存储 SHA-256，不存明文
    Role    role = Role::Operator;
};

class UserManager {
public:
    // 账号库持久化在用户目录 accounts.json
    bool load(const QString& path, QString* err = nullptr);
    bool save(const QString& path, QString* err = nullptr) const;

    bool login(const QString& name, const QString& password, QString* err = nullptr);
    void logout();
    bool hasUser() const { return !users_.isEmpty(); }

    Role currentRole() const { return current_.role; }
    QString currentUser() const { return current_.name; }
    bool loggedIn() const { return !current_.name.isEmpty(); }

    bool canExecuteTask() const { return loggedIn(); }                        // 操作工即可
    bool canEditParams() const;                                               // 工程师及以上
    bool canManageUsers() const { return current_.role == Role::Admin; }      // 仅管理员

    bool addUser(const QString& name, const QString& password, Role role, QString* err = nullptr);
    bool removeUser(const QString& name, QString* err = nullptr);

private:
    QVector<UserAccount> users_;
    UserAccount current_;
};

QString roleName(Role r);

}  // namespace lx

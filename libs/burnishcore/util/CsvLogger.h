// 运行日志：把 2ms 遥测数据落盘为 CSV，供 data-analyzer 做力/轨迹跟踪误差分析
// 采样降频可配（默认每 5 帧存 1 条，即 10ms 分辨率），避免磁盘压力
#pragma once

#include "protocol/FirmwareProtocol.h"

#include <QFile>
#include <QString>
#include <QTextStream>

namespace lx {

class CsvLogger {
public:
    CsvLogger() = default;
    ~CsvLogger() { stop(); }

    CsvLogger(const CsvLogger&) = delete;
    CsvLogger& operator=(const CsvLogger&) = delete;

    bool start(const QString& filePath, int decimation = 5, QString* err = nullptr);
    void stop();
    bool isActive() const { return file_.isOpen(); }
    QString filePath() const { return path_; }

    // 每帧调用即可，内部按 decimation 抽稀落盘
    void append(const protocol::TelemetryFrame& f);

private:
    QFile file_;
    QTextStream out_;
    QString path_;
    int    decimation_ = 5;
    int    counter_ = 0;
};

}  // namespace lx

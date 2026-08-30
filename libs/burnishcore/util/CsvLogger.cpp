#include "CsvLogger.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace lx {

// 表头列序与 tools/data-analyzer 的解析（第 22/23/24 列为 fx/fy/fz）保持一致，
// 修改列必须同步 data-analyzer 的 loadLog()
bool CsvLogger::start(const QString& filePath, int decimation, QString* err) {
    QFileInfo fi(filePath);
    if (!fi.dir().exists() && !QDir().mkpath(fi.absolutePath())) {
        if (err) *err = QStringLiteral("无法创建日志目录：%1").arg(fi.absolutePath());
        return false;
    }
    stop();

    file_.setFileName(filePath);
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (err) *err = QStringLiteral("无法写入日志文件：%1").arg(filePath);
        return false;
    }
    out_.setDevice(&file_);
    out_ << "wallTime,timestampUs,seq,mode,"
         << "j1,j2,j3,j4,j5,j6,"
         << "jv1,jv2,jv3,jv4,jv5,jv6,"
         << "jt1,jt2,jt3,jt4,jt5,jt6,"
         << "fx,fy,fz,mx,my,mz,"
         << "tx,ty,tz,trx,try_,trz,tempC\n";

    path_ = filePath;
    decimation_ = qMax(1, decimation);
    counter_ = 0;
    return true;
}

void CsvLogger::stop() {
    if (file_.isOpen()) {
        out_.flush();
        file_.close();
    }
    path_.clear();
}

void CsvLogger::append(const protocol::TelemetryFrame& f) {
    if (!file_.isOpen()) return;
    if (++counter_ % decimation_ != 0) return;

    out_ << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << ','
         << f.timestampUs << ',' << f.seq << ',' << static_cast<int>(f.mode) << ',';
    for (int i = 0; i < protocol::kJointCount; ++i) out_ << f.jointPos[i] << ',';
    for (int i = 0; i < protocol::kJointCount; ++i) out_ << f.jointVel[i] << ',';
    for (int i = 0; i < protocol::kJointCount; ++i) out_ << f.jointTor[i] << ',';
    for (int i = 0; i < 3; ++i) out_ << f.force[i] << ',';
    for (int i = 0; i < 3; ++i) out_ << f.torque[i] << ',';
    for (int i = 0; i < 6; ++i) out_ << f.tcpPose[i] << ',';
    out_ << f.sensorTempC << '\n';

    // 每 100 条落一次盘：断电/崩溃最多丢 1 秒数据，又不至于频繁刷盘
    if (counter_ % 500 == 0) out_.flush();
}

}  // namespace lx

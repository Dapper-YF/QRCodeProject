#include "QRProcessor.h"
#include <opencv2/opencv.hpp>
#include <quirc.h>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

constexpr int kMaxProcessingDimension = 1000;
constexpr int kBlurCheckMaxDimension = 320;
constexpr double kBlurVarianceThreshold = 100.0;
constexpr double kFrameVarianceTriggerThreshold = 35.0;
constexpr int kAutoSnapshotMinQrBoxSidePx = 96;
constexpr double kAutoSnapshotMinQrAreaRatio = 0.010;

struct QuircLocatorMetrics
{
    int candidateCount = 0;
    double maxAreaRatio = 0.0;
    int maxBoxWidth = 0;
    int maxBoxHeight = 0;
};

class VideoFrameUnmapGuard
{
public:
    explicit VideoFrameUnmapGuard(QVideoFrame &frame)
        : frame(frame)
    {
    }

    ~VideoFrameUnmapGuard()
    {
        if (frame.isMapped()) {
            frame.unmap();
        }
    }

private:
    QVideoFrame &frame;
};

QString normalizeImagePath(const QString &imagePath)
{
    QString localPath = imagePath;
    if (localPath.startsWith("file:///", Qt::CaseInsensitive)) {
        localPath = localPath.mid(8);
    }

    if (localPath.startsWith("/") && localPath.length() > 3 && localPath[2] == ':') {
        localPath = localPath.mid(1);
    }

    return localPath;
}

QString scanStateLabel(QRProcessor::ScanState state)
{
    switch (state) {
    case QRProcessor::Searching:
        return QStringLiteral("搜索中");
    case QRProcessor::Decoding:
        return QStringLiteral("解码中");
    case QRProcessor::Done:
        return QStringLiteral("已完成");
    }

    return QStringLiteral("未知状态");
}

QString buildPipelineInfo(
    const cv::Size &originalSize,
    const cv::Size &preparedSize,
    bool blurFilterEnabled,
    double blurVariance,
    const QString &strategyLabel)
{
    QStringList steps;
    if (originalSize != preparedSize) {
        steps.append(QStringLiteral("缩小(%1x%2 -> %3x%4)")
                         .arg(originalSize.width)
                         .arg(originalSize.height)
                         .arg(preparedSize.width)
                         .arg(preparedSize.height));
    }

    steps.append(QStringLiteral("灰度"));

    if (blurFilterEnabled) {
        steps.append(QStringLiteral("模糊过滤(var=%1)").arg(blurVariance, 0, 'f', 1));
    }

    steps.append(QStringLiteral("CLAHE"));
    steps.append(QStringLiteral("USM"));

    if (!strategyLabel.isEmpty()) {
        steps.append(strategyLabel);
    }

    steps.append(QStringLiteral("quirc"));
    return QStringLiteral("多码路径：") + steps.join(QStringLiteral(" -> "));
}

double computeLaplacianVariance(const cv::Mat &grayImage)
{
    if (grayImage.empty()) {
        return 0.0;
    }

    cv::Mat blurCheckImage = grayImage;
    const int maxDimension = std::max(grayImage.cols, grayImage.rows);
    if (maxDimension > kBlurCheckMaxDimension) {
        const double scale = static_cast<double>(kBlurCheckMaxDimension) / static_cast<double>(maxDimension);
        const cv::Size resizedSize(
            std::max(1, cvRound(grayImage.cols * scale)),
            std::max(1, cvRound(grayImage.rows * scale)));
        cv::resize(grayImage, blurCheckImage, resizedSize, 0.0, 0.0, cv::INTER_AREA);
    }

    cv::Mat laplacianImage;
    cv::Laplacian(blurCheckImage, laplacianImage, CV_64F, 3);

    cv::Scalar meanValue;
    cv::Scalar stddevValue;
    cv::meanStdDev(laplacianImage, meanValue, stddevValue);
    return stddevValue[0] * stddevValue[0];
}

int adaptiveBlockSizeFor(const cv::Size &size)
{
    const int minDimension = std::min(size.width, size.height);
    if (minDimension < 3) {
        return 0;
    }

    int blockSize = std::clamp(minDimension / 8, 11, 41);
    if (blockSize % 2 == 0) {
        ++blockSize;
    }

    if (blockSize >= minDimension) {
        blockSize = (minDimension % 2 == 0) ? (minDimension - 1) : minDimension;
    }

    return blockSize >= 3 ? blockSize : 0;
}

bool normalizeToGray8(const cv::Mat &image, cv::Mat &grayImage, QString &errorMessage)
{
    if (image.empty()) {
        errorMessage = QStringLiteral("错误：输入图像为空");
        return false;
    }

    cv::Mat candidateGray;
    switch (image.channels()) {
    case 1:
        candidateGray = image;
        break;
    case 3:
        cv::cvtColor(image, candidateGray, cv::COLOR_BGR2GRAY);
        break;
    case 4:
        cv::cvtColor(image, candidateGray, cv::COLOR_BGRA2GRAY);
        break;
    default:
        errorMessage = QStringLiteral("错误：输入图像通道数不支持");
        return false;
    }

    if (candidateGray.depth() == CV_8U) {
        grayImage = candidateGray;
        return true;
    }

    if (candidateGray.depth() == CV_16U) {
        candidateGray.convertTo(grayImage, CV_8U, 1.0 / 256.0);
        return true;
    }

    cv::normalize(candidateGray, grayImage, 0, 255, cv::NORM_MINMAX, CV_8U);
    return true;
}

bool preprocessImageForQuirc(
    const cv::Mat &image,
    cv::Mat &processedGray,
    cv::Size &preparedSize,
    double &blurVariance,
    bool enableBlurFilter,
    bool &skippedForBlur,
    QString &errorMessage)
{
    skippedForBlur = false;
    blurVariance = 0.0;

    if (image.empty()) {
        errorMessage = QStringLiteral("错误：输入图像为空");
        return false;
    }

    cv::Mat workingImage = image;
    const int maxDimension = std::max(image.cols, image.rows);
    if (maxDimension > kMaxProcessingDimension) {
        const double scale = static_cast<double>(kMaxProcessingDimension) / static_cast<double>(maxDimension);
        const cv::Size resizedSize(
            std::max(1, cvRound(image.cols * scale)),
            std::max(1, cvRound(image.rows * scale)));
        cv::resize(image, workingImage, resizedSize, 0.0, 0.0, cv::INTER_AREA);
    }

    preparedSize = workingImage.size();

    cv::Mat grayImage;
    if (!normalizeToGray8(workingImage, grayImage, errorMessage)) {
        return false;
    }

    cv::Mat pixelGridRemovedGray;
    cv::GaussianBlur(grayImage, pixelGridRemovedGray, cv::Size(5, 5), 0);

    if (enableBlurFilter) {
        blurVariance = computeLaplacianVariance(pixelGridRemovedGray);
        if (blurVariance < kBlurVarianceThreshold) {
            skippedForBlur = true;
            return false;
        }
    }

    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(3.0, cv::Size(8, 8));
    cv::Mat claheEnhancedImage;
    clahe->apply(pixelGridRemovedGray, claheEnhancedImage);

    cv::Mat blurredImage;
    cv::GaussianBlur(claheEnhancedImage, blurredImage, cv::Size(0, 0), 1.2);
    cv::addWeighted(claheEnhancedImage, 1.5, blurredImage, -0.5, 0.0, processedGray);
    return true;
}

double computeQuadArea(const quirc_code &code)
{
    const double x1 = static_cast<double>(code.corners[0].x);
    const double y1 = static_cast<double>(code.corners[0].y);
    const double x2 = static_cast<double>(code.corners[1].x);
    const double y2 = static_cast<double>(code.corners[1].y);
    const double x3 = static_cast<double>(code.corners[2].x);
    const double y3 = static_cast<double>(code.corners[2].y);
    const double x4 = static_cast<double>(code.corners[3].x);
    const double y4 = static_cast<double>(code.corners[3].y);

    const double twiceArea =
        (x1 * y2) + (x2 * y3) + (x3 * y4) + (x4 * y1)
        - (y1 * x2) - (y2 * x3) - (y3 * x4) - (y4 * x1);
    return std::abs(twiceArea) * 0.5;
}

bool collectQuircLocatorMetrics(const cv::Mat &image, QuircLocatorMetrics &metrics)
{
    metrics = {};

    if (image.empty() || image.type() != CV_8UC1) {
        return false;
    }

    struct quirc *qr = quirc_new();
    if (!qr) {
        return false;
    }

    if (quirc_resize(qr, image.cols, image.rows) < 0) {
        quirc_destroy(qr);
        return false;
    }

    int quircWidth = 0;
    int quircHeight = 0;
    uint8_t *imageData = quirc_begin(qr, &quircWidth, &quircHeight);
    if (!imageData) {
        quirc_destroy(qr);
        return false;
    }

    if (image.isContinuous()) {
        std::memcpy(imageData, image.data, image.total());
    } else {
        for (int row = 0; row < image.rows; ++row) {
            const unsigned char *sourceRow = image.ptr<unsigned char>(row);
            std::memcpy(imageData + (row * image.cols), sourceRow, static_cast<size_t>(image.cols));
        }
    }

    quirc_end(qr);

    const int codeCount = quirc_count(qr);
    metrics.candidateCount = codeCount;

    const double imageArea = static_cast<double>(image.cols) * static_cast<double>(image.rows);
    for (int index = 0; index < codeCount; ++index) {
        struct quirc_code code;
        quirc_extract(qr, index, &code);

        const int minX = std::min(std::min(code.corners[0].x, code.corners[1].x), std::min(code.corners[2].x, code.corners[3].x));
        const int minY = std::min(std::min(code.corners[0].y, code.corners[1].y), std::min(code.corners[2].y, code.corners[3].y));
        const int maxX = std::max(std::max(code.corners[0].x, code.corners[1].x), std::max(code.corners[2].x, code.corners[3].x));
        const int maxY = std::max(std::max(code.corners[0].y, code.corners[1].y), std::max(code.corners[2].y, code.corners[3].y));

        metrics.maxBoxWidth = std::max(metrics.maxBoxWidth, std::max(0, maxX - minX));
        metrics.maxBoxHeight = std::max(metrics.maxBoxHeight, std::max(0, maxY - minY));

        if (imageArea > 0.0) {
            const double ratio = computeQuadArea(code) / imageArea;
            metrics.maxAreaRatio = std::max(metrics.maxAreaRatio, ratio);
        }
    }

    quirc_destroy(qr);
    return true;
}

QString resolveOutputDirectoryPath()
{
    QDir probeDir(QDir::currentPath());
    for (int depth = 0; depth < 6; ++depth) {
        if (QFileInfo::exists(probeDir.filePath(QStringLiteral("CMakeLists.txt")))) {
            return probeDir.filePath(QStringLiteral("output"));
        }

        if (!probeDir.cdUp()) {
            break;
        }
    }

    return QDir::current().filePath(QStringLiteral("output"));
}

bool saveAutoSnapshot(const QImage &snapshotImage, QString &savedPath, QString &errorMessage)
{
    savedPath.clear();

    if (snapshotImage.isNull()) {
        errorMessage = QStringLiteral("错误：抓拍图像为空");
        return false;
    }

    const QString outputDirPath = resolveOutputDirectoryPath();
    QDir outputDir(outputDirPath);
    if (!outputDir.exists() && !outputDir.mkpath(QStringLiteral("."))) {
        errorMessage = QStringLiteral("错误：无法创建抓拍目录\n路径：") + outputDirPath;
        return false;
    }

    static std::atomic<uint64_t> snapshotSequence{0};
    const uint64_t sequence = snapshotSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString fileName = QStringLiteral("qr_snap_%1_%2.jpg").arg(timestamp).arg(sequence);

    savedPath = outputDir.filePath(fileName);
    if (!snapshotImage.save(savedPath, "JPG", 95)) {
        errorMessage = QStringLiteral("错误：自动抓拍保存失败\n路径：") + savedPath;
        return false;
    }

    return true;
}

bool convertQImageToBgrMat(const QImage &sourceImage, cv::Mat &bgrImage)
{
    if (sourceImage.isNull()) {
        return false;
    }

    QImage convertedImage = sourceImage.convertToFormat(QImage::Format_BGR888);
    if (convertedImage.isNull()) {
        return false;
    }

    bgrImage = cv::Mat(
        convertedImage.height(),
        convertedImage.width(),
        CV_8UC3,
        const_cast<uchar *>(convertedImage.constBits()),
        convertedImage.bytesPerLine()).clone();
    return !bgrImage.empty();
}

bool copyPlaneRows(const QVideoFrame &frame, int planeIndex, int rowWidth, int rowCount, uchar *destination)
{
    if (planeIndex >= frame.planeCount() || rowWidth <= 0 || rowCount <= 0 || !destination) {
        return false;
    }

    const uchar *source = frame.bits(planeIndex);
    const int sourceStride = frame.bytesPerLine(planeIndex);
    if (!source || sourceStride < rowWidth) {
        return false;
    }

    for (int row = 0; row < rowCount; ++row) {
        std::memcpy(destination, source + (row * sourceStride), static_cast<size_t>(rowWidth));
        destination += rowWidth;
    }

    return true;
}

void normalizeScanLineDirection(const QVideoFrame &frame, cv::Mat &image)
{
    if (!image.empty() && frame.surfaceFormat().scanLineDirection() == QVideoFrameFormat::BottomToTop) {
        cv::flip(image, image, 0);
    }
}

bool convertMappedVideoFrameToBgr(QVideoFrame &frame, cv::Mat &bgrImage, QString &errorMessage)
{
    if (!frame.isMapped()) {
        errorMessage = QStringLiteral("错误：视频帧未完成映射");
        return false;
    }

    const int width = frame.width();
    const int height = frame.height();
    if (width <= 0 || height <= 0) {
        errorMessage = QStringLiteral("错误：视频帧尺寸无效");
        return false;
    }

    const QVideoFrameFormat::PixelFormat pixelFormat = frame.pixelFormat();
    switch (pixelFormat) {
    case QVideoFrameFormat::Format_YUV420P:
    case QVideoFrameFormat::Format_YV12: {
        if ((width % 2) != 0 || (height % 2) != 0 || frame.planeCount() < 3) {
            errorMessage = QStringLiteral("错误：YUV420 视频帧的尺寸或平面数无效");
            return false;
        }

        const size_t yPlaneSize = static_cast<size_t>(width) * static_cast<size_t>(height);
        std::vector<uchar> yuvBuffer(yPlaneSize + (yPlaneSize / 2));
        uchar *destination = yuvBuffer.data();

        if (!copyPlaneRows(frame, 0, width, height, destination)) {
            errorMessage = QStringLiteral("错误：无法读取 Y 平面数据");
            return false;
        }

        destination += yPlaneSize;
        if (!copyPlaneRows(frame, 1, width / 2, height / 2, destination)) {
            errorMessage = QStringLiteral("错误：无法读取色度平面数据");
            return false;
        }

        destination += yPlaneSize / 4;
        if (!copyPlaneRows(frame, 2, width / 2, height / 2, destination)) {
            errorMessage = QStringLiteral("错误：无法读取色度平面数据");
            return false;
        }

        cv::Mat yuvImage(height + (height / 2), width, CV_8UC1, yuvBuffer.data());
        const int conversionCode = pixelFormat == QVideoFrameFormat::Format_YV12
                ? cv::COLOR_YUV2BGR_YV12
                : cv::COLOR_YUV2BGR_I420;
        cv::cvtColor(yuvImage, bgrImage, conversionCode);
        normalizeScanLineDirection(frame, bgrImage);
        return true;
    }
    case QVideoFrameFormat::Format_NV12:
    case QVideoFrameFormat::Format_NV21: {
        if ((width % 2) != 0 || (height % 2) != 0 || frame.planeCount() < 2) {
            errorMessage = QStringLiteral("错误：NV12/NV21 视频帧的尺寸或平面数无效");
            return false;
        }

        const size_t yPlaneSize = static_cast<size_t>(width) * static_cast<size_t>(height);
        std::vector<uchar> yuvBuffer(yPlaneSize + (yPlaneSize / 2));
        uchar *destination = yuvBuffer.data();

        if (!copyPlaneRows(frame, 0, width, height, destination)) {
            errorMessage = QStringLiteral("错误：无法读取 Y 平面数据");
            return false;
        }

        destination += yPlaneSize;
        if (!copyPlaneRows(frame, 1, width, height / 2, destination)) {
            errorMessage = QStringLiteral("错误：无法读取 UV 平面数据");
            return false;
        }

        cv::Mat yuvImage(height + (height / 2), width, CV_8UC1, yuvBuffer.data());
        const int conversionCode = pixelFormat == QVideoFrameFormat::Format_NV21
                ? cv::COLOR_YUV2BGR_NV21
                : cv::COLOR_YUV2BGR_NV12;
        cv::cvtColor(yuvImage, bgrImage, conversionCode);
        normalizeScanLineDirection(frame, bgrImage);
        return true;
    }
    case QVideoFrameFormat::Format_UYVY:
    case QVideoFrameFormat::Format_YUYV: {
        if (frame.planeCount() < 1 || !frame.bits(0) || frame.bytesPerLine(0) < (width * 2)) {
            errorMessage = QStringLiteral("错误：打包 YUV422 视频帧数据无效");
            return false;
        }

        cv::Mat packedImage(
            height,
            width,
            CV_8UC2,
            const_cast<uchar *>(frame.bits(0)),
            frame.bytesPerLine(0));
        const int conversionCode = pixelFormat == QVideoFrameFormat::Format_UYVY
                ? cv::COLOR_YUV2BGR_UYVY
                : cv::COLOR_YUV2BGR_YUY2;
        cv::cvtColor(packedImage, bgrImage, conversionCode);
        normalizeScanLineDirection(frame, bgrImage);
        return true;
    }
    case QVideoFrameFormat::Format_Y8: {
        if (frame.planeCount() < 1 || !frame.bits(0) || frame.bytesPerLine(0) < width) {
            errorMessage = QStringLiteral("错误：灰度视频帧数据无效");
            return false;
        }

        cv::Mat grayImage(
            height,
            width,
            CV_8UC1,
            const_cast<uchar *>(frame.bits(0)),
            frame.bytesPerLine(0));
        cv::cvtColor(grayImage, bgrImage, cv::COLOR_GRAY2BGR);
        normalizeScanLineDirection(frame, bgrImage);
        return true;
    }
    case QVideoFrameFormat::Format_Y16: {
        if (frame.planeCount() < 1 || !frame.bits(0) || frame.bytesPerLine(0) < (width * static_cast<int>(sizeof(uint16_t)))) {
            errorMessage = QStringLiteral("错误：16 位灰度视频帧数据无效");
            return false;
        }

        cv::Mat gray16Image(
            height,
            width,
            CV_16UC1,
            const_cast<uchar *>(frame.bits(0)),
            frame.bytesPerLine(0));
        cv::Mat gray8Image;
        gray16Image.convertTo(gray8Image, CV_8U, 1.0 / 256.0);
        cv::cvtColor(gray8Image, bgrImage, cv::COLOR_GRAY2BGR);
        normalizeScanLineDirection(frame, bgrImage);
        return true;
    }
    default:
        break;
    }

    const QImage::Format imageFormat = QVideoFrameFormat::imageFormatFromPixelFormat(pixelFormat);
    if (imageFormat == QImage::Format_Invalid || frame.planeCount() < 1 || !frame.bits(0) || frame.bytesPerLine(0) <= 0) {
        errorMessage = QStringLiteral("错误：暂不支持的视频帧格式：%1")
                .arg(QVideoFrameFormat::pixelFormatToString(pixelFormat));
        return false;
    }

    QImage imageView(frame.bits(0), width, height, frame.bytesPerLine(0), imageFormat);
    if (imageView.isNull()) {
        errorMessage = QStringLiteral("错误：无法构造视频帧图像视图");
        return false;
    }

    QImage convertedImage = imageView.convertToFormat(QImage::Format_BGR888);
    if (convertedImage.isNull()) {
        errorMessage = QStringLiteral("错误：无法将视频帧转换为 BGR 图像");
        return false;
    }

    bgrImage = cv::Mat(
        convertedImage.height(),
        convertedImage.width(),
        CV_8UC3,
        convertedImage.bits(),
        convertedImage.bytesPerLine()).clone();
    normalizeScanLineDirection(frame, bgrImage);
    return true;
}

bool preprocessForDecodingWithRoiCrop(
    const cv::Mat &image,
    cv::Mat &processedGray,
    cv::Rect &roiRegion,
    cv::Size &preparedSize,
    QString &errorMessage)
{
    if (image.empty()) {
        errorMessage = QStringLiteral("错误：输入图像为空");
        return false;
    }

    // Step 1: 全图转灰度
    cv::Mat grayFull;
    if (!normalizeToGray8(image, grayFull, errorMessage)) {
        return false;
    }

    // Step 2: 狙击镜裁剪 — 取中央 60% x 60% ROI
    const int roiW = std::max(1, cvRound(grayFull.cols * 0.6));
    const int roiH = std::max(1, cvRound(grayFull.rows * 0.6));
    const int roiX = (grayFull.cols - roiW) / 2;
    const int roiY = (grayFull.rows - roiH) / 2;
    roiRegion = cv::Rect(roiX, roiY, roiW, roiH);
    cv::Mat croppedGray = grayFull(roiRegion).clone();

    // Step 3: 安全缩放 — 仅当宽度超过 800px 时等比缩小
    cv::Mat workingGray = croppedGray;
    if (croppedGray.cols > 800) {
        const double scale = 800.0 / static_cast<double>(croppedGray.cols);
        const cv::Size resizedSize(800, std::max(1, cvRound(croppedGray.rows * scale)));
        cv::resize(croppedGray, workingGray, resizedSize, 0.0, 0.0, cv::INTER_AREA);
    }
    preparedSize = workingGray.size();

    // Step 4: 抑制像素网格噪声
    cv::Mat blurredGray;
    cv::GaussianBlur(workingGray, blurredGray, cv::Size(5, 5), 0);

    // Step 5: CLAHE 自适应直方图均衡化
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(3.0, cv::Size(8, 8));
    cv::Mat claheImage;
    clahe->apply(blurredGray, claheImage);

    // Step 6: USM 锐化
    cv::Mat usmBlurred;
    cv::GaussianBlur(claheImage, usmBlurred, cv::Size(0, 0), 1.2);
    cv::addWeighted(claheImage, 1.5, usmBlurred, -0.5, 0.0, processedGray);
    return true;
}

QString buildDecodingPipelineInfo(
    const cv::Size &originalSize,
    const cv::Rect &roiRegion,
    const cv::Size &preparedSize,
    const QString &strategyLabel)
{
    QStringList steps;
    steps.append(QStringLiteral("灰度"));
    steps.append(QStringLiteral("ROI裁剪(%1x%2→%3x%4)")
                     .arg(originalSize.width)
                     .arg(originalSize.height)
                     .arg(roiRegion.width)
                     .arg(roiRegion.height));
    if (preparedSize.width != roiRegion.width || preparedSize.height != roiRegion.height) {
        steps.append(QStringLiteral("安全缩放(%1x%2)")
                         .arg(preparedSize.width)
                         .arg(preparedSize.height));
    }
    steps.append(QStringLiteral("CLAHE"));
    steps.append(QStringLiteral("USM"));
    if (!strategyLabel.isEmpty()) {
        steps.append(strategyLabel);
    }
    steps.append(QStringLiteral("quirc"));
    return QStringLiteral("狙击管线：") + steps.join(QStringLiteral(" -> "));
}

} // namespace

QRProcessor::QRProcessor(QObject *parent)
    : QObject(parent)
{
}

QRProcessor::~QRProcessor()
{
}

void QRProcessor::emitDiagnosticLog(const QString &message)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    emit diagnosticLogReady(QStringLiteral("[%1] %2").arg(timestamp, message));
}

void QRProcessor::clearDetectionState()
{
    emit boxDetected(0, 0, 0, 0);
    emit quadDetected(0, 0, 0, 0, 0, 0, 0, 0);
    emit multiQuadDetected({});
}

void QRProcessor::processImage(const QString &imagePath)
{
    const QString localPath = normalizeImagePath(imagePath);
    emitDiagnosticLog(QStringLiteral("图片识别开始：%1").arg(localPath));

    cv::Mat image = cv::imread(localPath.toStdString(), cv::IMREAD_COLOR);
    if (image.empty()) {
        clearDetectionState();
        emit pipelineInfoReady(QStringLiteral("读取图片失败"));
        emit resultReady(QStringLiteral("错误：无法读取图片\n路径：") + localPath);
        emitDiagnosticLog(QStringLiteral("图片读取失败：路径=%1").arg(localPath));
        return;
    }

    processMatImage(image, false, true);
}

void QRProcessor::resetScanState()
{
    const ScanState previousState = m_scanState.exchange(Searching, std::memory_order_acq_rel);
    if (previousState != Searching) {
        emitDiagnosticLog(QStringLiteral("状态切换：%1 -> 搜索中").arg(scanStateLabel(previousState)));
    }

    if (!m_isProcessing.load(std::memory_order_acquire)) {
        clearDetectionState();
    }
}

void QRProcessor::processVideoFrame(const QVideoFrame &frame)
{
    if (m_scanState.load(std::memory_order_acquire) != Searching) {
        return;
    }

    if (m_isProcessing.load(std::memory_order_acquire)) {
        return;
    }

    if (!frame.isValid()) {
        return;
    }

    QImage image = frame.toImage();
    if (image.isNull()) {
        return;
    }

    QImage safeImage = image.convertToFormat(QImage::Format_RGBA8888);
    if (safeImage.isNull()) {
        return;
    }

    cv::Mat probeImage;
    if (!convertQImageToBgrMat(safeImage, probeImage)) {
        return;
    }

    QString grayError;
    cv::Mat grayImage;
    if (!normalizeToGray8(probeImage, grayImage, grayError)) {
        emitDiagnosticLog(QStringLiteral("实时帧灰度转换失败：%1").arg(grayError));
        return;
    }

    const double blurVariance = computeLaplacianVariance(grayImage);
    if (blurVariance < kFrameVarianceTriggerThreshold) {
        emitDiagnosticLog(
            QStringLiteral("帧评估：方差=%1，阈值=%2，结论=未通过，跳过本帧")
                .arg(blurVariance, 0, 'f', 1)
                .arg(kFrameVarianceTriggerThreshold, 0, 'f', 1));
        return;
    }

    emitDiagnosticLog(
        QStringLiteral("帧评估：方差=%1，阈值=%2，结论=通过，进入候选定位")
            .arg(blurVariance, 0, 'f', 1)
            .arg(kFrameVarianceTriggerThreshold, 0, 'f', 1));

    cv::Mat probeUsmGray;
    cv::Size probePreparedSize;
    double probeBlurVariance = 0.0;
    bool probeSkippedForBlur = false;
    QString probeError;
    if (!preprocessImageForQuirc(
            probeImage,
            probeUsmGray,
            probePreparedSize,
            probeBlurVariance,
            false,
            probeSkippedForBlur,
            probeError)) {
        emitDiagnosticLog(QStringLiteral("实时预处理失败：%1").arg(probeError));
        return;
    }

    QuircLocatorMetrics locatorMetrics;
    if (!collectQuircLocatorMetrics(probeUsmGray, locatorMetrics)) {
        emitDiagnosticLog(QStringLiteral("候选定位失败：quirc 定位器无法返回候选框"));
        return;
    }

    const int maxQrSide = std::max(locatorMetrics.maxBoxWidth, locatorMetrics.maxBoxHeight);
    const int adaptiveMinSide = std::clamp(std::min(probePreparedSize.width, probePreparedSize.height) / 10, 72, 160);
    const int sideThreshold = std::max(adaptiveMinSide, kAutoSnapshotMinQrBoxSidePx);
    const bool sizeQualified = locatorMetrics.candidateCount > 0
        && locatorMetrics.maxAreaRatio >= kAutoSnapshotMinQrAreaRatio
        && maxQrSide >= sideThreshold;
    if (!sizeQualified) {
        emitDiagnosticLog(
            QStringLiteral("定位判定：候选=%1，面积占比=%2(阈值=%3)，最大边=%4px(阈值=%5px)，触发抓拍=否")
                .arg(locatorMetrics.candidateCount)
                .arg(locatorMetrics.maxAreaRatio, 0, 'f', 4)
                .arg(kAutoSnapshotMinQrAreaRatio, 0, 'f', 4)
                .arg(maxQrSide)
                .arg(sideThreshold));
        return;
    }

    emitDiagnosticLog(
        QStringLiteral("定位判定：候选=%1，面积占比=%2(阈值=%3)，最大边=%4px(阈值=%5px)，触发抓拍=是")
            .arg(locatorMetrics.candidateCount)
            .arg(locatorMetrics.maxAreaRatio, 0, 'f', 4)
            .arg(kAutoSnapshotMinQrAreaRatio, 0, 'f', 4)
            .arg(maxQrSide)
            .arg(sideThreshold));

    ScanState expectedState = Searching;
    if (!m_scanState.compare_exchange_strong(expectedState, Decoding, std::memory_order_acq_rel)) {
        emitDiagnosticLog(QStringLiteral("状态切换失败：当前状态=%1，本帧放弃抓拍").arg(scanStateLabel(expectedState)));
        return;
    }

    emitDiagnosticLog(QStringLiteral("状态切换：搜索中 -> 解码中，开始自动抓拍"));
    m_isProcessing.store(true, std::memory_order_release);
    const QImage snapshotImage = safeImage.copy();
    if (snapshotImage.isNull()) {
        m_scanState.store(Searching, std::memory_order_release);
        m_isProcessing.store(false, std::memory_order_release);
        emitDiagnosticLog(QStringLiteral("抓拍失败：快照为空，状态切换：解码中 -> 搜索中"));
        emit decodeFinished();
        return;
    }

    m_decodeFuture = QtConcurrent::run([this, snapshotImage]() {
        const auto releaseProcessingLockInWorker = [this]() {
            m_isProcessing.store(false, std::memory_order_release);
            emit decodeFinished();
        };

        QString snapshotPath;
        QString snapshotError;
        if (!saveAutoSnapshot(snapshotImage, snapshotPath, snapshotError)) {
            m_scanState.store(Searching, std::memory_order_release);
            emit pipelineInfoReady(QStringLiteral("自动抓拍失败"));
            emitDiagnosticLog(QStringLiteral("自动抓拍失败：%1；状态切换：解码中 -> 搜索中").arg(snapshotError));
            emit errorOccurred(snapshotError);
            releaseProcessingLockInWorker();
            return;
        }

        emitDiagnosticLog(QStringLiteral("自动抓拍成功：%1").arg(snapshotPath));

        cv::Mat decodeImage = cv::imread(QFile::encodeName(snapshotPath).toStdString(), cv::IMREAD_COLOR);
        if (decodeImage.empty()) {
            emitDiagnosticLog(QStringLiteral("抓拍文件读取失败，尝试内存回退：%1").arg(snapshotPath));
            if (!convertQImageToBgrMat(snapshotImage, decodeImage)) {
                m_scanState.store(Searching, std::memory_order_release);
                emit pipelineInfoReady(QStringLiteral("抓拍读取失败"));
                emitDiagnosticLog(QStringLiteral("抓拍回退失败：状态切换：解码中 -> 搜索中"));
                emit errorOccurred(QStringLiteral("错误：抓拍图片读取失败\n路径：") + snapshotPath);
                releaseProcessingLockInWorker();
                return;
            }
        }

        const bool decodeSuccess = processMatImage(decodeImage, false, false, true);
        ScanState expected = Decoding;
        if (decodeSuccess) {
            if (m_scanState.compare_exchange_strong(expected, Done, std::memory_order_acq_rel)) {
                emitDiagnosticLog(QStringLiteral("解码成功：状态切换：解码中 -> 已完成"));
            } else {
                emitDiagnosticLog(QStringLiteral("解码成功：状态已被外部修改，当前状态=%1").arg(scanStateLabel(expected)));
            }
        } else {
            expected = Decoding;
            if (m_scanState.compare_exchange_strong(expected, Searching, std::memory_order_acq_rel)) {
                emitDiagnosticLog(QStringLiteral("解码失败：状态切换：解码中 -> 搜索中"));
            } else {
                emitDiagnosticLog(QStringLiteral("解码失败：状态已被外部修改，当前状态=%1").arg(scanStateLabel(expected)));
            }
        }

        releaseProcessingLockInWorker();
    });
}

bool QRProcessor::processMatImage(const cv::Mat &image, bool realtimeMode, bool emitMissResult, bool useRoiCrop)
{
    if (image.empty()) {
        clearDetectionState();
        emit pipelineInfoReady(QStringLiteral("输入图像为空"));
        emit errorOccurred(QStringLiteral("错误：输入图像为空"));
        emitDiagnosticLog(QStringLiteral("解码失败：输入图像为空"));
        return false;
    }

    emitDiagnosticLog(
        QStringLiteral("进入解码管线：输入=%1x%2，模式=%3，ROI裁剪=%4")
            .arg(image.cols)
            .arg(image.rows)
            .arg(realtimeMode ? QStringLiteral("实时") : QStringLiteral("非实时"))
            .arg(useRoiCrop ? QStringLiteral("开启") : QStringLiteral("关闭")));

    cv::Mat usmGray;
    cv::Size preparedSize;
    cv::Rect roiRegion;
    double blurVariance = 0.0;
    bool skippedForBlur = false;
    QString preprocessingError;
    if (useRoiCrop) {
        if (!preprocessForDecodingWithRoiCrop(image, usmGray, roiRegion, preparedSize, preprocessingError)) {
            clearDetectionState();
            emit pipelineInfoReady(QStringLiteral("预处理失败"));
            emit errorOccurred(preprocessingError);
            emitDiagnosticLog(QStringLiteral("预处理失败：%1").arg(preprocessingError));
            return false;
        }

        emitDiagnosticLog(
            QStringLiteral("预处理完成：ROI=(%1,%2,%3x%4)，处理尺寸=%5x%6")
                .arg(roiRegion.x)
                .arg(roiRegion.y)
                .arg(roiRegion.width)
                .arg(roiRegion.height)
                .arg(preparedSize.width)
                .arg(preparedSize.height));
    } else {
        if (!preprocessImageForQuirc(
                image,
                usmGray,
                preparedSize,
                blurVariance,
                realtimeMode,
                skippedForBlur,
                preprocessingError)) {
            if (realtimeMode && skippedForBlur) {
                emitDiagnosticLog(
                    QStringLiteral("预处理跳过：方差=%1，阈值=%2，结论=未通过")
                        .arg(blurVariance, 0, 'f', 1)
                        .arg(kBlurVarianceThreshold, 0, 'f', 1));
                return false;
            }

            clearDetectionState();
            emit pipelineInfoReady(QStringLiteral("预处理失败"));
            emit errorOccurred(preprocessingError);
            emitDiagnosticLog(QStringLiteral("预处理失败：%1").arg(preprocessingError));
            return false;
        }

        emitDiagnosticLog(
            QStringLiteral("预处理完成：处理尺寸=%1x%2，模糊方差=%3，模糊过滤=%4")
                .arg(preparedSize.width)
                .arg(preparedSize.height)
                .arg(blurVariance, 0, 'f', 1)
                .arg(realtimeMode ? QStringLiteral("开启") : QStringLiteral("关闭")));
    }

    clearDetectionState();

    const auto emitScaledDetections = [this, &image, &roiRegion](const QVariantList &sourceQuads, const cv::Size &candidateSize) {
        if (candidateSize.width <= 0 || candidateSize.height <= 0) {
            emit multiQuadDetected({});
            return;
        }

        const int targetW = (roiRegion.width > 0) ? roiRegion.width : image.cols;
        const int targetH = (roiRegion.height > 0) ? roiRegion.height : image.rows;
        const int offsetX = (roiRegion.width > 0) ? roiRegion.x : 0;
        const int offsetY = (roiRegion.height > 0) ? roiRegion.y : 0;
        const double scaleX = static_cast<double>(targetW) / static_cast<double>(candidateSize.width);
        const double scaleY = static_cast<double>(targetH) / static_cast<double>(candidateSize.height);
        QVariantList scaledQuads;
        int minX = image.cols;
        int minY = image.rows;
        int maxX = 0;
        int maxY = 0;

        for (const QVariant &quadValue : sourceQuads) {
            const QVariantMap sourceQuad = quadValue.toMap();
            if (sourceQuad.isEmpty()) {
                continue;
            }

            QVariantMap scaledQuad;
            const int x1 = offsetX + cvRound(sourceQuad.value(QStringLiteral("x1")).toDouble() * scaleX);
            const int y1 = offsetY + cvRound(sourceQuad.value(QStringLiteral("y1")).toDouble() * scaleY);
            const int x2 = offsetX + cvRound(sourceQuad.value(QStringLiteral("x2")).toDouble() * scaleX);
            const int y2 = offsetY + cvRound(sourceQuad.value(QStringLiteral("y2")).toDouble() * scaleY);
            const int x3 = offsetX + cvRound(sourceQuad.value(QStringLiteral("x3")).toDouble() * scaleX);
            const int y3 = offsetY + cvRound(sourceQuad.value(QStringLiteral("y3")).toDouble() * scaleY);
            const int x4 = offsetX + cvRound(sourceQuad.value(QStringLiteral("x4")).toDouble() * scaleX);
            const int y4 = offsetY + cvRound(sourceQuad.value(QStringLiteral("y4")).toDouble() * scaleY);

            scaledQuad.insert(QStringLiteral("x1"), x1);
            scaledQuad.insert(QStringLiteral("y1"), y1);
            scaledQuad.insert(QStringLiteral("x2"), x2);
            scaledQuad.insert(QStringLiteral("y2"), y2);
            scaledQuad.insert(QStringLiteral("x3"), x3);
            scaledQuad.insert(QStringLiteral("y3"), y3);
            scaledQuad.insert(QStringLiteral("x4"), x4);
            scaledQuad.insert(QStringLiteral("y4"), y4);
            scaledQuads.append(scaledQuad);

            minX = std::min(minX, std::min(std::min(x1, x2), std::min(x3, x4)));
            minY = std::min(minY, std::min(std::min(y1, y2), std::min(y3, y4)));
            maxX = std::max(maxX, std::max(std::max(x1, x2), std::max(x3, x4)));
            maxY = std::max(maxY, std::max(std::max(y1, y2), std::max(y3, y4)));
        }

        emit multiQuadDetected(scaledQuads);

        if (scaledQuads.isEmpty()) {
            return;
        }

        const QVariantMap firstQuad = scaledQuads.first().toMap();
        emit quadDetected(
            firstQuad.value(QStringLiteral("x1")).toInt(), firstQuad.value(QStringLiteral("y1")).toInt(),
            firstQuad.value(QStringLiteral("x2")).toInt(), firstQuad.value(QStringLiteral("y2")).toInt(),
            firstQuad.value(QStringLiteral("x3")).toInt(), firstQuad.value(QStringLiteral("y3")).toInt(),
            firstQuad.value(QStringLiteral("x4")).toInt(), firstQuad.value(QStringLiteral("y4")).toInt());

        emit boxDetected(
            std::max(0, minX),
            std::max(0, minY),
            std::max(0, maxX - minX),
            std::max(0, maxY - minY));
    };

    struct DecodeCandidate {
        QString strategyLabel;
        cv::Mat image;
    };

    std::vector<DecodeCandidate> candidates;
    candidates.push_back({QStringLiteral("策略A-USM灰度"), usmGray});

    cv::Mat otsuBinary;
    cv::threshold(usmGray, otsuBinary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    candidates.push_back({QStringLiteral("策略B-Otsu二值"), otsuBinary});

    const int adaptiveBlockSize = adaptiveBlockSizeFor(usmGray.size());
    if (adaptiveBlockSize >= 3) {
        cv::Mat adaptiveBinary;
        cv::adaptiveThreshold(
            usmGray,
            adaptiveBinary,
            255,
            cv::ADAPTIVE_THRESH_GAUSSIAN_C,
            cv::THRESH_BINARY,
            adaptiveBlockSize,
            5);
        candidates.push_back({QStringLiteral("策略C-自适应二值"), adaptiveBinary});
    }

    for (const DecodeCandidate &candidate : candidates) {
        QString decodedText;
        QVariantList detectedQuads;
        if (!decodeWithQuirc(candidate.image, decodedText, &detectedQuads)) {
            emitDiagnosticLog(QStringLiteral("策略未命中：%1").arg(candidate.strategyLabel));
            continue;
        }

        emitDiagnosticLog(
            QStringLiteral("策略命中：%1，检测到二维码数=%2")
                .arg(candidate.strategyLabel)
                .arg(detectedQuads.size()));

        emitScaledDetections(detectedQuads, candidate.image.size());
        if (useRoiCrop) {
            emit pipelineInfoReady(buildDecodingPipelineInfo(image.size(), roiRegion, preparedSize, candidate.strategyLabel));
        } else {
            emit pipelineInfoReady(buildPipelineInfo(image.size(), preparedSize, realtimeMode, blurVariance, candidate.strategyLabel));
        }
        emit resultReady(decodedText);
        return true;
    }

    if (useRoiCrop) {
        emit pipelineInfoReady(buildDecodingPipelineInfo(image.size(), roiRegion, preparedSize, QStringLiteral("策略A/B/C未命中")));
    } else {
        emit pipelineInfoReady(buildPipelineInfo(image.size(), preparedSize, realtimeMode, blurVariance, QStringLiteral("策略A/B/C未命中")));
    }
    if (emitMissResult) {
        emit resultReady(QStringLiteral("未识别到二维码\n请保持二维码完整、清晰，并避免剧烈抖动"));
    }

    emitDiagnosticLog(QStringLiteral("解码结束：策略A/B/C均未命中"));

    return false;
}

bool QRProcessor::decodeWithQuirc(const cv::Mat &image, QString &decodedText, QVariantList *detectedQuads) const
{
    if (detectedQuads) {
        detectedQuads->clear();
    }

    if (image.empty() || image.type() != CV_8UC1) {
        return false;
    }

    struct quirc *qr = quirc_new();
    if (!qr) {
        return false;
    }

    if (quirc_resize(qr, image.cols, image.rows) < 0) {
        quirc_destroy(qr);
        return false;
    }

    int quircWidth = 0;
    int quircHeight = 0;
    uint8_t *imageData = quirc_begin(qr, &quircWidth, &quircHeight);
    if (!imageData) {
        quirc_destroy(qr);
        return false;
    }

    if (image.isContinuous()) {
        std::memcpy(imageData, image.data, image.total());
    } else {
        for (int row = 0; row < image.rows; ++row) {
            const unsigned char *sourceRow = image.ptr<unsigned char>(row);
            std::memcpy(imageData + (row * image.cols), sourceRow, static_cast<size_t>(image.cols));
        }
    }

    quirc_end(qr);

    const int codeCount = quirc_count(qr);
    QStringList decodedResults;
    for (int index = 0; index < codeCount; ++index) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(qr, index, &code);

        if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
            decodedResults.append(QString::fromUtf8(
                reinterpret_cast<const char *>(data.payload),
                data.payload_len));

            if (detectedQuads) {
                QVariantMap quad;
                quad.insert(QStringLiteral("x1"), code.corners[0].x);
                quad.insert(QStringLiteral("y1"), code.corners[0].y);
                quad.insert(QStringLiteral("x2"), code.corners[1].x);
                quad.insert(QStringLiteral("y2"), code.corners[1].y);
                quad.insert(QStringLiteral("x3"), code.corners[2].x);
                quad.insert(QStringLiteral("y3"), code.corners[2].y);
                quad.insert(QStringLiteral("x4"), code.corners[3].x);
                quad.insert(QStringLiteral("y4"), code.corners[3].y);
                detectedQuads->append(quad);
            }
        }
    }

    quirc_destroy(qr);
    if (decodedResults.isEmpty()) {
        return false;
    }

    decodedText = decodedResults.join(QStringLiteral("\n================\n"));
    return true;
}

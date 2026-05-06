#ifndef QRPROCESSOR_H
#define QRPROCESSOR_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVideoFrame>
#include <QImage>
#include <QFuture>
#include <QtConcurrent>
#include <atomic>
#include <opencv2/opencv.hpp>
#include "quirc.h"

class QRProcessor : public QObject
{
    Q_OBJECT

public:
    enum ScanState {
        Searching,
        Decoding,
        Done
    };
    Q_ENUM(ScanState)

    explicit QRProcessor(QObject *parent = nullptr);
    ~QRProcessor();

    Q_INVOKABLE void processImage(const QString &imagePath);
    Q_INVOKABLE void processVideoFrame(const QVideoFrame &frame);
    Q_INVOKABLE void resetScanState();

signals:
    void resultReady(QString text);
    void errorOccurred(const QString &error);
    void boxDetected(int x, int y, int width, int height);
    void quadDetected(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4);
    void multiQuadDetected(const QVariantList &quads);
    void decodeFinished();
    void pipelineInfoReady(const QString &info);
    void diagnosticLogReady(const QString &line);

private:
    void emitDiagnosticLog(const QString &message);
    void clearDetectionState();
    bool processMatImage(const cv::Mat &image, bool realtimeMode, bool emitMissResult, bool useRoiCrop = false);
    bool decodeWithQuirc(const cv::Mat &image, QString &decodedText, QVariantList *detectedQuads = nullptr) const;

    std::atomic<bool> m_isProcessing{false};
    std::atomic<ScanState> m_scanState{Searching};
    QFuture<void> m_decodeFuture;
};

#endif // QRPROCESSOR_H

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import com.qrcode.app 1.0

Window {
    id: mainWindow
    width: 980
    height: 720
    minimumWidth: 420
    minimumHeight: 620
    visible: true
    title: "QR Studio"

    color: "#f1ece4"

    property bool compactLayout: width < 760
    property bool liveScanEnabled: false
    property string inputMode: "camera"
    property int scanFrameIntervalMs: 80
    property int liveResultCooldownMs: 900
    property bool liveResultCooldown: false
    property string lastAcceptedLiveResult: ""
    property var detectedQuads: []
    property url selectedImageUrl: ""
    property string selectedImageText: ""
    property string pipelineInfoText: "等待识别"
    property int detectedBoxX: 0
    property int detectedBoxY: 0
    property int detectedBoxWidth: 0
    property int detectedBoxHeight: 0
    property bool detectedQuadValid: false
    property int quadX1: 0
    property int quadY1: 0
    property int quadX2: 0
    property int quadY2: 0
    property int quadX3: 0
    property int quadY3: 0
    property int quadX4: 0
    property int quadY4: 0
    property int maxDiagnosticLogEntries: 320
    readonly property bool cameraMode: inputMode === "camera"
    readonly property bool cameraAvailable: mediaDevices.videoInputs.length > 0

    ListModel {
        id: diagnosticLogModel
    }

    function resetDetectedBox() {
        detectedBoxX = 0
        detectedBoxY = 0
        detectedBoxWidth = 0
        detectedBoxHeight = 0
        detectedQuads = []
        detectedQuadValid = false
        quadX1 = 0
        quadY1 = 0
        quadX2 = 0
        quadY2 = 0
        quadX3 = 0
        quadY3 = 0
        quadX4 = 0
        quadY4 = 0
    }

    function resetLiveResultControl() {
        lastAcceptedLiveResult = ""
        liveResultCooldown = false
        liveResultCooldownTimer.stop()
    }

    function appendDiagnosticLog(line) {
        if (line === undefined || line === null || line.length === 0) {
            return
        }

        diagnosticLogModel.append({ line: line })
        if (diagnosticLogModel.count > maxDiagnosticLogEntries) {
            diagnosticLogModel.remove(0, diagnosticLogModel.count - maxDiagnosticLogEntries)
        }

        Qt.callLater(function() {
            if (logListView.count > 0) {
                logListView.positionViewAtEnd()
            }
        })
    }

    function clearDiagnosticLog() {
        diagnosticLogModel.clear()
    }

    function applyIdleStatus(text) {
        statusText.text = text
        statusText.color = "#56666a"
        statusBadge.color = "#ecf0f2"
        statusBadge.border.color = "#c4d0d6"
    }

    function applyBusyStatus(text) {
        statusText.text = text
        statusText.color = "#6d4b00"
        statusBadge.color = "#fff4d6"
        statusBadge.border.color = "#e8ca7a"
    }

    function applyErrorStatus(text) {
        statusText.text = text
        statusText.color = "#b42318"
        statusBadge.color = "#ffe4dd"
        statusBadge.border.color = "#f1b7a8"
    }

    function applySuccessStatus(text) {
        statusText.text = text
        statusText.color = "#0c6b58"
        statusBadge.color = "#dcf8ec"
        statusBadge.border.color = "#84d6b9"
    }

    function isTransientLiveScanMiss(text) {
        return text.startsWith("未发现二维码定位框") ||
               text.startsWith("二维码区域已定位，但 quirc 解码失败") ||
               text.startsWith("未识别到二维码")
    }

    function isSupportedImage(pathText) {
        var lower = pathText.toLowerCase()
        return lower.endsWith(".png") || lower.endsWith(".jpg") ||
               lower.endsWith(".jpeg") || lower.endsWith(".bmp") ||
               lower.endsWith(".gif")
    }

    function startLiveScan() {
        inputMode = "camera"
        qrProcessor.resetScanState()
        selectedImageUrl = ""
        selectedImageText = ""
        resetDetectedBox()
        resetLiveResultControl()
        frameThrottle.stop()

        if (!cameraAvailable) {
            liveScanEnabled = false
            pipelineInfoText = "未检测到摄像头"
            applyErrorStatus("未检测到摄像头")
            resultText.text = "未检测到可用摄像头，请连接摄像头后重试，或切换到图片识别。"
            return
        }

        liveScanEnabled = true
        pipelineInfoText = "实时扫描中"
        applyBusyStatus("等待二维码进入画面")
        if (resultText.text === "" || resultText.text.startsWith("未检测到可用摄像头")) {
            resultText.text = "等待识别到二维码..."
        }
    }

    function toggleLiveScan() {
        if (!cameraMode) {
            startLiveScan()
            return
        }

        if (!cameraAvailable) {
            startLiveScan()
            return
        }

        liveScanEnabled = !liveScanEnabled
        resetDetectedBox()
        resetLiveResultControl()
        frameThrottle.stop()

        if (liveScanEnabled) {
            qrProcessor.resetScanState()
            pipelineInfoText = "实时扫描中"
            applyBusyStatus("等待二维码进入画面")
            if (resultText.text === "") {
                resultText.text = "等待识别到二维码..."
            }
        } else {
            pipelineInfoText = "实时扫描已暂停"
            applyIdleStatus("实时扫描已暂停")
        }
    }

    function beginRecognition(fileUrl) {
        var urlText = fileUrl.toString()
        if (!isSupportedImage(urlText)) {
            inputMode = "image"
            liveScanEnabled = false
            qrProcessor.resetScanState()
            resetDetectedBox()
            pipelineInfoText = "未开始识别"
            applyErrorStatus("文件类型不支持")
            resultText.text = "仅支持 PNG / JPG / JPEG / BMP / GIF"
            return
        }

        inputMode = "image"
        liveScanEnabled = false
        qrProcessor.resetScanState()
        selectedImageUrl = fileUrl
        selectedImageText = urlText.replace("file:///", "")
        resetDetectedBox()
        resetLiveResultControl()
        frameThrottle.stop()
        pipelineInfoText = "识别中"
        applyBusyStatus("正在识别...")
        resultText.text = ""
        qrProcessor.processImage(urlText)
    }

    function updateStatusByResult(text) {
        if (cameraMode && isTransientLiveScanMiss(text)) {
            if (liveScanEnabled) {
                applyBusyStatus("等待二维码进入画面")
            }
            return
        }

        if (text.startsWith("错误") || text.startsWith("未发现") || text.startsWith("无法") || text.startsWith("二维码区域已定位")) {
            applyErrorStatus(cameraMode ? "实时扫码失败" : "识别失败")
        } else {
            applySuccessStatus("识别成功")
        }
    }

    function clearResults() {
        qrProcessor.resetScanState()
        resetDetectedBox()
        resetLiveResultControl()
        clearDiagnosticLog()
        if (cameraMode) {
            selectedImageUrl = ""
            selectedImageText = ""
            pipelineInfoText = liveScanEnabled ? "实时扫描中" : "实时扫描已暂停"
            resultText.text = liveScanEnabled ? "等待识别到二维码..." : ""
            if (liveScanEnabled) {
                applyBusyStatus("等待二维码进入画面")
            } else {
                applyIdleStatus("实时扫描已暂停")
            }
        } else {
            selectedImageUrl = ""
            selectedImageText = ""
            pipelineInfoText = "等待识别"
            resultText.text = ""
            applyIdleStatus("等待选择二维码图片")
        }
    }

    function selectOptimalCamera() {
        if (!mediaDevices || mediaDevices.videoInputs.length === 0) {
            return
        }

        var selectedDevice = null
        for (var i = 0; i < mediaDevices.videoInputs.length; ++i) {
            var device = mediaDevices.videoInputs[i]
            if (device.description && device.description.includes("Iriun")) {
                selectedDevice = device
                console.log("Found Iriun camera: " + device.description)
                break
            }
        }

        if (selectedDevice) {
            liveCamera.cameraDevice = selectedDevice
            console.log("Switched to Iriun camera")
        } else {
            liveCamera.cameraDevice = mediaDevices.defaultVideoInput
            console.log("Iriun camera not found, using default camera")
        }
    }

    MediaDevices {
        id: mediaDevices
        onVideoInputsChanged: selectOptimalCamera()
    }

    CaptureSession {
        id: captureSession
        camera: Camera {
            id: liveCamera
            cameraDevice: mediaDevices.defaultVideoInput
            focusMode: Camera.FocusModeContinuous !== undefined
                       ? Camera.FocusModeContinuous
                       : Camera.FocusModeAutoNear
            active: mainWindow.cameraMode && mainWindow.liveScanEnabled && mainWindow.cameraAvailable

            Component.onCompleted: {
                selectOptimalCamera()
            }
        }
        videoOutput: liveViewfinder
    }

    Timer {
        id: frameThrottle
        interval: scanFrameIntervalMs
        repeat: false
    }

    Timer {
        id: liveResultCooldownTimer
        interval: liveResultCooldownMs
        repeat: false
        onTriggered: {
            liveResultCooldown = false
            if (cameraMode && liveScanEnabled) {
                qrProcessor.resetScanState()
            }
        }
    }

    QRProcessor {
        id: qrProcessor
        onBoxDetected: (x, y, width, height) => {
            detectedBoxX = x
            detectedBoxY = y
            detectedBoxWidth = width
            detectedBoxHeight = height
        }
        onQuadDetected: (x1, y1, x2, y2, x3, y3, x4, y4) => {
            quadX1 = x1
            quadY1 = y1
            quadX2 = x2
            quadY2 = y2
            quadX3 = x3
            quadY3 = y3
            quadX4 = x4
            quadY4 = y4
            detectedQuadValid = !(x1 === 0 && y1 === 0
                                  && x2 === 0 && y2 === 0
                                  && x3 === 0 && y3 === 0
                                  && x4 === 0 && y4 === 0)
        }
        onMultiQuadDetected: (quads) => {
            detectedQuads = quads
            if (quads.length > 0) {
                var first = quads[0]
                quadX1 = first.x1
                quadY1 = first.y1
                quadX2 = first.x2
                quadY2 = first.y2
                quadX3 = first.x3
                quadY3 = first.y3
                quadX4 = first.x4
                quadY4 = first.y4
                detectedQuadValid = true
            } else {
                detectedQuadValid = false
            }
        }
        onPipelineInfoReady: (info) => {
            pipelineInfoText = info
        }
        onDiagnosticLogReady: (line) => {
            appendDiagnosticLog(line)
        }
        onResultReady: (text) => {
            if (cameraMode && isTransientLiveScanMiss(text)) {
                if (resultText.text === "") {
                    resultText.text = "等待识别到二维码..."
                }
                updateStatusByResult(text)
                return
            }

            if (cameraMode) {
                if (text === lastAcceptedLiveResult) {
                    return
                }

                lastAcceptedLiveResult = text
                liveResultCooldown = true
                liveResultCooldownTimer.stop()
                liveResultCooldownTimer.start()
            }

            resultText.text = text
            updateStatusByResult(text)
        }
        onErrorOccurred: (error) => {
            resetDetectedBox()
            pipelineInfoText = "处理异常"
            resultText.text = error
            applyErrorStatus(cameraMode ? "实时扫码异常" : "识别失败")
        }
    }

    FileDialog {
        id: fileDialog
        title: "选择图片"
        nameFilters: ["图片文件 (*.png *.jpg *.jpeg *.bmp *.gif)"]
        onAccepted: {
            beginRecognition(fileDialog.selectedFile)
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: "#f5efe7"
            }
            GradientStop {
                position: 1.0
                color: "#ebe3d8"
            }
        }
    }

    Rectangle {
        width: compactLayout ? 280 : 420
        height: width
        radius: width / 2
        color: "#8ecdc0"
        opacity: 0.14
        x: compactLayout ? -140 : -120
        y: compactLayout ? -90 : -110
    }

    Rectangle {
        width: compactLayout ? 260 : 380
        height: width
        radius: width / 2
        color: "#e8a24a"
        opacity: 0.12
        x: width - (compactLayout ? 180 : 250)
        y: height - (compactLayout ? 150 : 210)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: compactLayout ? 16 : 26
        spacing: compactLayout ? 14 : 18

        Rectangle {
            id: heroCard
            Layout.fillWidth: true
            Layout.preferredHeight: compactLayout ? 138 : 124
            radius: 16
            border.color: "#65b7aa"
            border.width: 1
            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: "#0f766e"
                }
                GradientStop {
                    position: 1.0
                    color: "#155e75"
                }
            }
            opacity: 0.0

            Behavior on opacity {
                NumberAnimation {
                    duration: 480
                    easing.type: Easing.OutCubic
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: compactLayout ? 14 : 18
                spacing: 8

                Text {
                    text: "QR Studio"
                    font.pixelSize: compactLayout ? 30 : 34
                    font.bold: true
                    font.family: "Bahnschrift"
                    color: "#f7fbfb"
                }

                Text {
                    text: "实时摄像头扫码与图片识别共用同一套解码链路，实时模式下每秒可处理约 12 帧。"
                    font.pixelSize: compactLayout ? 13 : 14
                    font.family: "Microsoft YaHei UI"
                    color: "#d4f0ec"
                    wrapMode: Text.Wrap
                }
            }
        }

        Rectangle {
            id: controlCard
            Layout.fillWidth: true
            Layout.preferredHeight: compactLayout ? 108 : 64
            color: "#f9f5ef"
            radius: 14
            border.color: "#d8cab7"
            border.width: 1
            opacity: 0.0

            Behavior on opacity {
                NumberAnimation {
                    duration: 520
                    easing.type: Easing.OutCubic
                }
            }

            GridLayout {
                anchors.fill: parent
                anchors.margins: 12
                columns: compactLayout ? 2 : 4
                rowSpacing: 8
                columnSpacing: 10

                Button {
                    id: liveButton
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40

                    background: Rectangle {
                        radius: 10
                        color: liveButton.pressed ? "#176057" : (liveButton.hovered ? "#1f8779" : "#0f766e")

                        Behavior on color {
                            ColorAnimation {
                                duration: 160
                            }
                        }
                    }

                    contentItem: Text {
                        text: !cameraMode ? "返回实时扫码" : (liveScanEnabled ? "暂停扫码" : "启动扫码")
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: "#ecfbf8"
                        font.pixelSize: 15
                        font.bold: true
                        font.family: "Microsoft YaHei UI"
                    }

                    onClicked: toggleLiveScan()
                }

                Button {
                    id: chooseButton
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40

                    background: Rectangle {
                        radius: 10
                        color: chooseButton.pressed ? "#c96c00" : (chooseButton.hovered ? "#eb8a16" : "#d97706")

                        Behavior on color {
                            ColorAnimation {
                                duration: 160
                            }
                        }
                    }

                    contentItem: Text {
                        text: "选择图片"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: "#fff7ea"
                        font.pixelSize: 15
                        font.bold: true
                        font.family: "Microsoft YaHei UI"
                    }

                    onClicked: fileDialog.open()
                }

                Button {
                    id: clearButton
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40

                    background: Rectangle {
                        radius: 10
                        color: clearButton.pressed ? "#176057" : (clearButton.hovered ? "#1f8779" : "#0f766e")

                        Behavior on color {
                            ColorAnimation {
                                duration: 160
                            }
                        }
                    }

                    contentItem: Text {
                        text: "清空结果"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: "#ecfbf8"
                        font.pixelSize: 15
                        font.bold: true
                        font.family: "Microsoft YaHei UI"
                    }

                    onClicked: clearResults()
                }

                Rectangle {
                    id: statusBadge
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    Layout.columnSpan: compactLayout ? 2 : 1
                    radius: 10
                    color: "#ecf0f2"
                    border.color: "#c4d0d6"
                    border.width: 1

                    Text {
                        id: statusText
                        anchors.centerIn: parent
                        text: "等待选择二维码图片"
                        font.pixelSize: 14
                        font.bold: true
                        font.family: "Microsoft YaHei UI"
                        color: "#56666a"
                    }
                }
            }
        }

        GridLayout {
            id: contentGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: compactLayout ? 1 : 2
            columnSpacing: compactLayout ? 0 : 14
            rowSpacing: 12
            opacity: 0.0

            Behavior on opacity {
                NumberAnimation {
                    duration: 560
                    easing.type: Easing.OutCubic
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 220
                color: "#fffdfa"
                radius: 14
                border.color: "#d9ccba"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Text {
                        text: cameraMode ? "实时取景器" : "图片预览"
                        font.pixelSize: 16
                        font.bold: true
                        font.family: "Microsoft YaHei UI"
                        color: "#2f3f44"
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 10
                        border.color: "#d3d9dc"
                        border.width: 1
                        color: "#f3f6f7"

                        Item {
                            id: previewStage
                            anchors.fill: parent
                            anchors.margins: 8
                            clip: true

                            property real cameraSourceWidth: liveViewfinder.sourceRect.width
                            property real cameraSourceHeight: liveViewfinder.sourceRect.height
                            property real sourceWidth: cameraMode
                                                       ? cameraSourceWidth
                                                       : (previewImage.sourceSize.width > 0 ? previewImage.sourceSize.width : previewImage.implicitWidth)
                            property real sourceHeight: cameraMode
                                                        ? cameraSourceHeight
                                                        : (previewImage.sourceSize.height > 0 ? previewImage.sourceSize.height : previewImage.implicitHeight)
                            property real drawnWidth: {
                                if (sourceWidth <= 0 || sourceHeight <= 0 || width <= 0 || height <= 0) {
                                    return 0
                                }

                                var sourceAspect = sourceWidth / sourceHeight
                                var stageAspect = width / height
                                return stageAspect > sourceAspect ? (height * sourceAspect) : width
                            }
                            property real drawnHeight: {
                                if (sourceWidth <= 0 || sourceHeight <= 0 || width <= 0 || height <= 0) {
                                    return 0
                                }

                                var sourceAspect = sourceWidth / sourceHeight
                                var stageAspect = width / height
                                return stageAspect > sourceAspect ? height : (width / sourceAspect)
                            }
                            property real offsetX: Math.max(0, (width - drawnWidth) / 2)
                            property real offsetY: Math.max(0, (height - drawnHeight) / 2)
                            property real scaleX: sourceWidth > 0 ? drawnWidth / sourceWidth : 0
                            property real scaleY: sourceHeight > 0 ? drawnHeight / sourceHeight : 0

                            Image {
                                id: previewImage
                                anchors.fill: parent
                                source: selectedImageUrl
                                fillMode: Image.PreserveAspectFit
                                visible: !cameraMode && selectedImageUrl !== ""
                                smooth: true
                            }

                            VideoOutput {
                                id: liveViewfinder
                                anchors.fill: parent
                                visible: cameraMode && cameraAvailable
                                fillMode: VideoOutput.PreserveAspectFit
                            }

                            // ── 瞄准框遮罩 ──────────────────────────────────────
                            Item {
                                id: sniperOverlay
                                anchors.fill: parent
                                visible: cameraMode && cameraAvailable

                                // 扫描区边长 = 父容器宽度 × 60%，并保持正方形
                                readonly property real boxSize: Math.min(width, height) * 0.60
                                readonly property real boxX: (width  - boxSize) / 2
                                readonly property real boxY: (height - boxSize) / 2

                                // 上方遮罩
                                Rectangle {
                                    x: 0; y: 0
                                    width: parent.width
                                    height: parent.boxY
                                    color: "#000000"
                                    opacity: 0.50
                                }
                                // 左侧遮罩
                                Rectangle {
                                    x: 0; y: parent.boxY
                                    width: parent.boxX
                                    height: parent.boxSize
                                    color: "#000000"
                                    opacity: 0.50
                                }
                                // 右侧遮罩
                                Rectangle {
                                    x: parent.boxX + parent.boxSize
                                    y: parent.boxY
                                    width: parent.width  - (parent.boxX + parent.boxSize)
                                    height: parent.boxSize
                                    color: "#000000"
                                    opacity: 0.50
                                }
                                // 下方遮罩
                                Rectangle {
                                    x: 0
                                    y: parent.boxY + parent.boxSize
                                    width: parent.width
                                    height: parent.height - (parent.boxY + parent.boxSize)
                                    color: "#000000"
                                    opacity: 0.50
                                }

                                // 四角绿色直角边框（L 形，用两个 Rectangle 组合）
                                // ── 左上角 ──
                                Rectangle { x: sniperOverlay.boxX;      y: sniperOverlay.boxY;      width: 20; height: 4; color: "#00e676" }
                                Rectangle { x: sniperOverlay.boxX;      y: sniperOverlay.boxY;      width: 4;  height: 20; color: "#00e676" }
                                // ── 右上角 ──
                                Rectangle { x: sniperOverlay.boxX + sniperOverlay.boxSize - 20; y: sniperOverlay.boxY;      width: 20; height: 4; color: "#00e676" }
                                Rectangle { x: sniperOverlay.boxX + sniperOverlay.boxSize - 4;  y: sniperOverlay.boxY;      width: 4;  height: 20; color: "#00e676" }
                                // ── 左下角 ──
                                Rectangle { x: sniperOverlay.boxX;      y: sniperOverlay.boxY + sniperOverlay.boxSize - 4;  width: 20; height: 4; color: "#00e676" }
                                Rectangle { x: sniperOverlay.boxX;      y: sniperOverlay.boxY + sniperOverlay.boxSize - 20; width: 4;  height: 20; color: "#00e676" }
                                // ── 右下角 ──
                                Rectangle { x: sniperOverlay.boxX + sniperOverlay.boxSize - 20; y: sniperOverlay.boxY + sniperOverlay.boxSize - 4;  width: 20; height: 4; color: "#00e676" }
                                Rectangle { x: sniperOverlay.boxX + sniperOverlay.boxSize - 4;  y: sniperOverlay.boxY + sniperOverlay.boxSize - 20; width: 4;  height: 20; color: "#00e676" }
                            }
                            // ────────────────────────────────────────────────────

                            Connections {
                                target: liveViewfinder.videoSink
                                enabled: cameraMode && liveScanEnabled && cameraAvailable && !!liveViewfinder.videoSink

                                function onVideoFrameChanged(frame) {
                                    if (frameThrottle.running || liveResultCooldown) {
                                        return
                                    }

                                    if (frame === undefined || frame === null || !frame.valid) {
                                        return
                                    }

                                    frameThrottle.start()
                                    qrProcessor.processVideoFrame(frame)
                                }
                            }

                            Item {
                                anchors.fill: parent
                                visible: cameraMode && cameraAvailable
                                opacity: liveScanEnabled ? 1.0 : 0.48

                                property real scanBoxSize: Math.min(width * 0.72, height * 0.72)
                                property real scanProgress: 0

                                NumberAnimation on scanProgress {
                                    from: 0
                                    to: 1
                                    duration: 1800
                                    loops: Animation.Infinite
                                    running: cameraMode && cameraAvailable && liveScanEnabled
                                }

                                Rectangle {
                                    id: scanBox
                                    anchors.centerIn: parent
                                    width: parent.scanBoxSize
                                    height: parent.scanBoxSize
                                    radius: 18
                                    color: "transparent"
                                    border.color: liveScanEnabled ? "#f3c86b" : "#c1ced4"
                                    border.width: 2
                                }

                                Rectangle {
                                    width: Math.max(24, scanBox.width - 32)
                                    height: 3
                                    radius: 2
                                    x: scanBox.x + 16
                                    y: scanBox.y + 14 + ((scanBox.height - 28) * parent.scanProgress)
                                    visible: liveScanEnabled
                                    color: "#f6ca67"
                                    opacity: 0.9
                                }

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.margins: 12
                                    radius: 8
                                    color: liveScanEnabled ? "#0f766e" : "#6f7e84"
                                    border.color: liveScanEnabled ? "#65b7aa" : "#aebbc0"
                                    border.width: 1
                                    width: badgeLabel.implicitWidth + 18
                                    height: 30

                                    Text {
                                        id: badgeLabel
                                        anchors.centerIn: parent
                                        text: liveScanEnabled ? "实时扫码 12.5 FPS" : "扫码已暂停"
                                        font.pixelSize: 12
                                        font.bold: true
                                        font.family: "Microsoft YaHei UI"
                                        color: "#f7fbfb"
                                    }
                                }
                            }

                            Canvas {
                                id: quadOverlay
                                anchors.fill: parent
                                visible: detectedQuads.length > 0
                                         && previewStage.scaleX > 0
                                         && previewStage.scaleY > 0
                                         && (cameraMode ? cameraAvailable : previewImage.visible)

                                onVisibleChanged: requestPaint()
                                onWidthChanged: requestPaint()
                                onHeightChanged: requestPaint()

                                Connections {
                                    target: mainWindow

                                    function onDetectedQuadsChanged() {
                                        quadOverlay.requestPaint()
                                    }
                                }

                                Connections {
                                    target: previewStage

                                    function onOffsetXChanged() {
                                        quadOverlay.requestPaint()
                                    }

                                    function onOffsetYChanged() {
                                        quadOverlay.requestPaint()
                                    }

                                    function onScaleXChanged() {
                                        quadOverlay.requestPaint()
                                    }

                                    function onScaleYChanged() {
                                        quadOverlay.requestPaint()
                                    }
                                }

                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.clearRect(0, 0, width, height)

                                    if (!visible || detectedQuads.length === 0) {
                                        return
                                    }

                                    for (var index = 0; index < detectedQuads.length; ++index) {
                                        var quad = detectedQuads[index]
                                        var p1x = previewStage.offsetX + (quad.x1 * previewStage.scaleX)
                                        var p1y = previewStage.offsetY + (quad.y1 * previewStage.scaleY)
                                        var p2x = previewStage.offsetX + (quad.x2 * previewStage.scaleX)
                                        var p2y = previewStage.offsetY + (quad.y2 * previewStage.scaleY)
                                        var p3x = previewStage.offsetX + (quad.x3 * previewStage.scaleX)
                                        var p3y = previewStage.offsetY + (quad.y3 * previewStage.scaleY)
                                        var p4x = previewStage.offsetX + (quad.x4 * previewStage.scaleX)
                                        var p4y = previewStage.offsetY + (quad.y4 * previewStage.scaleY)
                                        var hue = (index * 53) % 360

                                        ctx.beginPath()
                                        ctx.moveTo(p1x, p1y)
                                        ctx.lineTo(p2x, p2y)
                                        ctx.lineTo(p3x, p3y)
                                        ctx.lineTo(p4x, p4y)
                                        ctx.closePath()

                                        ctx.fillStyle = "hsla(" + hue + ",82%,52%,0.12)"
                                        ctx.strokeStyle = "hsl(" + hue + ",82%,45%)"
                                        ctx.lineWidth = 2.5
                                        ctx.fill()
                                        ctx.stroke()
                                    }
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: (cameraMode && (!cameraAvailable || !liveScanEnabled))
                                     || (!cameraMode && selectedImageUrl === "")
                            text: cameraMode
                                  ? (cameraAvailable
                                     ? "实时扫码已暂停\n点击上方按钮重新启动"
                                     : "未检测到摄像头\n可切换到图片识别")
                                  : "暂无图片\n点击上方按钮选择二维码"
                            horizontalAlignment: Text.AlignHCenter
                            font.pixelSize: 14
                            font.family: "Microsoft YaHei UI"
                            color: "#76878d"
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: cameraMode
                              ? (cameraAvailable
                                 ? (liveScanEnabled
                                                ? "摄像头实时处理中，每秒大约分析 12 帧；拖入图片可切换到静态识别。"
                                    : "实时扫码已暂停，点击“启动扫码”恢复摄像头取帧。")
                                 : "当前没有可用摄像头，你仍然可以使用图片识别。")
                              : selectedImageText
                        font.pixelSize: 12
                        font.family: "Microsoft YaHei UI"
                        color: "#7b7f80"
                        wrapMode: cameraMode ? Text.Wrap : Text.NoWrap
                        elide: cameraMode ? Text.ElideNone : Text.ElideMiddle
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 220
                color: "#fffcf8"
                radius: 14
                border.color: "#dfd0bd"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: "识别结果"
                            font.pixelSize: 16
                            font.bold: true
                            font.family: "Microsoft YaHei UI"
                            color: "#2f3f44"
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Button {
                            id: copyButton
                            enabled: resultText.text.length > 0
                            Layout.preferredHeight: 34
                            Layout.preferredWidth: 96

                            background: Rectangle {
                                radius: 9
                                color: copyButton.enabled ? (copyButton.pressed ? "#195d55" : (copyButton.hovered ? "#1f7f73" : "#0f766e")) : "#a7b6bb"
                            }

                            contentItem: Text {
                                text: "复制结果"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                color: "#f2fbf9"
                                font.pixelSize: 13
                                font.bold: true
                                font.family: "Microsoft YaHei UI"
                            }

                            onClicked: resultText.copy()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "解码路径: " + pipelineInfoText
                        font.pixelSize: 12
                        font.family: "Microsoft YaHei UI"
                        color: "#7b7f80"
                        wrapMode: Text.Wrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 10
                        color: "#f9f7f3"
                        border.color: "#ddcfbf"
                        border.width: 1

                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 10
                            clip: true

                            TextArea {
                                id: resultText
                                readOnly: true
                                selectByMouse: true
                                wrapMode: Text.Wrap
                                font.pixelSize: 18
                                font.family: "Consolas"
                                color: "#1f2628"
                                placeholderText: cameraMode ? "实时扫码结果会显示在这里" : "识别结果会显示在这里"
                                background: null
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 10
                        color: "#f6f2eb"
                        border.color: "#d8cab7"
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    text: "实时诊断日志"
                                    font.pixelSize: 13
                                    font.bold: true
                                    font.family: "Microsoft YaHei UI"
                                    color: "#4a5a5f"
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                Button {
                                    id: clearLogButton
                                    Layout.preferredHeight: 28
                                    Layout.preferredWidth: 84

                                    background: Rectangle {
                                        radius: 8
                                        color: clearLogButton.pressed ? "#b45309" : (clearLogButton.hovered ? "#d97706" : "#c26700")
                                    }

                                    contentItem: Text {
                                        text: "清空日志"
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        color: "#fff7ea"
                                        font.pixelSize: 12
                                        font.bold: true
                                        font.family: "Microsoft YaHei UI"
                                    }

                                    onClicked: clearDiagnosticLog()
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 8
                                color: "#fefcf8"
                                border.color: "#ddcfbf"
                                border.width: 1

                                ListView {
                                    id: logListView
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    clip: true
                                    spacing: 4
                                    model: diagnosticLogModel

                                    delegate: Text {
                                        width: logListView.width
                                        text: model.line
                                        wrapMode: Text.Wrap
                                        font.pixelSize: 12
                                        font.family: "Consolas"
                                        color: "#2e3a3f"
                                    }

                                    ScrollBar.vertical: ScrollBar {}
                                }

                                Text {
                                    anchors.centerIn: parent
                                    visible: diagnosticLogModel.count === 0
                                    text: "暂无日志\n开始扫码后会显示方差、阈值、状态切换和抓拍信息"
                                    horizontalAlignment: Text.AlignHCenter
                                    font.pixelSize: 12
                                    font.family: "Microsoft YaHei UI"
                                    color: "#8b8f90"
                                }
                            }
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: cameraMode
                ? "实时模式通过摄像头取景并限频到每秒约 12 帧，同时仍支持 PNG / JPG / JPEG / BMP / GIF 图片识别。"
                  : "图片模式支持 PNG / JPG / JPEG / BMP / GIF；点击“返回实时扫码”可切回摄像头取景。"
            font.pixelSize: 12
            font.family: "Microsoft YaHei UI"
            color: "#7d7f80"
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Component.onCompleted: {
        startLiveScan()
    }

    SequentialAnimation {
        running: true

        NumberAnimation {
            target: heroCard
            property: "opacity"
            from: 0
            to: 1
            duration: 430
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: controlCard
            property: "opacity"
            from: 0
            to: 1
            duration: 420
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: contentGrid
            property: "opacity"
            from: 0
            to: 1
            duration: 460
            easing.type: Easing.OutCubic
        }
    }

    DropArea {
        id: dropZone
        anchors.fill: parent

        onEntered: (drag) => {
            drag.acceptProposedAction()
        }

        onDropped: (drop) => {
            if (drop.hasUrls && drop.urls.length > 0) {
                beginRecognition(drop.urls[0])
                drop.acceptProposedAction()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        z: 100
        visible: dropZone.containsDrag
        color: "#0f766e"
        opacity: 0.22

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.8, 420)
            height: 128
            radius: 14
            color: "#f3fffb"
            border.color: "#0f766e"
            border.width: 2

            Text {
                anchors.centerIn: parent
                text: "松开鼠标开始识别二维码图片"
                color: "#155e75"
                font.pixelSize: 22
                font.bold: true
                font.family: "Microsoft YaHei UI"
            }
        }
    }
}

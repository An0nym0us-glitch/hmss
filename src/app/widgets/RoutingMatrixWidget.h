// Interactive microphone x speaker routing matrix. Rows are real input endpoints,
// columns real output endpoints; each cell is one route. Click toggles a route,
// right-click sets the per-route level. Cells glow with the live route level.
#pragma once

#include "../MeterHub.h"
#include "core/AudioEngine.h"

#include <QWidget>

#include <vector>

class RoutingMatrixWidget : public QWidget {
    Q_OBJECT
public:
    explicit RoutingMatrixWidget(QWidget* parent = nullptr);

    void setSnapshot(const ab::EngineSnapshot& s, bool showDisconnected);
    void updateLevels(const MeterFrame& f);
    bool isEmpty() const { return rows_.empty() || cols_.empty(); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    void routeToggleRequested(QString inputId, QString outputId, bool enable);
    void routeGainRequested(QString inputId, QString outputId, float currentGain, QPoint globalPos);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    bool event(QEvent* e) override;

private:
    struct Row {
        std::string id;
        QString name, detail;
        bool present = false, enabled = false, running = false;
        int slot = -1;
        int colorIndex = 0;
        ab::DeviceInfo info;
    };
    struct Cell {
        bool routed = false;
        float gain = 1.0f;
        ab::AudioRoutingMatrix::FeedbackRisk risk = ab::AudioRoutingMatrix::FeedbackRisk::None;
    };
    QRectF cellRect(int r, int c) const;
    bool hitCell(const QPoint& p, int* r, int* c) const;
    Cell& cell(int r, int c) { return cells_[static_cast<std::size_t>(r * cols_.size() + c)]; }
    const Cell& cell(int r, int c) const { return cells_[static_cast<std::size_t>(r * cols_.size() + c)]; }

    std::vector<Row> rows_, cols_;
    std::vector<Cell> cells_;
    std::vector<float> levels_; // per cell, smoothed
    int hoverR_ = -1, hoverC_ = -1;
    bool monitoring_ = false;

    static constexpr int kHeaderW = 340;
    static constexpr int kHeaderH = 104;
    static constexpr int kCellW = 150;
    static constexpr int kCellH = 56;
};

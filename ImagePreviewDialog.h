#pragma once

#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>

class ImagePreviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImagePreviewDialog(const QImage& image, const QString& title, QWidget *parent = nullptr);

private slots:
    void onZoomIn();
    void onZoomOut();
    void onZoomFit();
    void onZoomOriginal();
    void onSliderChanged(int value);

private:
    void updateImage();

    QImage m_image;
    QLabel* m_imageLabel;
    QScrollArea* m_scrollArea;
    QSlider* m_zoomSlider;
    QLabel* m_zoomLabel;
    double m_scaleFactor = 1.0;
};

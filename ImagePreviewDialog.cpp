#include "ImagePreviewDialog.h"
#include <QWheelEvent>
#include <QScrollBar>

ImagePreviewDialog::ImagePreviewDialog(const QImage& image, const QString& title, QWidget *parent)
    : QDialog(parent), m_image(image)
{
    setWindowTitle(title);
    setMinimumSize(800, 600);
    resize(1024, 768);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setAlignment(Qt::AlignCenter);

    m_imageLabel = new QLabel();
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_scrollArea->setWidget(m_imageLabel);

    mainLayout->addWidget(m_scrollArea, 1);

    QWidget* controlBar = new QWidget();
    QHBoxLayout* controlLayout = new QHBoxLayout(controlBar);
    controlLayout->setContentsMargins(8, 4, 8, 4);

    QPushButton* btnZoomOut = new QPushButton("-");
    btnZoomOut->setFixedSize(30, 30);
    connect(btnZoomOut, &QPushButton::clicked, this, &ImagePreviewDialog::onZoomOut);
    controlLayout->addWidget(btnZoomOut);

    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(10, 500);
    m_zoomSlider->setValue(100);
    m_zoomSlider->setMinimumWidth(200);
    connect(m_zoomSlider, &QSlider::valueChanged, this, &ImagePreviewDialog::onSliderChanged);
    controlLayout->addWidget(m_zoomSlider);

    QPushButton* btnZoomIn = new QPushButton("+");
    btnZoomIn->setFixedSize(30, 30);
    connect(btnZoomIn, &QPushButton::clicked, this, &ImagePreviewDialog::onZoomIn);
    controlLayout->addWidget(btnZoomIn);

    m_zoomLabel = new QLabel("100%");
    m_zoomLabel->setMinimumWidth(50);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    controlLayout->addWidget(m_zoomLabel);

    QPushButton* btnFit = new QPushButton("适应窗口");
    connect(btnFit, &QPushButton::clicked, this, &ImagePreviewDialog::onZoomFit);
    controlLayout->addWidget(btnFit);

    QPushButton* btnOriginal = new QPushButton("原始大小");
    connect(btnOriginal, &QPushButton::clicked, this, &ImagePreviewDialog::onZoomOriginal);
    controlLayout->addWidget(btnOriginal);

    QPushButton* btnClose = new QPushButton("关闭");
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    controlLayout->addWidget(btnClose);

    mainLayout->addWidget(controlBar);

    QString styleSheet = R"(
        QDialog { background-color: #0B1120; }
        QLabel { color: #E0E0E0; background-color: #0A1018; }
        QScrollArea { border: none; background-color: #0A1018; }
        QPushButton { background: transparent; border: 1px solid #00E5FF; border-radius: 4px; color: #00E5FF; padding: 4px 12px; }
        QPushButton:hover { background: rgba(0,229,255,0.12); }
        QSlider::groove:horizontal { background: #1A2332; height: 6px; border-radius: 3px; }
        QSlider::handle:horizontal { background: #00E5FF; width: 16px; margin: -5px 0; border-radius: 8px; }
        QSlider::sub-page:horizontal { background: rgba(0,229,255,0.3); border-radius: 3px; }
    )";
    setStyleSheet(styleSheet);

    onZoomFit();
}

void ImagePreviewDialog::onZoomIn()
{
    int newValue = m_zoomSlider->value() + 10;
    m_zoomSlider->setValue(std::min(newValue, 500));
}

void ImagePreviewDialog::onZoomOut()
{
    int newValue = m_zoomSlider->value() - 10;
    m_zoomSlider->setValue(std::max(newValue, 10));
}

void ImagePreviewDialog::onZoomFit()
{
    QSize scrollSize = m_scrollArea->viewport()->size();
    QSize imageSize = m_image.size();

    double scaleX = static_cast<double>(scrollSize.width()) / imageSize.width();
    double scaleY = static_cast<double>(scrollSize.height()) / imageSize.height();
    double scale = std::min(scaleX, scaleY) * 0.95;

    int percent = static_cast<int>(scale * 100);
    percent = std::max(10, std::min(500, percent));
    m_zoomSlider->setValue(percent);
}

void ImagePreviewDialog::onZoomOriginal()
{
    m_zoomSlider->setValue(100);
}

void ImagePreviewDialog::onSliderChanged(int value)
{
    m_scaleFactor = value / 100.0;
    m_zoomLabel->setText(QString("%1%").arg(value));
    updateImage();
}

void ImagePreviewDialog::updateImage()
{
    QSize newSize(static_cast<int>(m_image.width() * m_scaleFactor),
                  static_cast<int>(m_image.height() * m_scaleFactor));

    QPixmap pixmap = QPixmap::fromImage(m_image).scaled(newSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(pixmap);
    m_imageLabel->resize(pixmap.size());
}

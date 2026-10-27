// Export the approved SVG artwork to PNG and Windows ICO without changing it.
// Build with Qt Core, Gui and Svg; run: render-icons assets/bitshell.svg assets/bitshell
#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSaveFile>
#include <QSvgRenderer>
#include <array>
#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 3) return 2;
    QSvgRenderer renderer(QString::fromLocal8Bit(argv[1]));
    if (!renderer.isValid()) return 3;
    const QString destination = QString::fromLocal8Bit(argv[2]);
    constexpr std::array<int, 9> sizes{16, 20, 24, 32, 40, 48, 64, 128, 256};
    std::array<QByteArray, sizes.size()> images;
    auto render = [&](int size) {
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        renderer.render(&painter);
        painter.end();
        return image;
    };
    for (size_t i = 0; i < sizes.size(); ++i) {
        QBuffer buffer(&images[i]);
        if (!buffer.open(QIODevice::WriteOnly) || !render(sizes[i]).save(&buffer, "PNG")) return 4;
    }
    QSaveFile file(destination + QStringLiteral(".ico"));
    if (!file.open(QIODevice::WriteOnly)) return 5;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + 16 * quint32(sizes.size());
    for (size_t i = 0; i < sizes.size(); ++i) {
        const auto dimension = quint8(sizes[i] == 256 ? 0 : sizes[i]);
        stream << dimension << dimension << quint8(0) << quint8(0)
               << quint16(1) << quint16(32) << quint32(images[i].size()) << offset;
        offset += quint32(images[i].size());
    }
    for (const auto& image : images) stream.writeRawData(image.constData(), image.size());
    if (stream.status() != QDataStream::Ok || !file.commit()) return 6;
    if (!render(512).save(destination + QStringLiteral(".png"))) return 7;
    std::puts("Exported 9 Windows icon sizes and a 512px PNG from the SVG source.");
    return 0;
}

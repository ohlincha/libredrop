#include "manualwindow.h"

#include <QImage>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace {

// A text browser whose images (screenshots) shrink to the window's width, never above their own size.
class ManualBrowser : public QTextBrowser {
    static constexpr int kOriginal = QTextFormat::UserProperty + 1;   // the image's own resource name

public:
    using QTextBrowser::QTextBrowser;
    // Each image is replaced by a smoothly scaled copy (kept as a document resource) when the window is narrower.
    void fitImages() {
        QTextDocument* doc = document();
        const int maxW = std::max(200, viewport()->width() - 2 * int(doc->documentMargin()) - 12);
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment f = it.fragment();
                if (!f.isValid() || !f.charFormat().isImageFormat()) continue;
                QTextImageFormat fmt = f.charFormat().toImageFormat();
                QString orig = fmt.stringProperty(kOriginal);
                if (orig.isEmpty()) orig = fmt.name();
                const QImage img = doc->resource(QTextDocument::ImageResource, QUrl(orig)).value<QImage>();
                if (img.isNull()) continue;
                const int w = std::min(img.width(), maxW);
                const QString name = w == img.width() ? orig : QStringLiteral("%1?w=%2").arg(orig).arg(w);
                if (name == fmt.name()) continue;
                if (w != img.width() && doc->resource(QTextDocument::ImageResource, QUrl(name)).isNull())
                    doc->addResource(QTextDocument::ImageResource, QUrl(name),
                                     img.scaledToWidth(w, Qt::SmoothTransformation));
                fmt.setProperty(kOriginal, orig);
                fmt.setName(name);
                fmt.setWidth(w);
                fmt.setHeight(double(w) * img.height() / img.width());
                QTextCursor c(doc);
                c.setPosition(f.position());
                c.setPosition(f.position() + f.length(), QTextCursor::KeepAnchor);
                c.setCharFormat(fmt);
            }
    }

protected:
    void resizeEvent(QResizeEvent* e) override {
        QTextBrowser::resizeEvent(e);
        fitImages();
    }
};

}  // namespace

ManualWindow::ManualWindow(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("LibreDrop manual"));
    setAttribute(Qt::WA_DeleteOnClose);
    auto* browser = new ManualBrowser;
    browser->setOpenExternalLinks(true);
    browser->document()->setDocumentMargin(16);
    browser->setSource(QUrl(QStringLiteral("qrc:/docs/MANUAL.md")), QTextDocument::MarkdownResource);
    browser->fitImages();
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(browser);
    resize(860, 900);
}

#include "MailListWidget.h"

#include "MailDeltaSyncService.h"
#include "MailIndexService.h"
#include "MicrosoftGraphClient.h"
#include "ApplicationSettings.h"
#include "WorkDescriptorService.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QBuffer>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPalette>
#include <QRegularExpression>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>

#include <utility>

#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>
#include <libxml/tree.h>

namespace productivity::ui {
namespace {

QString normalizedContentId(QString contentId)
{
    contentId = QUrl::fromPercentEncoding(contentId.toUtf8()).trimmed();
    if (contentId.startsWith(QStringLiteral("cid:"), Qt::CaseInsensitive))
        contentId.remove(0, 4);
    contentId.remove(QLatin1Char('<'));
    contentId.remove(QLatin1Char('>'));
    return contentId.trimmed().toLower();
}

QString safeInlineStyle(const QString& style)
{
    static const QSet<QString> allowedProperties{
        QStringLiteral("color"), QStringLiteral("background-color"),
        QStringLiteral("font-family"), QStringLiteral("font-size"),
        QStringLiteral("font-style"), QStringLiteral("font-weight"),
        QStringLiteral("text-align"), QStringLiteral("text-decoration"),
        QStringLiteral("vertical-align"), QStringLiteral("white-space"),
        QStringLiteral("border"), QStringLiteral("border-collapse"),
        QStringLiteral("border-color"), QStringLiteral("border-style"),
        QStringLiteral("border-width"), QStringLiteral("padding"),
        QStringLiteral("padding-left"), QStringLiteral("padding-right"),
        QStringLiteral("padding-top"), QStringLiteral("padding-bottom"),
        QStringLiteral("margin"), QStringLiteral("margin-left"),
        QStringLiteral("margin-right"), QStringLiteral("margin-top"),
        QStringLiteral("margin-bottom"), QStringLiteral("width"),
        QStringLiteral("height"), QStringLiteral("max-width"), QStringLiteral("min-width")
    };
    QStringList safeDeclarations;
    for (const QString& declaration : style.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const qsizetype colon = declaration.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString property = declaration.left(colon).trimmed().toLower();
        const QString value = declaration.mid(colon + 1).trimmed();
        if (!allowedProperties.contains(property) || value.isEmpty() || value.size() > 256 ||
            value.contains(QLatin1Char('<')) || value.contains(QLatin1Char('>')) ||
            value.contains(QLatin1Char('\\')) ||
            QRegularExpression(QStringLiteral("url\\s*\\(|expression|javascript|@import|behavior|binding"),
                               QRegularExpression::CaseInsensitiveOption).match(value).hasMatch())
            continue;
        safeDeclarations.append(property + QLatin1Char(':') + value);
    }
    return safeDeclarations.join(QLatin1Char(';'));
}

bool isAllowedEmailTag(const QString& tag)
{
    static const QSet<QString> allowed{
        QStringLiteral("html"), QStringLiteral("body"), QStringLiteral("div"), QStringLiteral("span"),
        QStringLiteral("p"), QStringLiteral("br"), QStringLiteral("hr"), QStringLiteral("b"),
        QStringLiteral("strong"), QStringLiteral("i"), QStringLiteral("em"), QStringLiteral("u"),
        QStringLiteral("s"), QStringLiteral("strike"), QStringLiteral("sub"), QStringLiteral("sup"),
        QStringLiteral("small"), QStringLiteral("big"), QStringLiteral("font"),
        QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"), QStringLiteral("h4"),
        QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("blockquote"),
        QStringLiteral("pre"), QStringLiteral("code"), QStringLiteral("ul"), QStringLiteral("ol"),
        QStringLiteral("li"), QStringLiteral("table"), QStringLiteral("thead"), QStringLiteral("tbody"),
        QStringLiteral("tfoot"), QStringLiteral("tr"), QStringLiteral("td"), QStringLiteral("th"),
        QStringLiteral("caption"), QStringLiteral("a"), QStringLiteral("img")
    };
    return allowed.contains(tag);
}

bool isDangerousEmailTag(const QString& tag)
{
    static const QSet<QString> dangerous{
        QStringLiteral("script"), QStringLiteral("style"), QStringLiteral("iframe"),
        QStringLiteral("frame"), QStringLiteral("frameset"), QStringLiteral("object"),
        QStringLiteral("embed"), QStringLiteral("applet"), QStringLiteral("form"),
        QStringLiteral("input"), QStringLiteral("button"), QStringLiteral("textarea"),
        QStringLiteral("select"), QStringLiteral("option"), QStringLiteral("video"),
        QStringLiteral("audio"), QStringLiteral("source"), QStringLiteral("link"),
        QStringLiteral("meta"), QStringLiteral("base"), QStringLiteral("svg"),
        QStringLiteral("math"), QStringLiteral("noscript")
    };
    return dangerous.contains(tag);
}

bool hasSafeLinkScheme(const QString& href)
{
    const QUrl url(href, QUrl::StrictMode);
    if (!url.isValid() || !url.userInfo().isEmpty())
        return false;
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("https") || scheme == QLatin1String("http"))
        return !url.host().isEmpty();
    return scheme == QLatin1String("mailto") && !url.path().isEmpty();
}

bool validInlineDimension(const QString& value)
{
    bool ok = false;
    const int dimension = value.toInt(&ok);
    return ok && dimension > 0 && dimension <= 4096;
}

void sanitizeEmailAttributes(xmlDocPtr document, xmlNodePtr node,
                             const QSet<QString>& allowedInlineContentIds)
{
    const QString tag = QString::fromUtf8(reinterpret_cast<const char*>(node->name)).toLower();
    for (xmlAttrPtr attribute = node->properties; attribute;) {
        xmlAttrPtr next = attribute->next;
        const QString name = QString::fromUtf8(reinterpret_cast<const char*>(attribute->name)).toLower();
        xmlChar* rawValue = xmlNodeListGetString(document, attribute->children, 1);
        const QString value = rawValue ? QString::fromUtf8(reinterpret_cast<const char*>(rawValue)) : QString();
        if (rawValue)
            xmlFree(rawValue);

        bool keep = false;
        if (name == QLatin1String("title") || name == QLatin1String("lang") || name == QLatin1String("dir") ||
            name == QLatin1String("alt")) {
            keep = value.size() <= 512;
        } else if (name == QLatin1String("style")) {
            const QString safeStyle = safeInlineStyle(value);
            if (!safeStyle.isEmpty())
                xmlSetProp(node, BAD_CAST "style", BAD_CAST safeStyle.toUtf8().constData());
            keep = !safeStyle.isEmpty();
        } else if (name == QLatin1String("href") && tag == QLatin1String("a")) {
            keep = hasSafeLinkScheme(value);
        } else if (name == QLatin1String("src") && tag == QLatin1String("img")) {
            const QString contentId = normalizedContentId(value);
            keep = value.startsWith(QStringLiteral("cid:"), Qt::CaseInsensitive) &&
                   allowedInlineContentIds.contains(contentId);
        } else if ((name == QLatin1String("width") || name == QLatin1String("height")) &&
                   tag == QLatin1String("img")) {
            keep = validInlineDimension(value);
        } else if ((name == QLatin1String("colspan") || name == QLatin1String("rowspan")) &&
                   (tag == QLatin1String("td") || tag == QLatin1String("th"))) {
            bool ok = false;
            const int count = value.toInt(&ok);
            keep = ok && count > 0 && count <= 100;
        } else if (name == QLatin1String("align")) {
            const QString alignment = value.toLower();
            keep = alignment == QLatin1String("left") || alignment == QLatin1String("right") ||
                   alignment == QLatin1String("center") || alignment == QLatin1String("justify");
        }
        if (!keep)
            xmlRemoveProp(attribute);
        attribute = next;
    }
}

void sanitizeEmailTree(xmlDocPtr document, xmlNodePtr parent,
                       const QSet<QString>& allowedInlineContentIds)
{
    for (xmlNodePtr node = parent->children; node;) {
        xmlNodePtr next = node->next;
        if (node->type == XML_ELEMENT_NODE) {
            const QString tag = QString::fromUtf8(reinterpret_cast<const char*>(node->name)).toLower();
            if (isDangerousEmailTag(tag)) {
                xmlUnlinkNode(node);
                xmlFreeNode(node);
            } else {
                sanitizeEmailAttributes(document, node, allowedInlineContentIds);
                sanitizeEmailTree(document, node, allowedInlineContentIds);
                if (!isAllowedEmailTag(tag)) {
                    while (node->children) {
                        xmlNodePtr child = node->children;
                        xmlUnlinkNode(child);
                        xmlAddPrevSibling(node, child);
                    }
                    xmlUnlinkNode(node);
                    xmlFreeNode(node);
                }
            }
        } else if (node->type != XML_TEXT_NODE && node->type != XML_CDATA_SECTION_NODE) {
            xmlUnlinkNode(node);
            xmlFreeNode(node);
        }
        node = next;
    }
}

QString sanitizeEmailHtml(const QString& html, const QSet<QString>& allowedInlineContentIds)
{
    if (html.isEmpty() || html.size() > 2 * 1024 * 1024)
        return {};
    const QByteArray utf8 = html.toUtf8();
    htmlDocPtr document = htmlReadMemory(utf8.constData(), utf8.size(), nullptr, "UTF-8",
        HTML_PARSE_NONET | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_RECOVER | HTML_PARSE_COMPACT);
    if (!document)
        return {};
    xmlNodePtr root = xmlDocGetRootElement(document);
    if (root)
        sanitizeEmailTree(document, root, allowedInlineContentIds);
    xmlBufferPtr buffer = xmlBufferCreate();
    if (!buffer) {
        xmlFreeDoc(document);
        return {};
    }
    if (root)
        htmlNodeDump(buffer, document, root);
    const QString safeHtml = QString::fromUtf8(reinterpret_cast<const char*>(buffer->content),
                                               static_cast<qsizetype>(buffer->use));
    xmlBufferFree(buffer);
    xmlFreeDoc(document);
    return safeHtml;
}

class SafeEmailBrowser final : public QTextBrowser
{
public:
    using QTextBrowser::QTextBrowser;

    void setInlineImages(QHash<QString, QByteArray> images)
    {
        inlineImages_ = std::move(images);
    }

protected:
    // Message markup is untrusted. Qt rich text has no JavaScript runtime; resource
    // access is deny-by-default, with only validated inline CID images handled here.
    QVariant loadResource(int type, const QUrl& name) override
    {
        if (type != QTextDocument::ImageResource || name.scheme() != QLatin1String("cid"))
            return {};
        const QString contentId = normalizedContentId(name.toString(QUrl::FullyDecoded));
        const auto imageData = inlineImages_.constFind(contentId);
        if (imageData == inlineImages_.cend() || imageData->isEmpty() || imageData->size() > 6 * 1024 * 1024)
            return {};

        QBuffer buffer;
        buffer.setData(*imageData);
        if (!buffer.open(QIODevice::ReadOnly))
            return {};
        QImageReader reader(&buffer);
        reader.setDecideFormatFromContent(true);
        const QSize imageSize = reader.size();
        if (!imageSize.isValid() || imageSize.width() > 4096 || imageSize.height() > 4096 ||
            qint64(imageSize.width()) * imageSize.height() > 12000000)
            return {};
        const QImage image = reader.read();
        return image.isNull() ? QVariant() : QVariant::fromValue(image);
    }

private:
    QHash<QString, QByteArray> inlineImages_;
};

} // namespace

MailListWidget::MailListWidget(services::MailIndexService& mailIndex,
                               services::MailDeltaSyncService& mailSync,
                               services::MicrosoftGraphClient& graphClient,
                               services::ApplicationSettings& settings,
                               services::WorkDescriptorService& workService,
                               bool unreadOnly,
                               QWidget* parent)
    : QWidget(parent)
    , mailIndex_(mailIndex)
    , graphClient_(graphClient)
    , settings_(settings)
    , workService_(workService)
    , unreadOnly_(unreadOnly)
{
    auto* layout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout;
    searchInput_ = new QLineEdit(this);
    searchInput_->setPlaceholderText(QStringLiteral("Search sender or subject"));
    searchInput_->setClearButtonEnabled(true);
    auto* refreshButton = new QPushButton(QStringLiteral("Refresh"), this);
    auto* syncButton = new QPushButton(QStringLiteral("Sync mail changes"), this);
    syncStatus_ = new QLabel(this);
    controls->addWidget(searchInput_, 1);
    controls->addWidget(refreshButton);
    controls->addWidget(syncButton);
    controls->addWidget(syncStatus_, 1);
    layout->addLayout(controls);

    messages_ = new QTableWidget(0, 3, this);
    messages_->setHorizontalHeaderLabels({QStringLiteral("Sender"), QStringLiteral("Subject"), QStringLiteral("Received")});
    messages_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    messages_->setSelectionBehavior(QAbstractItemView::SelectRows);
    messages_->setSelectionMode(QAbstractItemView::SingleSelection);
    messages_->verticalHeader()->hide();
    messages_->horizontalHeader()->setStretchLastSection(false);
    messages_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    messages_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    messages_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    messages_->setColumnWidth(0, 260);
    messages_->setColumnWidth(2, 155);
    layout->addWidget(messages_);

    emptyState_ = new QLabel(unreadOnly_ ? QStringLiteral("No unread mail.")
                                         : QStringLiteral("No mail indexed yet."), this);
    emptyState_->setAlignment(Qt::AlignCenter);
    layout->addWidget(emptyState_);
    emptyState_->hide();

    connect(refreshButton, &QPushButton::clicked, this, &MailListWidget::refreshMessages);
    connect(searchInput_, &QLineEdit::textChanged, this, &MailListWidget::refreshMessages);
    connect(messages_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const auto* item = messages_->item(row, 0);
        if (!item)
            return;
        auto* dialog = new QDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(QStringLiteral("Open message"));
        dialog->setProperty("mailRow", row);
        dialog->resize(680, 480);
        auto* dialogLayout = new QVBoxLayout(dialog);
        auto* loading = new QLabel(QStringLiteral("Loading message…"), dialog);
        dialogLayout->addWidget(loading);
        const quint64 requestId = graphClient_.getMessage(item->data(Qt::UserRole).toString());
        messageDialogs_.insert(requestId, dialog);
        connect(dialog, &QObject::destroyed, this, [this, requestId] { messageDialogs_.remove(requestId); });
        dialog->show();
    });
    connect(&graphClient_, &services::MicrosoftGraphClient::requestFinished, this,
            [this](quint64 requestId, int status, const QByteArray& response) {
        QDialog* dialog = messageDialogs_.take(requestId);
        if (!dialog)
            return;
        if (status < 200 || status >= 300) {
            auto* label = dialog->findChild<QLabel*>();
            if (label)
                label->setText(QStringLiteral("Microsoft Graph could not load this message (HTTP %1).").arg(status));
            return;
        }
        const QJsonDocument messageDocument = QJsonDocument::fromJson(response);
        if (!messageDocument.isObject()) {
            auto* label = dialog->findChild<QLabel*>();
            if (label)
                label->setText(QStringLiteral("Microsoft returned an unreadable message. Close this window and retry."));
            return;
        }
        const QJsonObject message = messageDocument.object();
        const QJsonObject sender = message.value(QStringLiteral("from")).toObject()
                                       .value(QStringLiteral("emailAddress")).toObject();
        const QJsonObject body = message.value(QStringLiteral("body")).toObject();
        const QString bodyType = body.value(QStringLiteral("contentType")).toString().toLower();
        const bool isHtml = bodyType == QLatin1String("html");
        const bool isPlainText = bodyType == QLatin1String("text");
        const QString bodyContent = body.value(QStringLiteral("content")).toString();
        const QString bodyPreview = message.value(QStringLiteral("bodyPreview")).toString();
        auto* layout = qobject_cast<QVBoxLayout*>(dialog->layout());
        if (!layout)
            return;
        delete layout->takeAt(0)->widget();
        auto* details = new QLabel(QStringLiteral("<b>%1</b><br/>From: %2 &lt;%3&gt;<br/>Received: %4<br/>Status: %5<br/>Importance: %6<br/>Folder ID: %7")
            .arg(message.value(QStringLiteral("subject")).toString().toHtmlEscaped(),
                 sender.value(QStringLiteral("name")).toString().toHtmlEscaped(),
                 sender.value(QStringLiteral("address")).toString().toHtmlEscaped(),
                 message.value(QStringLiteral("receivedDateTime")).toString().toHtmlEscaped(),
                 message.value(QStringLiteral("isRead")).toBool() ? QStringLiteral("Read") : QStringLiteral("Unread"),
                 message.value(QStringLiteral("importance")).toString(QStringLiteral("Unknown")).toHtmlEscaped(),
                 message.value(QStringLiteral("parentFolderId")).toString(QStringLiteral("Unknown")).toHtmlEscaped()), dialog);
        details->setTextFormat(Qt::RichText);
        details->setWordWrap(true);
        layout->addWidget(details);
        if (isHtml && !bodyContent.isEmpty()) {
            QHash<QString, QByteArray> inlineImages;
            const QJsonArray attachments = message.value(QStringLiteral("attachments")).toArray();
            for (const QJsonValue& attachmentValue : attachments) {
                const QJsonObject attachment = attachmentValue.toObject();
                const QString contentType = attachment.value(QStringLiteral("contentType")).toString().toLower();
                const bool supportedImage = contentType == QLatin1String("image/png") ||
                    contentType == QLatin1String("image/jpeg") || contentType == QLatin1String("image/gif") ||
                    contentType == QLatin1String("image/bmp") || contentType == QLatin1String("image/webp");
                const QString contentId = normalizedContentId(attachment.value(QStringLiteral("contentId")).toString());
                const QString encodedBytes = attachment.value(QStringLiteral("contentBytes")).toString();
                if (!attachment.value(QStringLiteral("isInline")).toBool() || !supportedImage ||
                    contentId.isEmpty() || encodedBytes.isEmpty() || encodedBytes.size() > 8 * 1024 * 1024)
                    continue;
                const QByteArray bytes = QByteArray::fromBase64(encodedBytes.toLatin1());
                if (!bytes.isEmpty() && bytes.size() <= 6 * 1024 * 1024)
                    inlineImages.insert(contentId, bytes);
            }
            QSet<QString> inlineContentIds;
            for (auto it = inlineImages.cbegin(); it != inlineImages.cend(); ++it)
                inlineContentIds.insert(it.key());
            const QString safeHtml = sanitizeEmailHtml(bodyContent, inlineContentIds);

            auto* note = new QLabel(safeHtml.isEmpty()
                ? QStringLiteral("Email markup could not be safely rendered; showing a plain-text preview.")
                : QStringLiteral("Only supported inline images are shown. Remote images, unsupported image formats, and other resources are blocked. Links ask before opening."), dialog);
            note->setWordWrap(true);
            layout->addWidget(note);
            auto* content = new SafeEmailBrowser(dialog);
            content->setOpenLinks(false);
            content->setOpenExternalLinks(false);
            connect(content, &QTextBrowser::anchorClicked, dialog, [this, dialog](const QUrl& url) {
                if (!hasSafeLinkScheme(url.toString(QUrl::FullyEncoded)))
                    return;
                const QString domain = url.host().toLower();
                if (!domain.isEmpty() && settings_.isTrustedLinkDomain(domain)) {
                    const QString workId = workService_.startTask(QStringLiteral("Open trusted email link"));
                    if (QDesktopServices::openUrl(url))
                        workService_.completeTask(workId, QStringLiteral("External link opened"));
                    else
                        workService_.failTask(workId, QStringLiteral("Could not open the external link"));
                    return;
                }

                QMessageBox prompt(QMessageBox::Warning, QStringLiteral("Trust this email link?"),
                    domain.isEmpty()
                        ? QStringLiteral("This email wants to open an external handler for:\n%1").arg(url.toDisplayString())
                        : QStringLiteral("This email links to %1:\n%2\nOnly open it if you trust this domain.")
                              .arg(domain, url.toDisplayString()),
                    QMessageBox::NoButton, dialog);
                prompt.setTextFormat(Qt::PlainText);
                QPushButton* openOnce = prompt.addButton(QStringLiteral("Open once"), QMessageBox::AcceptRole);
                QPushButton* alwaysTrust = nullptr;
                if (!domain.isEmpty())
                    alwaysTrust = prompt.addButton(QStringLiteral("Always trust %1").arg(domain), QMessageBox::AcceptRole);
                prompt.addButton(QMessageBox::Cancel);
                prompt.setDefaultButton(qobject_cast<QPushButton*>(prompt.button(QMessageBox::Cancel)));
                prompt.exec();
                if (prompt.clickedButton() == alwaysTrust) {
                    const QString workId = workService_.startTask(QStringLiteral("Trust email link domain"));
                    if (!settings_.trustLinkDomain(domain)) {
                        workService_.failTask(workId, QStringLiteral("Could not save the trusted domain"));
                        QMessageBox::warning(dialog, QStringLiteral("Domain not saved"),
                            QStringLiteral("Could not save this trusted domain. The link was not opened."));
                        return;
                    }
                    workService_.completeTask(workId, QStringLiteral("Trusted domain saved"));
                } else if (prompt.clickedButton() != openOnce) {
                    return;
                }
                const QString workId = workService_.startTask(QStringLiteral("Open email link externally"));
                if (QDesktopServices::openUrl(url))
                    workService_.completeTask(workId, QStringLiteral("External link opened"));
                else
                    workService_.failTask(workId, QStringLiteral("Could not open the external link"));
            });
            content->setAutoFillBackground(true);
            QPalette emailPalette = content->palette();
            emailPalette.setColor(QPalette::Base, Qt::white);
            emailPalette.setColor(QPalette::Text, Qt::black);
            content->setPalette(emailPalette);
            content->document()->setDefaultStyleSheet(
                QStringLiteral("body { background-color: #ffffff; color: #000000; }"));
            content->setInlineImages(std::move(inlineImages));
            if (safeHtml.isEmpty())
                content->setPlainText(bodyPreview.isEmpty()
                    ? QStringLiteral("No readable text preview is available.") : bodyPreview);
            else
                content->setHtml(safeHtml);
            layout->addWidget(content, 1);
        } else {
            const QString fallbackText = bodyPreview.isEmpty()
                ? QStringLiteral("No readable body content is available.") : bodyPreview;
            if (!isPlainText && !bodyPreview.isEmpty()) {
                const QString noteText = isHtml
                    ? QStringLiteral("The HTML body is unavailable; showing the plain-text preview.")
                    : QStringLiteral("This body format is not supported; showing the plain-text preview.");
                auto* note = new QLabel(noteText, dialog);
                note->setWordWrap(true);
                layout->addWidget(note);
            }
            auto* content = new QPlainTextEdit(isPlainText && !bodyContent.isEmpty()
                ? bodyContent : fallbackText, dialog);
            content->setReadOnly(true);
            layout->addWidget(content, 1);
        }
        auto* navigation = new QHBoxLayout;
        auto* newerButton = new QPushButton(QStringLiteral("Newer"), dialog);
        auto* olderButton = new QPushButton(QStringLiteral("Older"), dialog);
        const int row = dialog->property("mailRow").toInt();
        newerButton->setEnabled(row > 0);
        olderButton->setEnabled(row + 1 < messages_->rowCount());
        navigation->addWidget(newerButton);
        navigation->addWidget(olderButton);
        navigation->addStretch();
        layout->addLayout(navigation);
        connect(newerButton, &QPushButton::clicked, dialog, [this, dialog, row] {
            dialog->close();
            emit messages_->cellDoubleClicked(row - 1, 0);
        });
        connect(olderButton, &QPushButton::clicked, dialog, [this, dialog, row] {
            dialog->close();
            emit messages_->cellDoubleClicked(row + 1, 0);
        });
        dialog->setWindowTitle(message.value(QStringLiteral("subject")).toString());
    });
    connect(&graphClient_, &services::MicrosoftGraphClient::requestFailed, this,
            [this](quint64 requestId, const QString& error) {
        QDialog* dialog = messageDialogs_.take(requestId);
        if (!dialog)
            return;
        auto* label = dialog->findChild<QLabel*>();
        if (label)
            label->setText(error);
    });
    connect(syncButton, &QPushButton::clicked, &mailSync, &services::MailDeltaSyncService::start);
    connect(&mailSync, &services::MailDeltaSyncService::progressChanged, this,
            [this](const QString& status) { syncStatus_->setText(status); });
    connect(&mailSync, &services::MailDeltaSyncService::finished, this,
            [this](int changed, int removed, int untranslatableIds) {
        QString status = QStringLiteral("Mail sync complete: %1 updated, %2 removed.").arg(changed).arg(removed);
        if (untranslatableIds > 0)
            status += QStringLiteral(" %1 stale local ID(s) could not be translated and were removed.").arg(untranslatableIds);
        syncStatus_->setText(status);
        refreshMessages();
    });
    connect(&mailSync, &services::MailDeltaSyncService::failed, this,
            [this](const QString& error) { syncStatus_->setText(error); });

    if (unreadOnly_) {
        refreshTimer_ = new QTimer(this);
        refreshTimer_->setInterval(5000);
        connect(refreshTimer_, &QTimer::timeout, this, &MailListWidget::refreshMessages);
        refreshTimer_->start();
    }
    refreshMessages();
}

void MailListWidget::openMessageById(const QString& messageId)
{
    searchInput_->clear();
    refreshMessages();
    for (int row = 0; row < messages_->rowCount(); ++row) {
        const auto* item = messages_->item(row, 0);
        if (item && item->data(Qt::UserRole).toString() == messageId) {
            emit messages_->cellDoubleClicked(row, 0);
            return;
        }
    }
    syncStatus_->setText(QStringLiteral("This linked email is no longer in the local mail index."));
}

void MailListWidget::refreshMessages()
{
    const QVector<services::IndexedMailSummary> indexedMail = unreadOnly_ ? mailIndex_.unreadMessages()
                                                                          : mailIndex_.allMessages();
    QVector<services::IndexedMailSummary> mail;
    const QString search = searchInput_->text().trimmed();
    mail.reserve(indexedMail.size());
    for (const auto& message : indexedMail) {
        const QString sender = message.senderName + QLatin1Char(' ') + message.senderAddress;
        if (search.isEmpty() || sender.contains(search, Qt::CaseInsensitive) ||
            message.subject.contains(search, Qt::CaseInsensitive)) {
            mail.append(message);
        }
    }
    messages_->setUpdatesEnabled(false);
    messages_->clearContents();
    messages_->setRowCount(mail.size());
    for (int row = 0; row < mail.size(); ++row) {
        const auto& message = mail.at(row);
        const QString sender = message.senderName.isEmpty()
            ? message.senderAddress
            : message.senderName + (message.senderAddress.isEmpty()
                                      ? QString() : QStringLiteral(" <%1>").arg(message.senderAddress));
        const QDateTime received = QDateTime::fromString(message.receivedAtUtc, Qt::ISODateWithMs);
        const QString receivedText = received.isValid()
            ? received.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString();
        auto* senderItem = new QTableWidgetItem(sender);
        senderItem->setData(Qt::UserRole, message.messageId);
        messages_->setItem(row, 0, senderItem);
        messages_->setItem(row, 1, new QTableWidgetItem(message.subject));
        messages_->setItem(row, 2, new QTableWidgetItem(receivedText));
    }
    const bool empty = mail.isEmpty();
    if (empty) {
        emptyState_->setText(indexedMail.isEmpty()
            ? (unreadOnly_ ? QStringLiteral("No unread mail.") : QStringLiteral("No mail indexed yet."))
            : QStringLiteral("No messages match your search."));
    }
    messages_->setVisible(!empty);
    emptyState_->setVisible(empty);
    messages_->setUpdatesEnabled(true);
}

} // namespace productivity::ui

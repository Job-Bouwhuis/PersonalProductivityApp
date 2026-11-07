#include "MailListWidget.h"

#include "MailDeltaSyncService.h"
#include "MailIndexService.h"
#include "MicrosoftGraphClient.h"
#include "ApplicationSettings.h"
#include "SenderRuleMatcher.h"
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

#include <algorithm>
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
    auto* refreshButton = new QPushButton(QStringLiteral("Refresh from Outlook"), this);
    if (unreadOnly_) {
        markAllReadButton_ = new QPushButton(QStringLiteral("Mark all as read"), this);
        controls->addWidget(markAllReadButton_);
        connect(markAllReadButton_, &QPushButton::clicked, this, &MailListWidget::startMarkAllUnreadAsRead);
    }
    syncStatus_ = new QLabel(this);
    controls->addWidget(searchInput_, 1);
    controls->addWidget(refreshButton);
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

    connect(refreshButton, &QPushButton::clicked, this, [this, &mailSync] {
        refreshMessages();
        mailSync.start();
    });
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
        dialog->setProperty("messageId", item->data(Qt::UserRole).toString());
        const quint64 requestId = graphClient_.getMessage(item->data(Qt::UserRole).toString());
        messageDialogs_.insert(requestId, dialog);
        connect(dialog, &QObject::destroyed, this, [this, requestId] { messageDialogs_.remove(requestId); });
        dialog->show();
    });
    connect(&graphClient_, &services::MicrosoftGraphClient::requestFinished, this,
            [this](quint64 requestId, int status, const QByteArray& response) {
        if (requestId == markReadRequestId_) {
            finishMarkReadRequest(status >= 200 && status < 300,
                                  status >= 200 && status < 300
                                      ? QString()
                                      : QStringLiteral("Microsoft Graph returned HTTP %1.").arg(status));
            return;
        }
        if (viewerMarkReadMessages_.contains(requestId)) {
            const QString id = viewerMarkReadMessages_.take(requestId);
            const QPointer<QPushButton> button = viewerMarkReadButtons_.take(requestId);
            const bool success = status >= 200 && status < 300;
            QString error;
            if (success && !mailIndex_.setMessageReadState(id, true, error)) {
                workService_.failTask(button ? button->property("workId").toString() : QString(), error);
            } else if (success) {
                workService_.completeTask(button ? button->property("workId").toString() : QString(),
                                          QStringLiteral("Email marked as read in Outlook"));
                if (button) {
                    button->setEnabled(false);
                    button->setText(QStringLiteral("Marked as read"));
                }
                if (auto* details = button ? button->parentWidget()->findChild<QLabel*>() : nullptr) {
                    QString text = details->text();
                    text.replace(QStringLiteral("Status: Unread"), QStringLiteral("Status: Read"));
                    details->setText(text);
                }
                refreshMessages();
            } else {
                const QString detail = QStringLiteral("Could not mark email as read (HTTP %1). ").arg(status);
                workService_.failTask(button ? button->property("workId").toString() : QString(), detail);
                syncStatus_->setText(detail);
            }
            return;
        }
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
        const QString senderAddress = sender.value(QStringLiteral("address")).toString().trimmed();
        const QString senderName = sender.value(QStringLiteral("name")).toString().trimmed();
        const QStringList ignoredSenderRules = settings_.ignoredSenderRules();
        const bool senderIsIgnored = std::any_of(ignoredSenderRules.cbegin(), ignoredSenderRules.cend(),
            [&senderAddress, &senderName](const QString& rule) {
                return services::senderRuleMatches(senderAddress, senderName, rule);
            });
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
        const QString messageId = dialog->property("messageId").toString();
        auto* ignoredButton = new QPushButton(QStringLiteral("Ignore sender"), dialog);
        auto* importantButton = new QPushButton(QStringLiteral("Important"), dialog);
        ignoredButton->setCheckable(true);
        importantButton->setCheckable(true);
        const auto* rowItem = messages_->item(dialog->property("mailRow").toInt(), 1);
        ignoredButton->setChecked(senderIsIgnored || (rowItem && rowItem->data(Qt::UserRole + 1).toBool()));
        importantButton->setChecked(rowItem && rowItem->data(Qt::UserRole + 2).toBool());
        auto* flagControls = new QHBoxLayout;
        flagControls->addWidget(ignoredButton);
        flagControls->addWidget(importantButton);
        if (!message.value(QStringLiteral("isRead")).toBool()) {
            auto* markReadButton = new QPushButton(QStringLiteral("Mark as read"), dialog);
            flagControls->addWidget(markReadButton);
            connect(markReadButton, &QPushButton::clicked, this,
                    [this, markReadButton, messageId] {
                if (!markReadButton->isEnabled())
                    return;
                markReadButton->setEnabled(false);
                const QString workId = workService_.startTask(QStringLiteral("Mark email as read"));
                markReadButton->setProperty("workId", workId);
                const quint64 request = graphClient_.setMessageRead(messageId, true, workId);
                viewerMarkReadMessages_.insert(request, messageId);
                viewerMarkReadButtons_.insert(request, markReadButton);
            });
        }
        flagControls->addStretch();
        layout->addLayout(flagControls);
        const auto saveFlags = [this, dialog, messageId, senderAddress, senderName, ignoredButton, importantButton]
                               (bool updateSenderRule) {
            const QString taskId = workService_.startTask(QStringLiteral("Update email flags"));
            QString error;
            bool senderRuleChanged = false;
            if (updateSenderRule && !senderAddress.isEmpty()) {
                const QStringList originalIgnoredSenders = settings_.ignoredSenderRules();
                QStringList ignoredSenders = originalIgnoredSenders;
                const auto exactRule = std::find_if(ignoredSenders.begin(), ignoredSenders.end(),
                    [&senderAddress](const QString& rule) { return rule.compare(senderAddress, Qt::CaseInsensitive) == 0; });
                const bool exactRuleExists = exactRule != ignoredSenders.end();
                const bool anyRuleMatches = std::any_of(ignoredSenders.cbegin(), ignoredSenders.cend(),
                    [&senderAddress, &senderName](const QString& rule) {
                        return services::senderRuleMatches(senderAddress, senderName, rule);
                    });
                const bool shouldIgnoreSender = ignoredButton->isChecked();
                bool changeRule = false;
                if (shouldIgnoreSender && !anyRuleMatches) {
                    ignoredSenders.append(senderAddress);
                    changeRule = true;
                } else if (!shouldIgnoreSender && exactRuleExists) {
                    ignoredSenders.erase(exactRule);
                    const bool stillMatched = std::any_of(ignoredSenders.cbegin(), ignoredSenders.cend(),
                        [&senderAddress, &senderName](const QString& rule) {
                            return services::senderRuleMatches(senderAddress, senderName, rule);
                        });
                    if (stillMatched) {
                        ignoredButton->setChecked(true);
                        const QString detail = QStringLiteral("This sender still matches a wildcard rule. Remove or edit that rule in Outlook settings.");
                        workService_.failTask(taskId, detail);
                        syncStatus_->setText(detail);
                        return;
                    }
                    changeRule = true;
                } else if (!shouldIgnoreSender && anyRuleMatches) {
                    ignoredButton->setChecked(true);
                    const QString detail = QStringLiteral("This sender matches a wildcard rule. Remove or edit that rule in Outlook settings.");
                    workService_.failTask(taskId, detail);
                    syncStatus_->setText(detail);
                    return;
                }
                if (changeRule) {
                    if (!mailIndex_.requestAllFolderRescans(error)) {
                        ignoredButton->setChecked(!shouldIgnoreSender);
                        workService_.failTask(taskId, error);
                        QMessageBox::warning(dialog, QStringLiteral("Ignore rule not saved"), error);
                        return;
                    }
                    settings_.setIgnoredSenderRules(ignoredSenders);
                    if (!settings_.sync()) {
                        settings_.setIgnoredSenderRules(originalIgnoredSenders);
                        settings_.sync();
                        ignoredButton->setChecked(!shouldIgnoreSender);
                        workService_.failTask(taskId, QStringLiteral("Could not save the ignored sender rule"));
                        QMessageBox::warning(dialog, QStringLiteral("Ignore rule not saved"),
                            QStringLiteral("Could not save the sender rule. Try again from Outlook settings."));
                        return;
                    }
                    senderRuleChanged = true;
                    syncStatus_->setText(QStringLiteral("Sender rule queued for the next automatic scan."));
                    emit ignoredSenderRulesChanged();
                }
            }
            if (!mailIndex_.setMessageFlags(messageId, ignoredButton->isChecked(), importantButton->isChecked(), error)) {
                workService_.failTask(taskId, error);
                QMessageBox::warning(dialog, QStringLiteral("Email flags not saved"), error);
                return;
            }
            workService_.completeTask(taskId, senderRuleChanged
                ? QStringLiteral("Email flags saved; sender rule queued for the next automatic scan")
                : QStringLiteral("Email flags saved locally"));
            refreshMessages();
        };
        connect(ignoredButton, &QPushButton::clicked, dialog, [saveFlags] { saveFlags(true); });
        connect(importantButton, &QPushButton::clicked, dialog, [saveFlags] { saveFlags(false); });
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
        if (requestId == markReadRequestId_) {
            finishMarkReadRequest(false, error);
            return;
        }
        if (viewerMarkReadMessages_.contains(requestId)) {
            const QString id = viewerMarkReadMessages_.take(requestId);
            Q_UNUSED(id);
            const QPointer<QPushButton> button = viewerMarkReadButtons_.take(requestId);
            if (button) {
                button->setEnabled(true);
                workService_.failTask(button->property("workId").toString(), error);
            }
            syncStatus_->setText(error);
            return;
        }
        QDialog* dialog = messageDialogs_.take(requestId);
        if (!dialog)
            return;
        auto* label = dialog->findChild<QLabel*>();
        if (label)
            label->setText(error);
    });
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
        auto* subjectItem = new QTableWidgetItem(
            (message.important ? QStringLiteral("★ ") : QString()) +
            (message.ignored ? QStringLiteral("[Ignored] ") : QString()) + message.subject);
        subjectItem->setData(Qt::UserRole + 1, message.ignored);
        subjectItem->setData(Qt::UserRole + 2, message.important);
        messages_->setItem(row, 0, senderItem);
        messages_->setItem(row, 1, subjectItem);
        messages_->setItem(row, 2, new QTableWidgetItem(receivedText));
    }
    const bool empty = mail.isEmpty();
    if (markAllReadButton_)
        markAllReadButton_->setEnabled(!indexedMail.isEmpty() && markReadRequestId_ == 0);
    if (empty) {
        emptyState_->setText(indexedMail.isEmpty()
            ? (unreadOnly_ ? QStringLiteral("No unread mail.") : QStringLiteral("No mail indexed yet."))
            : QStringLiteral("No messages match your search."));
    }
    messages_->setVisible(!empty);
    emptyState_->setVisible(empty);
    messages_->setUpdatesEnabled(true);
}

void MailListWidget::startMarkAllUnreadAsRead()
{
    if (!unreadOnly_ || markReadRequestId_ != 0)
        return;
    const auto unread = mailIndex_.unreadMessages();
    if (unread.isEmpty()) {
        refreshMessages();
        return;
    }

    pendingMarkReadIds_.clear();
    for (const auto& message : unread)
        pendingMarkReadIds_.enqueue(message.messageId);
    markReadTotal_ = pendingMarkReadIds_.size();
    markReadSucceeded_ = 0;
    markReadFailed_ = 0;
    markReadWorkTaskId_ = workService_.startTask(QStringLiteral("Mark all unread mail as read"));
    markAllReadButton_->setEnabled(false);
    syncStatus_->setText(QStringLiteral("Marking %1 unread message(s) as read…").arg(markReadTotal_));
    sendNextMarkReadRequest();
}

void MailListWidget::sendNextMarkReadRequest()
{
    if (pendingMarkReadIds_.isEmpty()) {
        const QString detail = QStringLiteral("Marked %1 message(s) as read; %2 failed.")
                                   .arg(markReadSucceeded_).arg(markReadFailed_);
        if (markReadFailed_ == 0)
            workService_.completeTask(markReadWorkTaskId_, detail);
        else
            workService_.failTask(markReadWorkTaskId_, detail);
        syncStatus_->setText(detail);
        markReadWorkTaskId_.clear();
        currentMarkReadMessageId_.clear();
        refreshMessages();
        return;
    }

    currentMarkReadMessageId_ = pendingMarkReadIds_.dequeue();
    markReadRequestId_ = graphClient_.setMessageRead(currentMarkReadMessageId_, true, markReadWorkTaskId_);
    const int completed = markReadSucceeded_ + markReadFailed_;
    workService_.updateTask(markReadWorkTaskId_,
        QStringLiteral("Marked %1 of %2 unread messages as read…").arg(completed).arg(markReadTotal_),
        markReadTotal_ > 0 ? std::optional<int>((completed * 100) / markReadTotal_) : std::nullopt);
}

void MailListWidget::finishMarkReadRequest(bool success, const QString& error)
{
    if (markReadRequestId_ == 0)
        return;
    markReadRequestId_ = 0;
    if (success) {
        QString databaseError;
        if (mailIndex_.setMessageReadState(currentMarkReadMessageId_, true, databaseError)) {
            ++markReadSucceeded_;
        } else {
            ++markReadFailed_;
            qWarning("Could not update local mail state after remote mark-read success");
        }
    } else {
        ++markReadFailed_;
        if (!error.isEmpty())
            syncStatus_->setText(error);
    }
    sendNextMarkReadRequest();
}

} // namespace productivity::ui

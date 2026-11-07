#include "SenderRuleMatcher.h"

#include <QRegularExpression>

namespace productivity::services {
namespace {

constexpr auto nameSeparator = "||";

QString ruleName(const QString& rule)
{
    const qsizetype separator = rule.indexOf(QLatin1String(nameSeparator));
    return separator < 0 ? QString() : rule.left(separator).trimmed();
}

QString ruleAddress(const QString& rule)
{
    const qsizetype separator = rule.indexOf(QLatin1String(nameSeparator));
    return (separator < 0 ? rule : rule.mid(separator + 2)).trimmed();
}

QString addressPattern(const QString& rule)
{
    QString expression = QRegularExpression::escape(rule.trimmed());
    expression.replace(QStringLiteral("\\*"), QStringLiteral(".*"));
    return QStringLiteral("^(?:%1)$").arg(expression);
}

bool hasSingleAtSign(const QString& value)
{
    const qsizetype at = value.indexOf(QLatin1Char('@'));
    return at > 0 && at == value.lastIndexOf(QLatin1Char('@')) && at < value.size() - 1;
}

} // namespace

bool isValidSenderRule(const QString& rule)
{
    const QString trimmed = rule.trimmed();
    const QString addressPattern = ruleAddress(trimmed);
    const QString name = ruleName(trimmed);
    if (!hasSingleAtSign(addressPattern) || trimmed.count(QLatin1String(nameSeparator)) > 1)
        return false;
    for (const QChar character : addressPattern) {
        if (character.isSpace() || character == QLatin1Char('<') || character == QLatin1Char('>'))
            return false;
    }
    return !name.contains(QLatin1Char('\n')) && !name.contains(QLatin1Char('\r'));
}

bool senderRuleMatchesAddress(const QString& emailAddress, const QString& rule)
{
    return senderRuleMatches(emailAddress, {}, rule);
}

bool senderRuleMatches(const QString& emailAddress, const QString& senderName, const QString& rule)
{
    const QString address = emailAddress.trimmed();
    const QString trimmedRule = rule.trimmed();
    if (!hasSingleAtSign(address) || !isValidSenderRule(trimmedRule))
        return false;
    const QString requiredName = ruleName(trimmedRule);
    if (!requiredName.isEmpty()) {
        const QString localPart = address.left(address.indexOf(QLatin1Char('@')));
        const QRegularExpression nameExpression(addressPattern(requiredName),
                                                QRegularExpression::CaseInsensitiveOption);
        if (!nameExpression.match(senderName.trimmed()).hasMatch() &&
            !nameExpression.match(localPart).hasMatch())
            return false;
    }
    const QRegularExpression expression(addressPattern(ruleAddress(trimmedRule)),
                                        QRegularExpression::CaseInsensitiveOption);
    return expression.isValid() && expression.match(address).hasMatch();
}

QString senderRuleDisplayText(const QString& rule)
{
    const QString name = ruleName(rule);
    const QString address = ruleAddress(rule);
    return name.isEmpty() ? address : QStringLiteral("%1 <%2>").arg(name, address);
}

} // namespace productivity::services

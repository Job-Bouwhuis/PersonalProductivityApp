#pragma once

#include <QString>

namespace productivity::services {

bool isValidSenderRule(const QString& rule);
bool senderRuleMatchesAddress(const QString& emailAddress, const QString& rule);
bool senderRuleMatches(const QString& emailAddress, const QString& senderName, const QString& rule);
QString senderRuleDisplayText(const QString& rule);

} // namespace productivity::services

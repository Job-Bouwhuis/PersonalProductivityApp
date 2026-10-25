#pragma once

namespace productivity::services {

class AutostartService final
{
public:
    bool isEnabled() const;
    bool setEnabled(bool enabled) const;
};

} // namespace productivity::services

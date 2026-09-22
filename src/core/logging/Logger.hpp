#pragma once

#include "../Logger.hpp"

#define TUTONES_LOG_INFO(component, message) ::TutonesV2::Core::Logger::Get().Info((component), (message))
#define TUTONES_LOG_WARN(component, message) ::TutonesV2::Core::Logger::Get().Warn((component), (message))
#define TUTONES_LOG_ERROR(component, message) ::TutonesV2::Core::Logger::Get().Error((component), (message))
#define TUTONES_LOG_DEBUG(component, message) ::TutonesV2::Core::Logger::Get().Info((component), (message))

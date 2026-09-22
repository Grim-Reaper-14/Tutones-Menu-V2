#pragma once
#include <array>

namespace TutonesV2::Game::VehicleCatalogs
{
    inline constexpr std::array<const char*, 23> VehicleClassNames{{
        "Compact", "Sedan", "SUV", "Coupe", "Muscle", "Sport Classic", "Sport", "Super",
        "Motorcycle", "Off-road", "Industrial", "Utility", "Van", "Cycle", "Boat", "Helicopter",
        "Plane", "Service", "Emergency", "Military", "Commercial", "Rail", "Open Wheel",
    }};
}

#pragma once

#include "config/config_types.h"

#include <chrono>
#include <optional>
#include <string_view>

namespace day_night_schedule {

  struct GeoCoordinates {
    std::optional<double> latitude;
    std::optional<double> longitude;
  };

  struct Evaluation {
    bool night = false;
    std::chrono::milliseconds untilBoundary = std::chrono::hours(1);
  };

  [[nodiscard]] std::optional<std::string> normalizedClock(std::string_view value);
  [[nodiscard]] GeoCoordinates resolveCoordinates(
      const LocationConfig& config, std::optional<double> weatherLatitude, std::optional<double> weatherLongitude
  );
  [[nodiscard]] bool isManualMode(
      const LocationConfig& config, std::optional<double> weatherLatitude, std::optional<double> weatherLongitude
  );
  [[nodiscard]] Evaluation
  evaluate(const LocationConfig& config, std::optional<double> weatherLatitude, std::optional<double> weatherLongitude);

} // namespace day_night_schedule

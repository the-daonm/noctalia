#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class HttpClient;

namespace noctalia::theme {

  struct AvailablePalette {
    struct PreviewMode {
      std::string surface;
      std::vector<std::string> accents;
    };

    struct Preview {
      PreviewMode dark;
      PreviewMode light;
    };

    std::string name;
    Preview preview = {};
  };

  class CommunityPaletteService {
  public:
    using ReadyCallback = std::function<void()>;

    explicit CommunityPaletteService(HttpClient& httpClient);

    void setReadyCallback(ReadyCallback callback);
    void sync();

  private:
    HttpClient& m_httpClient;
    ReadyCallback m_readyCallback;
    std::uint64_t m_generation = 0;
  };

  [[nodiscard]] std::vector<AvailablePalette> availableCommunityPalettes();
  [[nodiscard]] std::filesystem::path communityPaletteCacheDir();
  [[nodiscard]] std::filesystem::path communityPaletteCachePath(std::string_view name);
  [[nodiscard]] std::string communityPaletteDownloadUrl(std::string_view name);

} // namespace noctalia::theme

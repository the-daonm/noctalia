#include "scripting/plugin_manager.h"

#include "config/config_service.h"
#include "core/build_info.h"
#include "core/deferred_call.h"
#include "core/log.h"
#include "core/version.h"
#include "scripting/plugin_catalog.h"
#include "scripting/plugin_git.h"
#include "scripting/plugin_manifest.h"
#include "scripting/plugin_registry.h"
#include "util/file_utils.h"

#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace scripting {

  namespace {
    constexpr Logger kLog("plugins");

    // Repo subdir for a plugin id by convention: "author/foo" lives at "foo/".
    std::string pluginSubdir(std::string_view pluginId) {
      const auto slash = pluginId.find('/');
      return slash == std::string_view::npos ? std::string(pluginId) : std::string(pluginId.substr(slash + 1));
    }
  } // namespace

  std::filesystem::path PluginManager::sourceRoot(const PluginSourceConfig& source) const {
    if (source.kind == PluginSourceKind::Path) {
      return FileUtils::expandUserPath(source.location);
    }
    return std::filesystem::path(FileUtils::pluginSourcesDir()) / source.name;
  }

  std::optional<PluginSourceConfig> PluginManager::findSourceOffering(std::string_view pluginId) const {
    for (const auto& source : m_config.config().plugins.sources) {
      const auto catalog = discoverCatalog(source);
      for (const auto& entry : catalog.entries) {
        if (entry.id == pluginId) {
          return source;
        }
      }
    }
    return std::nullopt;
  }

  std::optional<PluginSourceConfig> PluginManager::findSource(std::string_view name) const {
    for (const auto& source : m_config.config().plugins.sources) {
      if (source.name == name) {
        return source;
      }
    }
    return std::nullopt;
  }

  std::unordered_set<std::string> PluginManager::localPluginIds() const {
    std::unordered_set<std::string> ids;
    const std::string data = FileUtils::dataDir();
    if (data.empty()) {
      return ids;
    }
    PluginSourceConfig localSource{
        .kind = PluginSourceKind::Path, .name = "local", .location = (std::filesystem::path(data) / "plugins").string()
    };
    for (const auto& entry : discoverCatalog(localSource).entries) {
      ids.insert(entry.id);
    }
    return ids;
  }

  bool PluginManager::ensureEnabledMaterialized(const PluginsConfig& plugins) const {
    bool materialized = false;
    std::error_code ec;
    for (const auto& source : plugins.sources) {
      if (source.kind != PluginSourceKind::Git) {
        continue;
      }
      const std::filesystem::path root = sourceRoot(source);
      if (!std::filesystem::exists(root / ".git", ec)) {
        continue; // not cloned yet — enable/list/update will clone
      }
      for (const auto& id : plugins.enabled) {
        const std::string sub = pluginSubdir(id);
        if (std::filesystem::exists(root / sub / "plugin.toml", ec)) {
          continue; // already checked out
        }
        if (!plugin_git::hasPath(root, sub + "/plugin.toml")) {
          continue; // this source doesn't ship it
        }
        kLog.info("materializing enabled plugin '{}' from source '{}'", id, source.name);
        if (plugin_git::sparseAdd(root, sub)) {
          materialized = true;
        }
      }
    }
    return materialized;
  }

  void PluginManager::refresh() {
    const PluginsConfig& pc = m_config.config().plugins;
    if (m_applied && pc == m_lastApplied) {
      return;
    }
    // Heal a wiped clone / restored config once at startup (the clone state does
    // not change on later config reloads, so don't re-touch the network then).
    if (!m_applied) {
      ensureEnabledMaterialized(pc);
    }

    // Scan the local dev dir + every configured source; a plugin is active only if
    // its id is in [plugins].enabled (opt-in, uniform across all sources).
    std::vector<std::filesystem::path> roots;
    if (const std::string data = FileUtils::dataDir(); !data.empty()) {
      roots.push_back(std::filesystem::path(data) / "plugins");
    }
    for (const auto& source : pc.sources) {
      roots.push_back(sourceRoot(source));
    }

    auto& registry = PluginRegistry::instance();
    registry.setSources(std::move(roots));
    registry.setEnabledFilter(std::unordered_set<std::string>(pc.enabled.begin(), pc.enabled.end()));
    registry.scan();

    m_lastApplied = pc;
    m_applied = true;
  }

  EnableResult PluginManager::enable(std::string_view pluginId) {
    const std::string id(pluginId);

    // Managed source: fetch the code (git sparse-checkout) before enabling.
    if (const auto source = findSourceOffering(id); source.has_value()) {
      const std::filesystem::path root = sourceRoot(*source);
      const std::string subdir = pluginSubdir(id);
      if (source->kind == PluginSourceKind::Git) {
        const auto materialized = plugin_git::sparseAdd(root, subdir);
        if (!materialized) {
          return {.ok = false, .error = "sparse-checkout failed: " + materialized.err};
        }
      }
      std::string error;
      const auto manifest = parsePluginManifest(root / subdir / "plugin.toml", &error);
      if (!manifest.has_value()) {
        return {.ok = false, .error = error};
      }
      if (!noctalia::version::atLeast(noctalia::build_info::version(), manifest->minNoctalia)) {
        return {
            .ok = false,
            .error = "plugin '"
                + manifest->id
                + "' requires noctalia >= "
                + manifest->minNoctalia
                + " (running "
                + std::string(noctalia::build_info::version())
                + ")",
        };
      }
    } else if (!localPluginIds().contains(id)) {
      // Not offered by any managed source and not present locally.
      return {.ok = false, .error = "no plugin '" + id + "' found in any source"};
    }

    kLog.info("enabling plugin '{}'", id);
    m_config.setPluginEnabled(id, true);
    refresh();
    return {.ok = true, .error = {}};
  }

  void PluginManager::disable(std::string_view pluginId) {
    kLog.info("disabling plugin '{}'", pluginId);
    m_config.setPluginEnabled(pluginId, false);
    refresh();
  }

  std::vector<PluginStatus> PluginManager::list() const {
    const auto& pc = m_config.config().plugins;
    const std::unordered_set<std::string> enabledSet(pc.enabled.begin(), pc.enabled.end());

    std::vector<PluginStatus> out;
    const auto collect = [&](const std::string& sourceName, const CatalogResult& catalog) {
      for (const auto& entry : catalog.entries) {
        out.push_back(
            PluginStatus{
                .id = entry.id,
                .version = entry.version,
                .source = sourceName,
                .compatible = entry.compatible,
                .enabled = enabledSet.contains(entry.id),
            }
        );
      }
    };

    if (const std::string data = FileUtils::dataDir(); !data.empty()) {
      PluginSourceConfig localSource{
          .kind = PluginSourceKind::Path,
          .name = "local",
          .location = (std::filesystem::path(data) / "plugins").string()
      };
      collect("local", discoverCatalog(localSource));
    }
    for (const auto& source : pc.sources) {
      collect(source.name, discoverCatalog(source));
    }
    return out;
  }

  void PluginManager::addSource(const PluginSourceConfig& source) {
    kLog.info("adding plugin source '{}' ({})", source.name, source.location);
    m_config.addPluginSource(source); // fires reload -> refresh re-injects the registry
  }

  void PluginManager::update(std::string sourceName) {
    const auto source = findSource(sourceName);
    if (!source.has_value() || source->kind != PluginSourceKind::Git) {
      return; // path / unknown sources are externally owned
    }
    const std::filesystem::path root = sourceRoot(*source);
    std::error_code ec;
    if (!std::filesystem::exists(root / ".git", ec)) {
      return; // nothing cloned yet
    }
    // Snapshot the enabled set for the worker (config is read on the main thread only).
    std::unordered_set<std::string> enabled(
        m_config.config().plugins.enabled.begin(), m_config.config().plugins.enabled.end()
    );

    // The whole git sequence runs off-thread; only the final registry rescan marshals
    // back to the main thread. `this` is an Application member, so it outlives the worker.
    std::thread([root, sourceName = std::move(sourceName), enabled = std::move(enabled)]() mutable {
      const auto fetched = plugin_git::fetch(root);
      if (!fetched) {
        DeferredCall::callLater([sourceName, err = fetched.err]() {
          kLog.warn("update '{}': fetch failed: {}", sourceName, err);
        });
        return;
      }
      const std::string newRev = plugin_git::remoteHead(root).out;
      const std::string curRev = plugin_git::headRevision(root).out;
      if (newRev.empty() || newRev == curRev) {
        DeferredCall::callLater([sourceName]() { kLog.info("source '{}' already up to date", sourceName); });
        return;
      }

      // Compatibility guard BEFORE applying: read the *new* catalog at the fetched
      // revision (no working-tree change) and check every enabled plugin's
      // min_noctalia. If one would require a newer Noctalia, skip the update — nothing
      // is applied, so there is nothing to undo.
      if (const auto catalog = plugin_git::showFile(root, "catalog.toml", newRev); catalog) {
        for (const auto& entry : parseCatalogToml(catalog.out)) {
          if (enabled.contains(entry.id)
              && !entry.minNoctalia.empty()
              && !noctalia::version::atLeast(noctalia::build_info::version(), entry.minNoctalia)) {
            DeferredCall::callLater([sourceName, id = entry.id, min = entry.minNoctalia]() {
              kLog.warn(
                  "update '{}' withheld: '{}' requires noctalia >= {} (running {})", sourceName, id, min,
                  noctalia::build_info::version()
              );
            });
            return;
          }
        }
      }

      const auto applied = plugin_git::fastForward(root, newRev);
      DeferredCall::callLater([sourceName, ok = static_cast<bool>(applied), err = applied.err, newRev]() {
        if (!ok) {
          kLog.warn("update '{}': apply failed: {}", sourceName, err);
          return;
        }
        kLog.info("updated source '{}' -> {}", sourceName, newRev);
        PluginRegistry::instance().scan(); // re-parse manifests; live .luau changes hot-reload via file watch
      });
    }).detach();
  }

  void PluginManager::removeSource(std::string sourceName) {
    const auto source = findSource(sourceName);
    if (!source.has_value()) {
      return;
    }
    kLog.info("removing plugin source '{}'", sourceName);

    // Disable this source's plugins so no stale enabled ids linger.
    for (const auto& entry : discoverCatalog(*source).entries) {
      m_config.setPluginEnabled(entry.id, false);
    }
    if (source->kind == PluginSourceKind::Git) {
      std::error_code ec;
      std::filesystem::remove_all(sourceRoot(*source), ec); // delete the clone
    }
    m_config.removePluginSource(sourceName); // fires reload -> refresh re-injects
  }

} // namespace scripting

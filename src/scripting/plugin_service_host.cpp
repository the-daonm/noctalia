#include "scripting/plugin_service_host.h"

#include "core/log.h"
#include "scripting/plugin_manifest.h"
#include "scripting/plugin_registry.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace scripting {

  namespace {
    constexpr Logger kLog("plugin-service");

    std::string readFile(const std::filesystem::path& path) {
      std::ifstream file(path);
      if (!file) {
        return {};
      }
      std::stringstream ss;
      ss << file.rdbuf();
      return ss.str();
    }
  } // namespace

  PluginServiceHost::PluginServiceHost(ScriptApiContext& scriptApi, HttpClient* httpClient, ClipboardService* clipboard)
      : m_scriptApi(scriptApi), m_httpClient(httpClient), m_clipboard(clipboard) {}

  PluginServiceHost::~PluginServiceHost() {
    for (auto& service : m_services) {
      if (service->alive) {
        *service->alive = false;
      }
      if (service->runtime != nullptr) {
        if (service->subscription != 0) {
          service->runtime->unsubscribe(service->subscription);
        }
        service->runtime->stop();
      }
    }
  }

  void PluginServiceHost::start() {
    PluginRegistry::instance().ensureScanned();
    for (const auto& entry : PluginRegistry::instance().entriesOfKind(PluginEntryKind::Service)) {
      const std::filesystem::path source = entry.sourcePath;
      std::string code = readFile(source);
      if (code.empty()) {
        kLog.warn("service '{}': empty or unreadable source {}", entry.fullId(), source.string());
        continue;
      }

      auto service = std::make_unique<Service>();
      service->entryId = entry.fullId();
      auto seeded = seedEntrySettings(*entry.entry, {});
      service->runtime = std::make_shared<ScriptRuntime>(
          entry.fullId(), std::move(seeded), m_scriptApi, source.parent_path(), m_httpClient, m_clipboard
      );

      Service* svc = service.get();
      std::weak_ptr<bool> alive = service->alive;
      service->subscription = service->runtime->subscribe([this, svc, alive](const ScriptWidgetResult& result) {
        auto token = alive.lock();
        if (token == nullptr || !*token) {
          return;
        }
        if (result.patch.updateIntervalMs.has_value()) {
          const int next = std::max(16, *result.patch.updateIntervalMs);
          if (next != svc->updateIntervalMs) {
            svc->updateIntervalMs = next;
            armTimer(*svc);
          }
        }
      });

      service->runtime->start(source.string(), std::move(code), {});
      kLog.info("started service '{}'", entry.fullId());
      armTimer(*service);
      m_services.push_back(std::move(service));
    }
  }

  void PluginServiceHost::armTimer(Service& service) {
    service.updateTimer.stop();
    Service* svc = &service;
    std::weak_ptr<bool> alive = service.alive;
    service.updateTimer.startRepeating(std::chrono::milliseconds(service.updateIntervalMs), [svc, alive] {
      auto token = alive.lock();
      if (token != nullptr && *token && svc->runtime != nullptr) {
        (void)svc->runtime->enqueueUpdate({});
      }
    });
  }

} // namespace scripting

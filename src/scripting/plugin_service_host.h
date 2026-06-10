#pragma once

#include "core/timer_manager.h"
#include "scripting/script_runtime.h"

#include <memory>
#include <string>
#include <vector>

class HttpClient;
class ClipboardService;

namespace scripting {

  class ScriptApiContext;

  // Hosts headless [[service]] entries: one singleton runtime per service entry,
  // started at launch and ticked on its own interval. Services hold a plugin's
  // shared/background logic and publish to the per-plugin state store; the
  // plugin's UI entries (widgets, panels) consume it via noctalia.state.
  class PluginServiceHost {
  public:
    PluginServiceHost(ScriptApiContext& scriptApi, HttpClient* httpClient, ClipboardService* clipboard);
    ~PluginServiceHost();

    PluginServiceHost(const PluginServiceHost&) = delete;
    PluginServiceHost& operator=(const PluginServiceHost&) = delete;

    // Discover and launch every [[service]] entry in the registry.
    void start();

  private:
    struct Service {
      std::string entryId;
      std::shared_ptr<ScriptRuntime> runtime;
      ScriptRuntime::SubscriberId subscription = 0;
      Timer updateTimer;
      int updateIntervalMs = 1000;
      std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    };

    void armTimer(Service& service);

    ScriptApiContext& m_scriptApi;
    HttpClient* m_httpClient = nullptr;
    ClipboardService* m_clipboard = nullptr;
    std::vector<std::unique_ptr<Service>> m_services;
  };

} // namespace scripting

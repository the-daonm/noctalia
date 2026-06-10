#include "shell/settings/settings_content_plugins.h"

#include "i18n/i18n.h"
#include "ui/builders.h"
#include "ui/controls/flex.h"
#include "ui/palette.h"
#include "ui/style.h"

#include <memory>
#include <string>
#include <utility>

namespace settings {

  namespace {
    std::unique_ptr<Label>
    makeLabel(std::string_view text, float fontSize, ColorRole role, FontWeight weight = FontWeight::Normal) {
      return ui::label({
          .text = std::string(text),
          .fontSize = fontSize,
          .color = colorSpecFromRole(role),
          .fontWeight = weight,
      });
    }

    std::unique_ptr<Flex> sourceRow(const PluginSourceConfig& source, const SettingsPluginsContext& ctx, float scale) {
      auto row = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true});
      Flex* r = row.get();

      auto info = ui::column({.align = FlexAlign::Start, .gap = 2.0F * scale, .flexGrow = 1.0F});
      info->addChild(makeLabel(source.name, Style::fontSizeBody * scale, ColorRole::OnSurface, FontWeight::Medium));
      const std::string kind = source.kind == PluginSourceKind::Git ? "git" : "path";
      info->addChild(
          makeLabel(kind + " · " + source.location, Style::fontSizeCaption * scale, ColorRole::OnSurfaceVariant)
      );
      r->addChild(std::move(info));

      if (source.kind == PluginSourceKind::Git) {
        r->addChild(
            ui::button({
                .text = "Update",
                .fontSize = Style::fontSizeCaption * scale,
                .variant = ButtonVariant::Outline,
                .onClick = [cb = ctx.updateSource, name = source.name]() {
                  if (cb) {
                    cb(name);
                  }
                },
            })
        );
      }
      r->addChild(
          ui::button({
              .glyph = "trash",
              .glyphSize = Style::fontSizeBody * scale,
              .variant = ButtonVariant::Ghost,
              .tooltip = "Remove source",
              .onClick = [cb = ctx.removeSource, name = source.name]() {
                if (cb) {
                  cb(name);
                }
              },
          })
      );
      return row;
    }

    std::unique_ptr<Flex>
    pluginRow(const scripting::PluginStatus& plugin, const SettingsPluginsContext& ctx, float scale) {
      auto row = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true});
      Flex* r = row.get();

      auto info = ui::column({.align = FlexAlign::Start, .gap = 2.0F * scale, .flexGrow = 1.0F});
      auto title = ui::row({.align = FlexAlign::Center, .gap = Style::spaceXs * scale});
      title->addChild(makeLabel(plugin.id, Style::fontSizeBody * scale, ColorRole::OnSurface, FontWeight::Medium));
      if (!plugin.compatible) {
        title->addChild(
            makeLabel("requires newer noctalia", Style::fontSizeMini * scale, ColorRole::Error, FontWeight::Bold)
        );
      }
      info->addChild(std::move(title));
      const std::string version = plugin.version.empty() ? std::string("?") : plugin.version;
      info->addChild(
          makeLabel("v" + version + " · " + plugin.source, Style::fontSizeCaption * scale, ColorRole::OnSurfaceVariant)
      );
      r->addChild(std::move(info));

      r->addChild(
          ui::toggle({
              .checked = plugin.enabled,
              .enabled = plugin.compatible,
              .scale = scale,
              .onChange = [cb = ctx.setEnabled, id = plugin.id](bool on) {
                if (cb) {
                  cb(id, on);
                }
              },
          })
      );
      return row;
    }
  } // namespace

  void addSettingsPlugins(Flex& content, SettingsPluginsContext ctx) {
    if (ctx.selectedSection != "plugins") {
      return;
    }
    const float scale = ctx.scale;

    auto sectionCol = ui::column({
        .align = FlexAlign::Stretch,
        .gap = Style::spaceSm * scale,
        .padding = Style::spaceLg * scale,
        .fill = clearColorSpec(),
        .fillWidth = true,
    });
    Flex* section = sectionCol.get();
    content.addChild(std::move(sectionCol));

    section->addChild(
        ui::row(
            {.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true},
            ui::glyph({
                .glyph = "puzzle",
                .glyphSize = Style::fontSizeHeader * scale,
                .color = colorSpecFromRole(ColorRole::Primary),
            }),
            makeLabel(
                i18n::tr("settings.navigation.sections.plugins"), Style::fontSizeHeader * scale, ColorRole::Primary,
                FontWeight::Bold
            )
        )
    );

    // ── Sources ──────────────────────────────────────────────────────────
    section->addChild(makeLabel("Sources", Style::fontSizeBody * scale, ColorRole::Secondary, FontWeight::Bold));
    if (ctx.sources.empty()) {
      section->addChild(
          makeLabel("No sources configured.", Style::fontSizeCaption * scale, ColorRole::OnSurfaceVariant)
      );
    }
    for (const auto& source : ctx.sources) {
      section->addChild(sourceRow(source, ctx, scale));
    }

    section->addChild(ui::separator());

    // ── Plugins ──────────────────────────────────────────────────────────
    section->addChild(makeLabel("Plugins", Style::fontSizeBody * scale, ColorRole::Secondary, FontWeight::Bold));
    if (ctx.plugins.empty()) {
      section->addChild(makeLabel(
          "No plugins found. Add a source, or check your network.", Style::fontSizeCaption * scale,
          ColorRole::OnSurfaceVariant
      ));
    }
    for (const auto& plugin : ctx.plugins) {
      section->addChild(pluginRow(plugin, ctx, scale));
    }
  }

} // namespace settings

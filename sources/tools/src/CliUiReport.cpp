#include "CliUiReport.hpp"

#include "CliCommon.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneUI.hpp"

#include <charconv>
#include <cmath>

namespace kb::cli {
namespace {

using kb::core::JsonValue;

[[nodiscard]] std::optional<float> ParseNumber(std::string_view text) {
    float value = 0.0F;
    const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (code != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) return std::nullopt;
    return value;
}

[[nodiscard]] std::string PathOf(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    return entity.IsValid() && scene.Entities().IsAlive(entity) ? EntityPath(scene, entity) : std::string{};
}

[[nodiscard]] kb::math::Vec2 Middle(const kb::scene::SceneUIFrameElement& element) {
    kb::math::Vec2 sum{};
    for (const kb::math::Vec2& corner : element.hitCorners) {
        sum.x += corner.x * 0.25F;
        sum.y += corner.y * 0.25F;
    }
    return sum;
}

[[nodiscard]] JsonValue Number(double value) { return JsonValue::MakeNumber(value); }

[[nodiscard]] JsonValue DispatchJson(const kb::scene::Scene& scene, const kb::script::ScriptUIDispatch& dispatch) {
    auto json = JsonValue::MakeObject();
    json.Set("name", JsonValue::MakeString(dispatch.name));
    json.Set("widget", JsonValue::MakeString(PathOf(scene, dispatch.widget)));
    json.Set("target", JsonValue::MakeString(PathOf(scene, dispatch.target)));
    json.Set("answered", Number(dispatch.answered));
    return json;
}

// The functions among `names` that an entity's Lua behaviour defines, with the script's path.
void AppendAnswers(JsonValue& answers, const kb::scene::Scene& scene, const kb::script::UIScriptIndex& scripts,
    kb::scene::SceneEntity entity, const std::vector<std::string>& names, std::string_view role) {
    const kb::scene::BehaviourComponent* behaviour = scene.Components().Behaviours().TryGet(entity);
    if (behaviour == nullptr || !behaviour->enabled) return;
    const kb::assets::AssetMetadata* script = scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ behaviour->behaviourAssetId });
    for (const std::string& name : names) {
        if (!scripts.Defines(behaviour->behaviourAssetId, name)) continue;
        auto answer = JsonValue::MakeObject();
        answer.Set("event", JsonValue::MakeString(name));
        answer.Set("role", JsonValue::MakeString(std::string{ role }));
        answer.Set("entity", JsonValue::MakeString(PathOf(scene, entity)));
        answer.Set("script", JsonValue::MakeString(script != nullptr ? script->virtualPath.generic_string() : std::string{}));
        answers.Append(std::move(answer));
    }
}

} // namespace

std::optional<UiClick> ParseUiClick(std::string_view text, std::string& error) {
    const std::size_t at = text.rfind('@');
    UiClick click;
    int frame = 0;
    const bool framed = at != std::string_view::npos && at > 0U &&
        std::from_chars(text.data() + at + 1U, text.data() + text.size(), frame).ec == std::errc{} && frame >= 2;
    if (!framed) {
        error = "--click expects <widget path or x,y>@<frame> with the frame 2 or later, e.g. --click Menu/PlayButton@10 or --click 320,240@10; got '" +
            std::string{ text } + "'";
        return std::nullopt;
    }
    click.target = std::string{ text.substr(0U, at) };
    click.frame = frame;
    if (const std::size_t comma = click.target.find(','); comma != std::string::npos) {
        const std::optional<float> x = ParseNumber(std::string_view{ click.target }.substr(0U, comma));
        const std::optional<float> y = ParseNumber(std::string_view{ click.target }.substr(comma + 1U));
        if (!x || !y) {
            error = "--click point '" + click.target + "' is not x,y in viewport pixels";
            return std::nullopt;
        }
        click.point = kb::math::Vec2{ *x, *y };
    }
    return click;
}

bool PlaceUiClick(kb::scene::Scene& scene, UiClick& click, std::string& error) {
    if (click.point) {
        click.position = *click.point;
        click.placed = true;
        return true;
    }
    const std::vector<kb::scene::SceneEntity> matches = MatchEntities(scene, click.target);
    if (matches.size() != 1U) {
        error = matches.empty() ? "--click target '" + click.target + "' names no object of the scene"
                                : "--click target '" + click.target + "' names " + std::to_string(matches.size()) + " objects; give its full path";
        return false;
    }
    click.widget = matches.front();
    for (const kb::scene::SceneUIFrameElement& element : scene.UI().Frame().elements) {
        if (element.entity != click.widget || element.tooltip) continue;
        click.position = Middle(element);
        click.placed = true;
        return true;
    }
    error = "--click target '" + click.target + "' is not laid out at frame " + std::to_string(click.frame - 1) +
        ": it has no UI components, is outside a Canvas or is hidden (kb_cli validate names the first two)";
    return false;
}

std::string DescribeUiClick(const kb::scene::Scene& scene, const UiClick& click) {
    std::string line = "click " + click.target + " at frame " + std::to_string(click.frame) + ": pointer " +
        std::to_string(static_cast<int>(std::lround(click.position.x))) + "," + std::to_string(static_cast<int>(std::lround(click.position.y))) +
        ", under it " + (click.hit.empty() ? std::string{ "no widget" } : click.hit);
    if (click.dispatched.empty()) return line + "; no widget event reached a script";
    for (const kb::script::ScriptUIDispatch& dispatch : click.dispatched) {
        // Hover, press and focus callbacks nobody wrote are not news; the click and the named events are.
        if (dispatch.answered == 0U && dispatch.name.starts_with("OnUI") && dispatch.name != "OnUIClicked") continue;
        const std::string target = PathOf(scene, dispatch.target);
        line += "; " + dispatch.name + " -> " + (target.empty() ? std::string{ "(gone)" } : target) +
            (dispatch.answered == 0U ? " unanswered" : " answered by " + std::to_string(dispatch.answered));
    }
    return line;
}

JsonValue BuildUiReport(kb::scene::Scene& scene, const kb::script::UIScriptIndex& scripts, const std::vector<UiClick>& clicks,
    const std::vector<std::pair<int, kb::script::ScriptUIDispatch>>& events, bool succeeded) {
    std::vector<std::string> callbacks;
    for (const kb::scene::SceneUIEventDescriptor& descriptor : kb::scene::SceneUIEventCatalog()) {
        callbacks.emplace_back(descriptor.callbackName);
    }
    const kb::scene::SceneUIFrame& frame = scene.UI().Frame();
    auto report = JsonValue::MakeObject();
    report.Set("schema", JsonValue::MakeString("21kb.ui-report/v1"));
    report.Set("succeeded", JsonValue::MakeBool(succeeded));
    auto viewport = JsonValue::MakeObject();
    viewport.Set("width", Number(frame.viewportSize.x));
    viewport.Set("height", Number(frame.viewportSize.y));
    report.Set("viewport", std::move(viewport));

    auto widgets = JsonValue::MakeArray();
    for (const kb::scene::SceneUIFrameElement& element : frame.elements) {
        if (element.tooltip) continue;
        auto widget = JsonValue::MakeObject();
        widget.Set("path", JsonValue::MakeString(PathOf(scene, element.entity)));
        auto rect = JsonValue::MakeObject();
        rect.Set("x", Number(element.rect.x));
        rect.Set("y", Number(element.rect.y));
        rect.Set("width", Number(element.rect.width));
        rect.Set("height", Number(element.rect.height));
        widget.Set("rect", std::move(rect));
        widget.Set("opacity", Number(element.effectiveOpacity));
        widget.Set("hit_testable", JsonValue::MakeBool(element.hitTestable));
        if (element.text) widget.Set("text", JsonValue::MakeString(std::string{ kb::scene::UITextContent(*element.text) }));
        if (element.selectable) {
            const std::string_view eventName = kb::scene::UIEventName(*element.selectable);
            widget.Set("interactable", JsonValue::MakeBool(element.interactionEnabled));
            widget.Set("event_name", JsonValue::MakeString(std::string{ eventName }));
            const kb::scene::SceneEntity target = element.selectable->eventTarget != 0U
                ? kb::scene::SceneEntity{ element.selectable->eventTarget } : element.entity;
            widget.Set("event_target", JsonValue::MakeString(PathOf(scene, target)));
            auto answers = JsonValue::MakeArray();
            AppendAnswers(answers, scene, scripts, element.entity, callbacks, "widget callback");
            if (!eventName.empty()) AppendAnswers(answers, scene, scripts, target, { std::string{ eventName } }, "event name");
            widget.Set("answers", std::move(answers));
        }
        widgets.Append(std::move(widget));
    }
    report.Set("widgets", std::move(widgets));

    auto clickList = JsonValue::MakeArray();
    for (const UiClick& click : clicks) {
        auto json = JsonValue::MakeObject();
        json.Set("target", JsonValue::MakeString(click.target));
        json.Set("frame", Number(click.frame));
        json.Set("x", Number(click.position.x));
        json.Set("y", Number(click.position.y));
        json.Set("hit", JsonValue::MakeString(click.hit));
        auto dispatched = JsonValue::MakeArray();
        for (const kb::script::ScriptUIDispatch& dispatch : click.dispatched) dispatched.Append(DispatchJson(scene, dispatch));
        json.Set("dispatched", std::move(dispatched));
        clickList.Append(std::move(json));
    }
    report.Set("clicks", std::move(clickList));

    auto eventList = JsonValue::MakeArray();
    for (const auto& [eventFrame, dispatch] : events) {
        auto json = DispatchJson(scene, dispatch);
        json.Set("frame", Number(eventFrame));
        eventList.Append(std::move(json));
    }
    report.Set("events", std::move(eventList));
    return report;
}

} // namespace kb::cli

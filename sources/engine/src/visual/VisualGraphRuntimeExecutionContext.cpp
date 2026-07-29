#include "engine/visual/VisualGraphRuntimeExecutionContext.hpp"

#include <optional>
#include <utility>

namespace kb::visual {

void VisualGraphRuntimeExecutionContext::Store(std::uint32_t nodeId, std::string_view pin, VisualGraphRuntimeValue value) {
    const std::string key = Key(nodeId, pin);
    values_[key] = std::move(value);
    writesInCurrentExecution_.insert(key);
}

const VisualGraphRuntimeValue* VisualGraphRuntimeExecutionContext::TryRead(std::uint32_t nodeId, std::string_view pin) const {
    const auto iter = values_.find(Key(nodeId, pin));
    return iter == values_.end() ? nullptr : &iter->second;
}

bool VisualGraphRuntimeExecutionContext::WasStoredInCurrentExecution(std::uint32_t nodeId, std::string_view pin) const {
    return writesInCurrentExecution_.contains(Key(nodeId, pin));
}

bool VisualGraphRuntimeExecutionContext::ReadBool(std::uint32_t nodeId, std::string_view pin, bool fallback) const {
    const VisualGraphRuntimeValue* value = TryRead(nodeId, pin);
    return value == nullptr ? fallback : value->AsBool(fallback);
}

int VisualGraphRuntimeExecutionContext::ReadInt(std::uint32_t nodeId, std::string_view pin, int fallback) const {
    const VisualGraphRuntimeValue* value = TryRead(nodeId, pin);
    return value == nullptr ? fallback : value->AsInt(fallback);
}

float VisualGraphRuntimeExecutionContext::ReadFloat(std::uint32_t nodeId, std::string_view pin, float fallback) const {
    const VisualGraphRuntimeValue* value = TryRead(nodeId, pin);
    return value == nullptr ? fallback : value->AsFloat(fallback);
}

double VisualGraphRuntimeExecutionContext::ReadDouble(std::uint32_t nodeId, std::string_view pin, double fallback) const {
    const VisualGraphRuntimeValue* value = TryRead(nodeId, pin);
    const std::optional<VisualGraphRuntimeValue> converted = value == nullptr ? std::nullopt : value->ConvertLosslessly(VisualGraphValueType::Double);
    return converted.has_value() ? converted->AsDouble(fallback) : fallback;
}

std::int64_t VisualGraphRuntimeExecutionContext::ReadInt64(std::uint32_t nodeId, std::string_view pin, std::int64_t fallback) const {
    const VisualGraphRuntimeValue* value = TryRead(nodeId, pin);
    const std::optional<VisualGraphRuntimeValue> converted = value == nullptr ? std::nullopt : value->ConvertLosslessly(VisualGraphValueType::Int64);
    return converted.has_value() ? converted->AsInt64(fallback) : fallback;
}

std::string VisualGraphRuntimeExecutionContext::ReadString(std::uint32_t nodeId, std::string_view pin) const {
    const VisualGraphRuntimeValue* value = TryRead(nodeId, pin);
    return value == nullptr ? std::string{} : value->AsString();
}

std::uint64_t VisualGraphRuntimeExecutionContext::ReadUInt64(std::uint32_t nodeId, std::string_view pin, std::uint64_t fallback) const {
    const VisualGraphRuntimeValue* value = TryRead(nodeId, pin);
    return value == nullptr ? fallback : value->AsUInt64(fallback);
}

void VisualGraphRuntimeExecutionContext::EmitEvent(std::string eventName) {
    EmitEvent(std::move(eventName), {});
}

void VisualGraphRuntimeExecutionContext::EmitEvent(std::string eventName, std::vector<VisualGraphEventArgument> arguments, kb::scene::SceneEntity target) {
    if (eventName.empty()) {
        return;
    }
    emittedEvents_.push_back(eventName);
    emittedEventRecords_.push_back(VisualGraphEmittedEvent{
        .name = std::move(eventName),
        .target = target,
        .arguments = std::move(arguments),
    });
}

void VisualGraphRuntimeExecutionContext::Trace(std::string message) {
    traces_.push_back(std::move(message));
}

void VisualGraphRuntimeExecutionContext::ReportError(std::string message) {
    if (message.empty()) {
        return;
    }
    runtimeErrors_.push_back(std::move(message));
}

const std::vector<std::string>& VisualGraphRuntimeExecutionContext::EmittedEvents() const noexcept {
    return emittedEvents_;
}

const std::vector<VisualGraphEmittedEvent>& VisualGraphRuntimeExecutionContext::EmittedEventRecords() const noexcept {
    return emittedEventRecords_;
}

const std::vector<std::string>& VisualGraphRuntimeExecutionContext::Traces() const noexcept {
    return traces_;
}

const std::vector<std::string>& VisualGraphRuntimeExecutionContext::RuntimeErrors() const noexcept {
    return runtimeErrors_;
}

void VisualGraphRuntimeExecutionContext::Suspend(std::uint32_t eventNodeId, std::uint32_t nextNodeId) {
    if (eventNodeId != 0U && nextNodeId != 0U) {
        continuations_[eventNodeId] = nextNodeId;
    }
}

std::uint32_t VisualGraphRuntimeExecutionContext::TakeContinuation(std::uint32_t eventNodeId) {
    const auto iter = continuations_.find(eventNodeId);
    if (iter == continuations_.end()) {
        return 0U;
    }
    const std::uint32_t nodeId = iter->second;
    continuations_.erase(iter);
    return nodeId;
}

void VisualGraphRuntimeExecutionContext::ClearContinuation(std::uint32_t eventNodeId) noexcept {
    continuations_.erase(eventNodeId);
}

void VisualGraphRuntimeExecutionContext::SetTaskIsRunningQuery(const void* userData, TaskIsRunningQuery query) noexcept {
    taskQueryUserData_ = userData;
    taskIsRunningQuery_ = query;
}

bool VisualGraphRuntimeExecutionContext::HasTaskIsRunningQuery() const noexcept {
    return taskIsRunningQuery_ != nullptr;
}

bool VisualGraphRuntimeExecutionContext::IsTaskRunning(std::uint64_t taskId) const noexcept {
    return taskIsRunningQuery_ != nullptr && taskId != 0U && taskIsRunningQuery_(taskQueryUserData_, taskId);
}

void VisualGraphRuntimeExecutionContext::SetWaitTask(std::uint32_t nodeId, std::uint64_t taskId) {
    if (nodeId != 0U && taskId != 0U) {
        waitTasks_[nodeId] = taskId;
    }
}

std::uint64_t VisualGraphRuntimeExecutionContext::WaitTask(std::uint32_t nodeId) const noexcept {
    const auto iterator = waitTasks_.find(nodeId);
    return iterator == waitTasks_.end() ? 0U : iterator->second;
}

void VisualGraphRuntimeExecutionContext::ClearWaitTask(std::uint32_t nodeId) noexcept {
    waitTasks_.erase(nodeId);
}

void VisualGraphRuntimeExecutionContext::BeginExecutionPass() {
    writesInCurrentExecution_.clear();
    emittedEvents_.clear();
    emittedEventRecords_.clear();
    traces_.clear();
    runtimeErrors_.clear();
}

void VisualGraphRuntimeExecutionContext::ClearFrameState() {
    values_.clear();
    writesInCurrentExecution_.clear();
    emittedEvents_.clear();
    emittedEventRecords_.clear();
    traces_.clear();
    runtimeErrors_.clear();
    continuations_.clear();
    waitTasks_.clear();
    taskQueryUserData_ = nullptr;
    taskIsRunningQuery_ = nullptr;
}

std::string VisualGraphRuntimeExecutionContext::Key(std::uint32_t nodeId, std::string_view pin) {
    return std::to_string(nodeId) + ":" + std::string{pin};
}

} // namespace kb::visual

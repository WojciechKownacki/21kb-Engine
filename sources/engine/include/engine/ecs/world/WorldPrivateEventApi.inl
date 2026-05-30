using RawComponentEventVisitor = void (*)(Entity entity, ComponentEventKind event, const void* component, void* context);
using RawContextFree = void (*)(void* context);

[[nodiscard]] ObserverId ObserveComponent(
    ComponentId componentId,
    std::size_t componentSize,
    ComponentEventKind event,
    RawComponentEventVisitor visitor,
    void* context,
    RawContextFree contextFree,
    bool yieldExisting) noexcept;

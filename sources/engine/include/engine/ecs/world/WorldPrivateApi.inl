using RawConstComponentVisitor = void (*)(Entity entity, const void* component, void* context);
using RawMutableComponentVisitor = void (*)(Entity entity, void* component, void* context);
using RawComponentEventVisitor = void (*)(Entity entity, ComponentEventKind event, const void* component, void* context);
using RawContextFree = void (*)(void* context);

template <typename T>
static constexpr void ValidateComponentType() noexcept;

template <typename T>
[[nodiscard]] static std::string_view DefaultComponentName() noexcept;

template <typename T>
static constexpr void ValidateTagType() noexcept;

template <typename T>
[[nodiscard]] static std::string_view DefaultTagName() noexcept;

[[nodiscard]] ComponentId RegisterComponent(std::type_index type, std::string_view name, std::size_t size, std::size_t alignment);
[[nodiscard]] ComponentId FindComponent(std::type_index type) const noexcept;
void SetComponent(Entity entity, ComponentId componentId, std::size_t size, const void* component);
[[nodiscard]] bool HasComponent(Entity entity, ComponentId componentId) const noexcept;
[[nodiscard]] const void* TryGetComponent(Entity entity, ComponentId componentId) const noexcept;
[[nodiscard]] void* TryGetMutableComponent(Entity entity, ComponentId componentId) noexcept;
void RemoveComponent(Entity entity, ComponentId componentId) noexcept;
void MarkComponentModified(Entity entity, ComponentId componentId) noexcept;
void ForEachComponent(ComponentId componentId, std::size_t componentSize, RawConstComponentVisitor visitor, void* context) const;
void ForEachMutableComponent(ComponentId componentId, std::size_t componentSize, RawMutableComponentVisitor visitor, void* context);
void ForEachComponents(
    ComponentId firstComponentId,
    std::size_t firstComponentSize,
    ComponentId secondComponentId,
    std::size_t secondComponentSize,
    void (*visitor)(Entity entity, const void* first, const void* second, void* context),
    void* context) const;
[[nodiscard]] QueryState* CreateQueryState(const ComponentId* componentIds, const std::size_t* componentSizes, std::size_t componentCount) const;
[[nodiscard]] ObserverId ObserveComponent(
    ComponentId componentId,
    std::size_t componentSize,
    ComponentEventKind event,
    RawComponentEventVisitor visitor,
    void* context,
    RawContextFree contextFree,
    bool yieldExisting) noexcept;
[[nodiscard]] TagId RegisterTag(std::type_index type, std::string_view name);
[[nodiscard]] TagId FindTag(std::type_index type) const noexcept;
[[nodiscard]] RelationId RegisterRelation(std::type_index type, std::string_view name);
[[nodiscard]] RelationId FindRelation(std::type_index type) const noexcept;
void Reset() noexcept;

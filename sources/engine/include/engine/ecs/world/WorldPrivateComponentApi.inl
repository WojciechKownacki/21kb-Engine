using RawConstComponentVisitor = void (*)(Entity entity, const void* component, void* context);
using RawMutableComponentVisitor = void (*)(Entity entity, void* component, void* context);

template <typename T>
static constexpr void ValidateComponentType() noexcept;

template <typename T>
[[nodiscard]] static std::string_view DefaultComponentName() noexcept;

[[nodiscard]] ComponentId RegisterComponent(std::type_index type, std::string_view name, std::size_t size, std::size_t alignment);
[[nodiscard]] ComponentId FindComponent(std::type_index type) const noexcept;
[[nodiscard]] const ComponentReflection* RegisterComponentReflection(ComponentId componentId, std::string_view name, std::size_t size, std::initializer_list<ComponentFieldDesc> fields);
void SetComponent(Entity entity, ComponentId componentId, std::size_t size, const void* component);
[[nodiscard]] bool HasComponent(Entity entity, ComponentId componentId) const;
[[nodiscard]] const void* TryGetComponent(Entity entity, ComponentId componentId) const;
[[nodiscard]] void* TryGetMutableComponent(Entity entity, ComponentId componentId);
void RemoveComponent(Entity entity, ComponentId componentId);
void MarkComponentModified(Entity entity, ComponentId componentId);
void ForEachComponent(ComponentId componentId, std::size_t componentSize, RawConstComponentVisitor visitor, void* context) const;
void ForEachMutableComponent(ComponentId componentId, std::size_t componentSize, RawMutableComponentVisitor visitor, void* context);
void ForEachComponents(
    ComponentId firstComponentId,
    std::size_t firstComponentSize,
    ComponentId secondComponentId,
    std::size_t secondComponentSize,
    void (*visitor)(Entity entity, const void* first, const void* second, void* context),
    void* context) const;

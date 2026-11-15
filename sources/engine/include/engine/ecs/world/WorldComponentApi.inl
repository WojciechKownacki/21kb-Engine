template <typename T>
using ConstComponentVisitor = void (*)(Entity entity, const T& component, void* context);

template <typename T>
using MutableComponentVisitor = void (*)(Entity entity, T& component, void* context);

template <typename TFirst, typename TSecond>
using ConstComponentsVisitor = void (*)(Entity entity, const TFirst& first, const TSecond& second, void* context);

template <typename T>
[[nodiscard]] ComponentId RegisterComponent(std::string_view name = {});

template <typename T>
[[nodiscard]] ComponentId RegisterComponent(ComponentRegistrationOptions options);

template <typename T>
[[nodiscard]] ComponentId RegisterComponent(std::string_view name, ComponentRegistrationOptions options);

template <typename T>
[[nodiscard]] ComponentId Component() const noexcept;

template <typename T>
[[nodiscard]] ComponentStorageClass ComponentStorage() const noexcept;

[[nodiscard]] ComponentStorageClass ComponentStorage(ComponentId componentId) const noexcept;

template <typename T>
[[nodiscard]] const ComponentReflection* RegisterComponentReflection(std::string_view name, std::initializer_list<ComponentFieldDesc> fields);

template <typename T>
[[nodiscard]] const ComponentReflection* Reflection() const noexcept;

[[nodiscard]] const ComponentReflection* Reflection(ComponentId componentId) const noexcept;
[[nodiscard]] const ComponentReflection* Reflection(std::string_view componentName) const noexcept;

template <typename T>
void Set(Entity entity, const T& component);

template <typename T>
void SetMany(std::span<const Entity> entities, std::span<const T> components);

template <typename T>
[[nodiscard]] bool Has(Entity entity) const;

template <typename T>
[[nodiscard]] const T* TryGet(Entity entity) const;

template <typename T>
[[nodiscard]] T* TryGetMutable(Entity entity);

template <typename T>
void Remove(Entity entity);

// Whole-archetype changes: each archetype that matches moves in one migration instead of one per entity. The
// filter narrows the entities by required and excluded components (Changed filters are not supported). Entities
// that already lack (RemoveFromAll) or already hold (AddToAll) the component are left alone. Return the number
// of entities changed. The world ends up as if the entities had been changed one by one in the order a query
// visits them: same values, same visiting order afterwards, same observer events in the same order. As with the
// command buffer's bulk removal, the events are raised after the whole change reached the native storage.
template <typename T>
std::size_t RemoveFromAll(const QueryFilter& filter = QueryFilter{});

template <typename T>
std::size_t AddToAll(const T& component, const QueryFilter& filter = QueryFilter{});

template <typename T>
void MarkModified(Entity entity);

template <typename T>
void ForEach(ConstComponentVisitor<T> visitor, void* context) const;

// Compatibility/debug iteration for single-component tools and tests. Production
// multi-component systems should use Query<T...> batch iteration to stay on the
// chunk hot path.
template <typename T>
[[deprecated("World::ForEachMutable is a compatibility/debug path; use Query<T...>::ForEachMutableBatch or UnsafeHotQuery in production systems.")]]
void ForEachMutable(MutableComponentVisitor<T> visitor, void* context);

template <typename TFirst, typename TSecond>
void ForEach(ConstComponentsVisitor<TFirst, TSecond> visitor, void* context) const;

template <typename T>
using ConstComponentVisitor = void (*)(Entity entity, const T& component, void* context);

template <typename T>
using MutableComponentVisitor = void (*)(Entity entity, T& component, void* context);

template <typename TFirst, typename TSecond>
using ConstComponentsVisitor = void (*)(Entity entity, const TFirst& first, const TSecond& second, void* context);

template <typename T>
[[nodiscard]] ComponentId RegisterComponent(std::string_view name = {});

template <typename T>
[[nodiscard]] ComponentId Component() const noexcept;

template <typename T>
[[nodiscard]] const ComponentReflection* RegisterComponentReflection(std::string_view name, std::initializer_list<ComponentFieldDesc> fields);

template <typename T>
[[nodiscard]] const ComponentReflection* Reflection() const noexcept;

[[nodiscard]] const ComponentReflection* Reflection(ComponentId componentId) const noexcept;
[[nodiscard]] const ComponentReflection* Reflection(std::string_view componentName) const noexcept;

template <typename T>
void Set(Entity entity, const T& component);

template <typename T>
[[nodiscard]] bool Has(Entity entity) const noexcept;

template <typename T>
[[nodiscard]] const T* TryGet(Entity entity) const noexcept;

template <typename T>
[[nodiscard]] T* TryGetMutable(Entity entity) noexcept;

template <typename T>
void Remove(Entity entity) noexcept;

template <typename T>
void MarkModified(Entity entity) noexcept;

template <typename T>
void ForEach(ConstComponentVisitor<T> visitor, void* context) const;

template <typename T>
void ForEachMutable(MutableComponentVisitor<T> visitor, void* context);

template <typename TFirst, typename TSecond>
void ForEach(ConstComponentsVisitor<TFirst, TSecond> visitor, void* context) const;

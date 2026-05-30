template <typename T>
[[nodiscard]] ObserverId ObserveComponent(ComponentEventKind event, ComponentEventVisitor<T> visitor, void* context = nullptr, bool yieldExisting = false);

void DestroyObserver(ObserverId observer) noexcept;

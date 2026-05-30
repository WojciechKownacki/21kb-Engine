[[nodiscard]] Entity CreateEntity();
[[nodiscard]] Entity CreateEntity(std::string_view name);

void DestroyEntity(Entity entity) noexcept;
void SetName(Entity entity, std::string_view name);

[[nodiscard]] bool IsAlive(Entity entity) const noexcept;
[[nodiscard]] std::string Name(Entity entity) const;

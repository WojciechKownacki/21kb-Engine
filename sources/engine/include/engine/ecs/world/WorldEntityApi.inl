[[nodiscard]] Entity CreateEntity();
[[nodiscard]] Entity CreateEntity(std::string_view name);

void DestroyEntity(Entity entity);
void DestroyEntities(std::span<const Entity> entities);
void SetName(Entity entity, std::string_view name);

[[nodiscard]] bool IsAlive(Entity entity) const noexcept;
[[nodiscard]] std::string Name(Entity entity) const;

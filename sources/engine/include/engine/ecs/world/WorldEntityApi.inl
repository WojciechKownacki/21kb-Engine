[[nodiscard]] Entity CreateEntity();
[[nodiscard]] Entity CreateEntity(std::string_view name);

void DestroyEntity(Entity entity) noexcept;

[[nodiscard]] bool IsAlive(Entity entity) const noexcept;
[[nodiscard]] std::string Name(Entity entity) const;

[[nodiscard]] bool Progress(float deltaSeconds);
void RequestQuit() noexcept;
[[nodiscard]] bool ShouldQuit() const noexcept;

[[nodiscard]] ecs_world_t* NativeHandle() noexcept;
[[nodiscard]] const ecs_world_t* NativeHandle() const noexcept;
[[nodiscard]] const WorldConfig& Config() const noexcept;

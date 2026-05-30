[[nodiscard]] EntityInspection InspectEntity(Entity entity) const;
[[nodiscard]] bool CaptureEditorWorld(EditorWorldInspection& output) const;
[[nodiscard]] WorldSnapshot CaptureSnapshot() const;
[[nodiscard]] bool SerializeComponent(Entity entity, ComponentId componentId, SerializedComponent& output) const;
[[nodiscard]] bool ApplySerializedComponent(Entity entity, const SerializedComponent& component);
[[nodiscard]] bool SerializeWorld(SerializedWorld& output) const;
[[nodiscard]] bool RestoreSerializedWorld(const SerializedWorld& source);

[[nodiscard]] EntityInspection InspectEntity(Entity entity) const;
[[nodiscard]] bool CaptureEditorWorld(EditorWorldInspection& output) const;
[[nodiscard]] WorldSnapshot CaptureSnapshot() const;
[[nodiscard]] ChunkedWorldSnapshot CaptureChunkedSnapshot() const;
[[nodiscard]] ChunkedWorldDeltaSnapshot CaptureChunkedDeltaSnapshot(const ChunkedWorldSnapshot& baseline) const;
[[nodiscard]] bool StreamChunkedSnapshot(
    ChunkedWorldSnapshotHeaderVisitor headerVisitor,
    ChunkedWorldSnapshotChunkVisitor chunkVisitor,
    void* context) const;
[[nodiscard]] bool SerializeComponent(Entity entity, ComponentId componentId, SerializedComponent& output) const;
[[nodiscard]] bool ApplySerializedComponent(Entity entity, const SerializedComponent& component);
[[nodiscard]] bool SerializeWorld(SerializedWorld& output) const;
[[nodiscard]] bool RestoreSerializedWorld(const SerializedWorld& source);
[[nodiscard]] bool SerializeChunkedSnapshotBinary(std::vector<std::byte>& output) const;
[[nodiscard]] bool RestoreChunkedSnapshotBinary(std::span<const std::byte> source);
[[nodiscard]] bool RestoreChunkedSnapshotStream(
    const ChunkedWorldSnapshotHeader& header,
    ChunkedWorldSnapshotChunkReader chunkReader,
    void* context);
[[nodiscard]] bool ApplyChunkedDeltaSnapshot(const ChunkedWorldDeltaSnapshot& delta);

template <typename T>
[[nodiscard]] TagId RegisterTag(std::string_view name = {});

template <typename T>
[[nodiscard]] TagId Tag() const noexcept;

template <typename T>
void AddTag(Entity entity);

template <typename T>
[[nodiscard]] bool HasTag(Entity entity) const noexcept;

template <typename T>
void RemoveTag(Entity entity) noexcept;

void AddTag(Entity entity, TagId tag) noexcept;
[[nodiscard]] bool HasTag(Entity entity, TagId tag) const noexcept;
void RemoveTag(Entity entity, TagId tag) noexcept;

template <typename T>
[[nodiscard]] RelationId RegisterRelation(std::string_view name = {});

template <typename T>
[[nodiscard]] RelationId Relation() const noexcept;

template <typename T>
void AddRelation(Entity entity, Entity target);

template <typename T>
[[nodiscard]] bool HasRelation(Entity entity, Entity target) const noexcept;

template <typename T>
void RemoveRelation(Entity entity, Entity target) noexcept;

template <typename T>
[[nodiscard]] Entity RelationTarget(Entity entity, int index = 0) const noexcept;

void AddRelation(Entity entity, RelationId relation, Entity target) noexcept;
[[nodiscard]] bool HasRelation(Entity entity, RelationId relation, Entity target) const noexcept;
void RemoveRelation(Entity entity, RelationId relation, Entity target) noexcept;
[[nodiscard]] Entity RelationTarget(Entity entity, RelationId relation, int index = 0) const noexcept;

void SetParent(Entity child, Entity parent) noexcept;
void ClearParent(Entity child) noexcept;
[[nodiscard]] Entity Parent(Entity child) const noexcept;

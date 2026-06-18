template <typename T>
[[nodiscard]] TagId RegisterTag(std::string_view name = {});

template <typename T>
[[nodiscard]] TagId Tag() const noexcept;

template <typename T>
void AddTag(Entity entity);

template <typename T>
[[nodiscard]] bool HasTag(Entity entity) const;

template <typename T>
void RemoveTag(Entity entity);

void AddTag(Entity entity, TagId tag);
[[nodiscard]] bool HasTag(Entity entity, TagId tag) const;
void RemoveTag(Entity entity, TagId tag);

template <typename T>
[[nodiscard]] RelationId RegisterRelation(std::string_view name = {});

template <typename T>
[[nodiscard]] RelationId Relation() const noexcept;

template <typename T>
void AddRelation(Entity entity, Entity target);

template <typename T>
[[nodiscard]] bool HasRelation(Entity entity, Entity target) const;

template <typename T>
void RemoveRelation(Entity entity, Entity target);

template <typename T>
[[nodiscard]] Entity RelationTarget(Entity entity, int index = 0) const;

void AddRelation(Entity entity, RelationId relation, Entity target);
[[nodiscard]] bool HasRelation(Entity entity, RelationId relation, Entity target) const;
void RemoveRelation(Entity entity, RelationId relation, Entity target);
[[nodiscard]] Entity RelationTarget(Entity entity, RelationId relation, int index = 0) const;

void SetParent(Entity child, Entity parent);
void SetParents(std::span<const Entity> children, std::span<const Entity> parents);
void ClearParent(Entity child);
void ClearParents(std::span<const Entity> children);
[[nodiscard]] Entity Parent(Entity child) const;
[[nodiscard]] std::vector<Entity> Children(Entity parent) const;

template <typename... Components>
[[nodiscard]] Query<Components...> CreateQuery();

template <typename... Components>
[[nodiscard]] Query<Components...> CreateQuery(const QueryFilter& filter);

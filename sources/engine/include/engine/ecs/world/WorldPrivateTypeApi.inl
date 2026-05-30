template <typename T>
static constexpr void ValidateTagType() noexcept;

template <typename T>
[[nodiscard]] static std::string_view DefaultTagName() noexcept;

[[nodiscard]] TagId RegisterTag(std::type_index type, std::string_view name);
[[nodiscard]] TagId FindTag(std::type_index type) const noexcept;
[[nodiscard]] RelationId RegisterRelation(std::type_index type, std::string_view name);
[[nodiscard]] RelationId FindRelation(std::type_index type) const noexcept;

#include <memory>
#include <type_traits>

template <typename T>
static void
free_ptr_list(T* head) {
	auto elem = head;
	while (*elem != nullptr) {
		free(*elem);
		elem++;
	}
	free(head);
}

template <typename T, typename Destr>
inline static auto
make_unique_destr(T*&& expiring, Destr&& destructor) -> std::unique_ptr<T, decltype(&destructor)>
{
	// type of Destr&& is deduced at the same time as Destr -> universal reference
	static_assert(!std::is_rvalue_reference<decltype(destructor)>::value);

	// type of T is deduced from T* first, then parameter as T*&& -> rvalue reference
	static_assert(std::is_rvalue_reference<decltype(expiring)>::value);

	return std::unique_ptr<T, decltype(&destructor)>
		{ std::move(expiring) // then we take ownership of the expiring raw pointer
		, destructor          // and merely capture a reference to the destructor
	};
}
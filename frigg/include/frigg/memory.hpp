
namespace frigg {
namespace memory {

// --------------------------------------------------------
// DebugAllocator declarations
// --------------------------------------------------------

enum {
	kPageSize = 0x1000
}; // TODO: allow different page sizes

template<typename VirtualAlloc>
class DebugAllocator {
public:
	struct Header {
		size_t numPages;
		uint8_t padding[32 - sizeof(size_t)];

		Header(size_t num_pages);
	};

	DebugAllocator(VirtualAlloc &virt_alloc);

	DebugAllocator(const DebugAllocator &) = delete;

	DebugAllocator &operator= (const DebugAllocator &) = delete;

	static_assert(sizeof(Header) == 32, "Header is not 32 bytes long");

	void *allocate(size_t length);
	void free(void *pointer);

private:
	VirtualAlloc &p_virtualAllocator;
};

// --------------------------------------------------------
// DebugAllocator::Header definitions
// --------------------------------------------------------

template<typename VirtualAlloc>
DebugAllocator<VirtualAlloc>::Header::Header(size_t num_pages)
: numPages(num_pages) { }

// --------------------------------------------------------
// DebugAllocator definitions
// --------------------------------------------------------

template<typename VirtualAlloc>
DebugAllocator<VirtualAlloc>::DebugAllocator(VirtualAlloc &virt_alloc)
: p_virtualAllocator(virt_alloc) { }

template<typename VirtualAlloc>
void *DebugAllocator<VirtualAlloc>::allocate(size_t length) {
	size_t with_header = length + sizeof(Header);

	size_t num_pages = with_header / kPageSize;
	if((with_header % kPageSize) != 0)
		num_pages++;

	uintptr_t pointer = p_virtualAllocator.map(num_pages * kPageSize);
	Header *header = (Header *)pointer;
	new (header) Header(num_pages);
	
	return (void *)(pointer + sizeof(Header));
}

template<typename VirtualAlloc>
void DebugAllocator<VirtualAlloc>::free(void *pointer) {
	if(pointer == nullptr)
		return;
	
	Header *header = (Header *)((uintptr_t)pointer - sizeof(Header));
	
	size_t num_pages = header->numPages;
	p_virtualAllocator.unmap((uintptr_t)header, num_pages * kPageSize);
}

// --------------------------------------------------------
// Namespace scope functions
// --------------------------------------------------------

template<typename T, typename Allocator, typename... Args>
T *construct(Allocator &allocator, Args &&... args) {
	void *pointer = allocator.allocate(sizeof(T));
	return new(pointer) T(traits::forward<Args>(args)...);
}
template<typename T, typename Allocator, typename... Args>
T *constructN(Allocator &allocator, size_t n, Args &&... args) {
	T *pointer = (T *)allocator.allocate(sizeof(T) * n);
	for(size_t i = 0; i < n; i++)
		new(&pointer[i]) T(traits::forward<Args>(args)...);
	return pointer;
}

template<typename T, typename Allocator>
void destruct(Allocator &allocator, T *pointer) {
	allocator.free(pointer);
}

} } // namespace frigg::memory

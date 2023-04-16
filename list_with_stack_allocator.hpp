#include <iostream>
#include <cstddef>
#include <iterator>

template <size_t N>
class alignas(std::max_align_t) StackStorage {
private:
char arr[N];
char *cur_ptr;

static size_t align(size_t n) {
    size_t alignment = alignof(std::max_align_t);
    n += alignment - 1;
    return n - n % alignment;
}
public:
    StackStorage() : cur_ptr(arr) {}

    StackStorage(const StackStorage&) = delete;

    StackStorage& operator=(const StackStorage&) = delete;

    char* allocate(size_t n) {
        n = align(n);
        char* old = cur_ptr;
        cur_ptr += n;
        return old;
    }

    void deallocate(char*, size_t) {}
};


template <typename T, size_t N>
class StackAllocator {
public:
    using value_type = T;

    using storage_type = StackStorage<N>;

    template <typename U>
    struct rebind {
        using other = StackAllocator<U, N>;
    };

    template <typename U, size_t M>
    friend class StackAllocator;

private:
    storage_type* storage;

public:
    explicit StackAllocator(storage_type& storage) : storage(&storage) {}

    template <typename U>
    StackAllocator(const StackAllocator<U, N>& alloc) : storage(alloc.storage) {}

    T* allocate(size_t n) {
        return reinterpret_cast<T*>(storage->allocate(n * sizeof(T)));
    }

    void deallocate(T* ptr, size_t n) {
        storage->deallocate(reinterpret_cast<char*>(ptr), n * sizeof(T));
    }

    ~StackAllocator() {}
};

template<typename T, size_t N>
bool operator==(const StackAllocator<T, N>& first, const StackAllocator<T, N>& second) {
    return first.storage == second.storage;
}

template<typename T, size_t N>
bool operator!=(const StackAllocator<T, N>& first, const StackAllocator<T, N>& second) {
    return !(first == second);
}

template <typename T, typename Allocator = std::allocator<T>>
class List {
private:

    template <typename Key,
            typename Value,
            typename Hash,
            typename Equal,
            typename Allocat>
    friend class UnorderedMap;

    struct BaseNode {
        BaseNode* prev;
        BaseNode* next;

        BaseNode() : prev(this), next(this) {}
    };

    struct Node : BaseNode {
        T val;

        Node() = default;

        Node(const T& val) : val(val) {}
    };

    BaseNode fake_node;
    size_t sz;

    using AllocTraits = std::allocator_traits<typename std::allocator_traits<Allocator>::template rebind_alloc<Node>>;
    using alloc_type = typename AllocTraits::allocator_type;
    typename AllocTraits::allocator_type alloc;

    void copy(const List& other) {
        BaseNode* new_cur = &fake_node;
        BaseNode* cur = const_cast<BaseNode*>(&other.fake_node);
        size_t j = 0;
        try {
            for (j = 0; j < sz; ++j) {
                insert_node(new_cur, static_cast<Node*>(cur->next)->val);
                new_cur = new_cur->next;
                cur = cur->next;
            }
        } catch (...) {
            partial_destroy(j, false);
            throw;
        }
        new_cur->next = &fake_node;
        fake_node.prev = new_cur;
    }

    template <typename... Args>
    void insert_node(BaseNode* cur_node, const Args&... args) {
        Node* new_node = AllocTraits::allocate(alloc, 1);
        cur_node->next = new_node;
        AllocTraits::construct(alloc, new_node, args...);
        new_node->prev = cur_node;
    }

    void partial_destroy(size_t end, bool is_remove_end) {
        sz = 0;
        BaseNode* cur_node = fake_node.next;
        for (size_t i = 0; i < end; ++i) {
            BaseNode* next = cur_node->next;
            AllocTraits::destroy(alloc, static_cast<Node*>(cur_node));
            AllocTraits::deallocate(alloc, static_cast<Node*>(cur_node), 1);
            cur_node = next;
        }
        if (is_remove_end)
            AllocTraits::deallocate(alloc, static_cast<Node*>(cur_node), 1);
    }

    template <typename... Args>
    explicit List(const Allocator& _alloc, size_t sz, Args... args)
            : fake_node(BaseNode()), sz(sz), alloc(_alloc) {
        BaseNode* cur_node = &fake_node;
        size_t j = 0;
        try {
            for (j = 0; j < sz; ++j) {
                insert_node(cur_node, args...);
                cur_node = cur_node->next;
            }
        } catch (...) {
            partial_destroy(j, true);
            throw;
        }
    }

    template <bool is_const>
    struct common_iterator {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = std::conditional_t<is_const, const T, T>;
        using pointer = value_type*;
        using reference = value_type&;
        using iterator_category = std::bidirectional_iterator_tag;

        using base_node_type = std::conditional_t<is_const, const BaseNode, BaseNode>;
        using node_type = std::conditional_t<is_const, const Node, Node>;

    private:
        base_node_type* ptr;

        base_node_type* get_pointer() {
            return ptr;
        }

        explicit common_iterator(base_node_type* ptr) : ptr(ptr) {}

        friend List;

    public:
        common_iterator() : ptr(&fake_node) {}

        common_iterator(const common_iterator<false>& other) : ptr(other.ptr) {}

        common_iterator& operator=(const common_iterator& other) {
            ptr = other.ptr;
            return *this;
        }

        ~common_iterator() {}

        value_type& operator*() {
            return static_cast<node_type*>(ptr)->val;
        }

        value_type* operator->() {
            return &(static_cast<node_type*>(ptr)->val);
        }

        common_iterator& operator++() {
            ptr = ptr->next;
            return *this;
        }

        common_iterator operator++(int) {
            common_iterator temp = *this;
            ptr = ptr->next;
            return temp;
        }

        common_iterator& operator--() {
            ptr = ptr->prev;
            return *this;
        }

        common_iterator operator--(int) {
            common_iterator temp = *this;
            ptr = ptr->prev;
            return temp;
        }

        bool operator==(const common_iterator& other) {
            return ptr == other.ptr;
        }

        bool operator!=(const common_iterator& other) {
            return !(ptr == other.ptr);
        }
    };

public:
    using iterator = common_iterator<false>;
    using const_iterator = common_iterator<true>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    List() : fake_node(BaseNode()), sz(0), alloc(alloc_type()) {}

    explicit List(const Allocator& _alloc) : fake_node(BaseNode()), sz(0), alloc(_alloc) {}

    explicit List(size_t sz) : List(alloc_type(), sz) {}

    explicit List(size_t sz, const Allocator& _alloc) : List(_alloc, sz) {}

    explicit List(size_t sz, const T& val) : List(alloc_type(), sz, val) {}

    explicit List(size_t sz, const T& val, const Allocator& _alloc) : List(_alloc, sz, val) {}

    List(const List& other) :
            fake_node(BaseNode()),
            sz(other.sz),
            alloc(AllocTraits::select_on_container_copy_construction(other.alloc)) {
        copy(other);
    }

    void swap(List& other) {
        std::swap(fake_node, other.fake_node);
        std::swap(sz, other.sz);
        std::swap(alloc, other.alloc);
    }

    // List& operator=(const List& other) {
    //     auto al = alloc;
    //     if (AllocTraits::propagate_on_container_copy_assignment::value) {
    //         al = other.alloc;
    //     }
    //     List other_copy = other;
    //     other_copy.swap(*this);
    //     alloc = al;
    //     other_copy.alloc = other.alloc;
    //     return *this;
    // }

    List& operator=(const List& other) {
        Allocator al = alloc;
        if (AllocTraits::propagate_on_container_copy_assignment::value) {
            al = other.alloc;
        }
        List copy(al);
        copy.sz = other.sz;
        copy.copy(other);
        copy.swap(*this);
        return *this;
    }

    Allocator get_allocator() const {
        return Allocator(alloc);
    }

    ~List() {
        if (sz == 0) return;
        BaseNode* cur = fake_node.next;
        for (size_t i = 0; i < sz; ++i) {
            BaseNode* nxt = cur->next;
            AllocTraits::destroy(alloc, static_cast<Node*>(cur));
            AllocTraits::deallocate(alloc, static_cast<Node*>(cur), 1);
            cur = nxt;
        }
    }

    size_t size() const {
        return sz;
    }

    iterator begin() {
        return iterator(fake_node.next);
    }

    const_iterator begin() const {
        return const_iterator(fake_node.next);
    }

    iterator end() {
        return iterator(&fake_node);
    }

    const_iterator end() const {
        return const_iterator(&fake_node);
    }

    reverse_iterator rbegin() {
        return reverse_iterator(end());
    }

    const_reverse_iterator rbegin() const {
        return const_reverse_iterator(end());
    }

    reverse_iterator rend() {
        return reverse_iterator(begin());
    }

    const_reverse_iterator rend() const {
        return const_reverse_iterator(begin());
    }

    const_iterator cbegin() const {
        return const_iterator(fake_node.next);
    }

    const_iterator cend() const {
        return const_iterator(&fake_node);
    }

    const_reverse_iterator crbegin() const {
        return const_reverse_iterator(cend());
    }

    const_reverse_iterator crend() const {
        return const_reverse_iterator(cbegin());
    }

private:
    template <bool is_const>
    std::conditional_t<is_const, const_iterator, iterator> insert(std::conditional_t<is_const, const_iterator, iterator> pos, const T& val) {
        sz++;
        Node* new_node = AllocTraits::allocate(alloc, 1);
        try {
            AllocTraits::construct(alloc, new_node, val);
        } catch (...) {
            AllocTraits::deallocate(alloc, new_node, 1);
            throw;
        }
        BaseNode* next = const_cast<BaseNode*>(pos.get_pointer());
        BaseNode* prev = next->prev;
        new_node->prev = prev;
        new_node->next = next;
        next->prev = new_node;
        prev->next = new_node;
        return std::conditional_t<is_const, const_iterator, iterator>(new_node);
    }

    template <bool is_const>
    std::conditional_t<is_const, const_iterator, iterator> erase(std::conditional_t<is_const, const_iterator, iterator> pos) {
        sz--;
        BaseNode* cur = const_cast<BaseNode*>(pos.get_pointer());
        BaseNode* prev = cur->prev;
        BaseNode* next = cur->next;
        prev->next = next;
        next->prev = prev;
        BaseNode* result = next;
        AllocTraits::destroy(alloc, static_cast<Node*>(cur));
        AllocTraits::deallocate(alloc, static_cast<Node*>(cur), 1);
        return std::conditional_t<is_const, const_iterator, iterator>(result);
    }

public:
    iterator insert(iterator pos, const T& val) {
        return insert<false>(pos, val);
    }

    const_iterator insert(const_iterator pos, const T& val) {
        return insert<true>(pos, val);
    }

    iterator erase(iterator pos) {
        return erase<false>(pos);
    }

    const_iterator erase(const_iterator pos) {
        return erase<true>(pos);
    }

    void push_back(const T& val) {
        insert(end(), val);
    }

    void pop_back() {
        erase(--end());
    }

    void push_front(const T& val) {
        insert(begin(), val);
    }

    void pop_front() {
        erase(begin());
    }
};

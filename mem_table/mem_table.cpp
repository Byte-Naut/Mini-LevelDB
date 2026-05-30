//
// Created by ljyay on 2026/2/20.
//
#include <optional>

#include "./includes/mem_table.h"
#include "../db/includes/coding.h"

mem_table::mem_table()
{
    auto raw_mem = alloc_.allocate_bytes(sizeof(Node), alignof(Node));
    head_ = new (raw_mem) Node(max_level_ + 1, "", 0, OperationType::kValue, "", alloc_);
}

mem_table::~mem_table()
{
    auto current = head_;
    while (current != nullptr)
    {
        const auto next = current->forward_[0];
        std::destroy_at(current); // 显式调用虚构函数，内存属于 arena_，严禁使用 delete
        current = next;
    }
}

mem_table::skip_list_iterator::skip_list_iterator(Node* current): current_(current){}

mem_table::skip_list_iterator::reference mem_table::skip_list_iterator::operator*() const
{
    return current_->decode();
}

std::span<const std::byte> mem_table::skip_list_iterator::raw_payload() const
{
    return current_->payload_;
}

mem_table::skip_list_iterator& mem_table::skip_list_iterator::operator++()
{
    current_ = current_->forward_[0];
    return *this;
}

mem_table::skip_list_iterator mem_table::skip_list_iterator::operator++(int)
{
    const auto tmp = *this;
    ++(*this);
    return tmp;
}

bool mem_table::skip_list_iterator::operator==(const skip_list_iterator&) const = default;

mem_table::skip_list_iterator mem_table::begin() const
{
    return skip_list_iterator{head_->forward_[0]};
}

mem_table::skip_list_iterator mem_table::end() const
{
    return skip_list_iterator{nullptr};
}

mem_table::const_iterator mem_table::cbegin() const
{
    return skip_list_iterator{head_->forward_[0]};
}

SearchResult mem_table::get(const std::string_view key) const
{
    auto current = head_;
    for(auto i = current_level_; i >= 0; i--)
    {
        while(current -> forward_[i] != nullptr) {
            auto next_view = current -> forward_[i] -> decode();
            const auto cmp = next_view.key.compare(key);
            if (cmp < 0)
            {
                current = current -> forward_[i];
            }
            else
            {
                break;
            }
        }
    }

    current = current -> forward_[0];

    if (current != nullptr)
    {
        auto view = current->decode();
        if (view.key == key)
        {
            if (view.type == OperationType::kDeletion)
            {
                return {SearchStatus::kFoundDeletion, ""};
            }
            return {SearchStatus::kFoundValue, std::string(view.value)};
        }
    }

    return {SearchStatus::kNotFound, ""};
}

void mem_table::insert_record(std::string_view key, uint64_t seq, std::string_view value)
{
    internal_put(key, seq, OperationType::kValue, value);
}

void mem_table::insert_tombstone(const std::string_view key, const uint64_t seq)
{
    internal_put(key, seq, OperationType::kDeletion, "");
}

size_t mem_table::approximate_memory_usage() const
{
    return usage_;
}

mem_table::Node::Node(const int level, const std::string_view key, const uint64_t seq, OperationType type,
                        const std::string_view value, const std::pmr::polymorphic_allocator<> alloc):
    payload_(alloc), forward_(level+1, nullptr, alloc)
{
    constexpr auto length_prefix_size = sizeof(uint32_t);
    const uint32_t key_size = key.size();
    constexpr auto pack_size = sizeof(uint64_t);
    const uint32_t internal_key_size = key.size() + pack_size;
    const uint32_t value_size = value.size();
    payload_.reserve(length_prefix_size + key_size + pack_size + length_prefix_size + value_size);

    auto append_bytes = [&](auto&& arr)
    {
        payload_.insert(payload_.end(), arr.begin(), arr.end());
    };
    auto append_str = [&](std::string_view str)
    {
        auto bytes = std::as_bytes(std::span{str});
        payload_.insert(payload_.end(), bytes.begin(), bytes.end());
    };
    // 执行类型安全的位压缩

    /*
     * 内存映像 (payload_):
     * payload_
     *    |--key_scope: Unknown
     *    |      |--internal_key_size(uint32): 4B
     *    |      |--internal_key: Unknown
     *    |           |--key(string): Unknown
     *    |           |--pack(uint64): 8B
     *    |               |--seq(uint56): 7B
     *    |               |--type(uint8): 1B
     *    |--value_scope: Unknown
     *         |--value_size(uint32): 4B
     *         |--value(string): Unknown
     */
    append_bytes(coding::encode_fixed(internal_key_size));
    append_str(key);
    const uint64_t pack = (seq << 8) | static_cast<uint8_t>(type);
    append_bytes(coding::encode_fixed(pack));
    append_bytes(coding::encode_fixed(value_size));
    append_str(value);
}

ParsedRecordView mem_table::Node::decode() const
{
    /*
     * 内存映像 (payload_):
     * payload_
     *    |--key_scope: Unknown
     *    |      |--internal_key_size(uint32): 4B
     *    |      |--internal_key: Unknown
     *    |           |--key(string): Unknown
     *    |           |--pack(uint64): 8B
     *    |               |--seq(uint56): 7B
     *    |               |--type(uint8): 1B
     *    |--value_scope: Unknown
     *         |--value_size(uint32): 4B
     *         |--value(string): Unknown
     */
    const std::span buffer{payload_};
    auto offset = 0u;

    const auto internal_key_size = coding::decode_fixed<uint32_t>(buffer.subspan<0, sizeof(uint32_t)>());
    offset += sizeof(uint32_t);

    const auto key_size = internal_key_size - sizeof(uint64_t);
    const auto key_span = buffer.subspan(offset, key_size);
    const std::string_view key(reinterpret_cast<const char*>(key_span.data()), key_size);
    offset += key_size;

    const auto pack = coding::decode_fixed<uint64_t>(buffer.subspan(offset).first<sizeof(uint64_t)>());
    offset += sizeof(uint64_t);
    const uint64_t seq = pack >> 8;
    const auto type = static_cast<OperationType>(pack & 0xFF);

    const auto value_size = coding::decode_fixed<uint32_t>(buffer.subspan(offset).first<sizeof(uint32_t)>());
    offset += sizeof(uint32_t);

    const auto value_span = buffer.subspan(offset, value_size);
    const std::string_view value(reinterpret_cast<const char*>(value_span.data()), value_size);

    return {key, seq, type, value};
}

void mem_table::internal_put(const std::string_view key, const uint64_t seq,
                               const OperationType type, const std::string_view value)
{
    std::pmr::vector<Node*> update (max_level_+1, head_, alloc_);
    auto current = head_;
    for(auto i=current_level_; i>=0; --i)
    {
        while(current -> forward_[i] != nullptr)
        {
            auto next_view = current -> forward_[i] -> decode();
            const auto cmp = next_view.key.compare(key);
            if (cmp < 0 || (cmp ==0 && next_view.seq > seq))
            {
                current = current->forward_[i];
            }
            else
            {
                break;
            }
        }
        update[i] = current;
    }
    auto new_level = 0;
    for(;new_level < max_level_ && get_random() < probability_; ++new_level) {}
    auto raw_mem = alloc_.allocate_bytes(sizeof(Node), alignof(Node));
    const auto current_node = new (raw_mem) Node(new_level+1, key, seq, type, value, alloc_);

    const size_t node_overhead = sizeof(Node) + (current_node->forward_.capacity() * sizeof(Node*));
    const size_t data_size = current_node->payload_.capacity();
    usage_ += (node_overhead + data_size);

    for(auto i=new_level; i>=0; i--) {
        current_node -> forward_[i] = update[i] -> forward_[i];
        update[i] -> forward_[i] = current_node;
    }
    if (new_level > current_level_)
        current_level_ = new_level;
}

double mem_table::get_random()
{
    return dist_(eng_) / 10.0;
}



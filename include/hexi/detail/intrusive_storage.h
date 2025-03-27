//  _               _ 
// | |__   _____  _(_)
// | '_ \ / _ \ \/ / | MIT & Apache 2.0 dual licensed
// | | | |  __/>  <| | Version 1.0
// |_| |_|\___/_/\_\_| https://github.com/EmberEmu/hexi

#pragma once

#if _MSC_VER
#pragma warning(disable : 4996)
#endif

#include <hexi/shared.h>
#include <hexi/concepts.h>
#include <array>
#include <concepts>
#include <type_traits>
#include <cassert>
#include <cstring>
#include <cstddef>

namespace hexi::detail {

struct intrusive_node {
	intrusive_node* next;
	intrusive_node* prev;
};

template<std::size_t block_size, byte_type storage_type = std::byte>
struct intrusive_storage final {
	using value_type = storage_type;
	using OffsetType = std::remove_const_t<decltype(block_size)>;

	OffsetType read_offset = 0;
	OffsetType write_offset = 0;
	intrusive_node node {};
	std::array<value_type, block_size> storage;

	/**
	 * @brief Clear the container.
	 * 
	 * Resets the read and write offsets, does not explicitly
	 * erase the data contained within the buffer but should
	 * be treated as such.
	 */
	void clear() {
		read_offset = 0;
		write_offset = 0;
	}

	/**
	 * @brief Write provided data to the container.
	 * 
	 * If the container size is lower than requested number of bytes,
	 * the request will be capped at the number of bytes available.
	 * 
	 * @param source Pointer to the data to be written.
	 * @param length Number of bytes to write from the source.
	 * 
	 * @return The number of bytes copied, which may be less than requested.
	 */
	std::size_t write(const auto source, std::size_t length) {
		assert(!region_overlap(source, length, storage.data(), storage.size()));
		std::size_t write_len = block_size - write_offset;

		if(write_len > length) {
			write_len = length;
		}

		std::memcpy(storage.data() + write_offset, source, write_len);
		write_offset += static_cast<OffsetType>(write_len);
		return write_len;
	}

	/**
	 * @brief Copies a number of bytes to the provided buffer but without
	 * advancing the read cursor.
	 * 
	 * If the container size is lower than requested number of bytes,
	 * the request will be capped at the number of bytes available.
	 * 
	 * @param destination The buffer to copy the data to.
	 * @param length The number of bytes to copy.
	 * 
	 * @return The number of bytes copied, which may be less than requested.
	 */
	std::size_t copy(auto destination, const std::size_t length) const {
		assert(!region_overlap(storage.data(), storage.size(), destination, length));
		std::size_t read_len = block_size - read_offset;

		if(read_len > length) {
			read_len = length;
		}

		std::memcpy(destination, storage.data() + read_offset, read_len);
		return read_len;
	}

	/**
	* @brief Reads a number of bytes to the provided buffer.
	* 
	* If the container size is lower than requested number of bytes,
	* the request will be capped at the number of bytes available.
	* 
	* @param length The number of bytes to skip.
	* @param destination The buffer to copy the data to.
	* @param allow_optimise Whether the buffer can reuse its space where possible.
	* 
	* @return The number of bytes read, which may be less than requested.
	*/
	std::size_t read(auto destination, const std::size_t length, const bool allow_optimise = false) {
		std::size_t read_len = copy(destination, length);
		read_offset += static_cast<OffsetType>(read_len);

		if(read_offset == write_offset && allow_optimise) {
			clear();
		}

		return read_len;
	}

	/**
	* @brief Skip over requested number of bytes.
	*
	* Skips over a number of bytes from the stream. This should be used
	* if the stream holds data that you don't care about but don't want
	* to have to read it to another buffer to move beyond it.
	* 
	* If the container size is lower than requested number of bytes,
	* the request will be capped at the number of bytes available.
	* 
	* @param length The number of bytes to skip.
	* @param allow_optimise Whether the buffer can reuse its space where possible.
	* 
	* @return The number of bytes skipped, which may be less than requested.
	*/
	std::size_t skip(const std::size_t length, const bool allow_optimise = false) {
		std::size_t skip_len = block_size - read_offset;

		if(skip_len > length) {
			skip_len = length;
		}

		read_offset += static_cast<OffsetType>(skip_len);

		if(read_offset == write_offset && allow_optimise) {
			clear();
		}

		return skip_len;
	}

	/**
	 * @brief Returns the size of the container.
	 * 
	 * @return The number of bytes of data available to read within the stream.
	 */
	std::size_t size() const {
		return write_offset - read_offset;
	}

	/**
	 * @brief The amount of free space.
	 * 
	 * @return The number of bytes of free space within the container.
	 */
	std::size_t free() const {
		return block_size - write_offset;
	}

	/**
	 * @brief Performs write seeking within the container.
	 * 
	 * @param direction Specify whether to seek in a given direction or to absolute seek.
	 * @param offset The offset relative to the seek direction or the absolute value
	 * when using absolute seeking.
	 */
	void write_seek(const buffer_seek direction, const std::size_t offset) {
		switch(direction) {
			case buffer_seek::sk_absolute:
				write_offset = offset;
				break;
			case buffer_seek::sk_backward:
				write_offset -= static_cast<OffsetType>(offset);
				break;
			case buffer_seek::sk_forward:
				write_offset += static_cast<OffsetType>(offset);
				break;
		}
	}

	/**
	 * @brief Advances the write cursor.
	 * 
	 * @param size The number of bytes by which to advance the write cursor.
	 */
	std::size_t advance_write(std::size_t size) {
		const auto remaining = free();

		if(remaining < size) {
			size = remaining;
		}

		write_offset += static_cast<OffsetType>(size);
		return size;
	}

	/**
	 * @brief Retrieve a pointer to the readable portion of the buffer.
	 * 
	 * @return Pointer to the reable portion of the buffer.
	 */
	const value_type* read_data() const {
		return storage.data() + read_offset;
	}

	/**
	 * @brief Retrieve a pointer to the readable portion of the buffer.
	 * 
	 * @return Pointer to the reable portion of the buffer.
	 */
	value_type* read_data() {
		return storage.data() + read_offset;
	}

	/**
	 * @brief Retrieve a pointer to the writeable portion of the buffer.
	 * 
	 * @return Pointer to the writeable portion of the buffer.
	 */
	const value_type* write_data() const {
		return storage.data() + write_offset;
	}

	/**
	* @brief Retrieve a pointer to the writeable portion of the buffer.
	* 
	* @return Pointer to the writeable portion of the buffer.
	*/
	value_type* write_data() {
		return storage.data() + write_offset;
	}

	/**
	 * @brief Retrieves a reference to the specified index within the container.
	 * 
	 * @param index The index within the container.
	 * 
	 * @return A reference to the value at the specified index.
	 */
	value_type& operator[](const std::size_t index) {
		return *(storage.data() + index);
	}

	/**
	 * @brief Retrieves a reference to the specified index within the container.
	 * 
	 * @param index The index within the container.
	 * 
	 * @return A reference to the value at the specified index.
	 */
	const value_type& operator[](const std::size_t index) const {
		return *(storage.data() + index);
	}
};

} // detail, hexi

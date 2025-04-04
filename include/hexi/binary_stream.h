//  _               _ 
// | |__   _____  _(_)
// | '_ \ / _ \ \/ / | MIT & Apache 2.0 dual licensed
// | | | |  __/>  <| | Version 1.0
// |_| |_|\___/_/\_\_| https://github.com/EmberEmu/hexi

#pragma once

#include <hexi/shared.h>
#include <hexi/concepts.h>
#include <hexi/exception.h>
#include <hexi/endian.h>
#include <algorithm>
#include <concepts>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hexi {

using namespace detail;

#define STREAM_READ_BOUNDS_CHECK(read_size, ret_var)              \
	check_read_bounds(read_size);                                 \
	                                                              \
	if constexpr(std::is_same_v<exceptions, no_throw>) {          \
		if(state_ != stream_state::ok) [[unlikely]] {             \
			return ret_var;                                       \
		}                                                         \
	}

template<byte_oriented buf_type, std::derived_from<except_tag> exceptions = allow_throw>
class binary_stream final {
public:
	using size_type          = typename buf_type::size_type;
	using offset_type        = typename buf_type::offset_type;
	using seeking            = typename buf_type::seeking;
	using value_type         = typename buf_type::value_type;
	using contiguous_type    = typename buf_type::contiguous;
	
private:
	using cond_size_type = std::conditional_t<writeable<buf_type>, size_type, std::monostate>;

	buf_type& buffer_;
	[[no_unique_address]] cond_size_type total_write_{};
	size_type total_read_ = 0;
	stream_state state_ = stream_state::ok;
	const size_type read_limit_;

	inline void check_read_bounds(const size_type read_size) {
		if(read_size > buffer_.size()) [[unlikely]] {
			state_ = stream_state::buff_limit_err;

			if constexpr(std::is_same_v<exceptions, allow_throw>) {
				throw buffer_underrun(read_size, total_read_, buffer_.size());
			}

			return;
		}

		const auto max_read_remaining = read_limit_ - total_read_;

		if(read_limit_ && read_size > max_read_remaining) [[unlikely]] {
			state_ = stream_state::read_limit_err;

			if constexpr(std::is_same_v<exceptions, allow_throw>) {
				throw stream_read_limit(read_size, total_read_, read_limit_);
			}

			return;
		}

		total_read_ += read_size;
	}

public:
	explicit binary_stream(buf_type& source, size_type read_limit = 0)
		: buffer_(source),
		  read_limit_(read_limit) {};

	binary_stream(binary_stream&& rhs) noexcept
		: buffer_(rhs.buffer_), 
		  total_write_(rhs.total_write_),
		  total_read_(rhs.total_read_),
		  state_(rhs.state_),
		  read_limit_(rhs.read_limit_) {
		rhs.total_read_ = static_cast<size_type>(-1);
		rhs.state_ = stream_state::invalid_stream;
	}

	binary_stream& operator=(binary_stream&&) = delete;
	binary_stream& operator=(binary_stream&) = delete;
	binary_stream(binary_stream&) = delete;

	/*** Write ***/

	binary_stream& operator <<(has_shl_override<binary_stream> auto&& data)
	requires(writeable<buf_type>) {
		return data.operator<<(*this);
	}

	template<pod T>
	requires (!has_shl_override<T, binary_stream>)
	binary_stream& operator <<(const T& data) requires(writeable<buf_type>) {
		buffer_.write(&data, sizeof(T));
		total_write_ += sizeof(T);
		return *this;
	}

	binary_stream& operator <<(const std::string& data) requires(writeable<buf_type>) {
		buffer_.write(data.data(), data.size() + 1); // +1 also writes terminator
		total_write_ += (data.size() + 1);
		return *this;
	}

	binary_stream& operator <<(const char* data) requires(writeable<buf_type>) {
		assert(data);
		const auto len = std::strlen(data);
		buffer_.write(data, len + 1); // include terminator
		total_write_ += len + 1;
		return *this;
	}

	binary_stream& operator <<(std::string_view& data) requires(writeable<buf_type>) {
		buffer_.write(data.data(), data.size());
		const char term = '\0';
		buffer_.write(&term, sizeof(term));
		total_write_ += (data.size() + 1);
		return *this;
	}

	/**
	 * @brief Writes a contiguous range to the stream.
	 * 
	 * @param data The contiguous range to be written to the stream.
	 */
	template<std::ranges::contiguous_range range>
	void put(const range& data) requires(writeable<buf_type>) {
		const auto write_size = data.size() * sizeof(typename range::value_type);
		buffer_.write(data.data(), write_size);
		total_write_ += write_size;
	}

	/**
	 * @brief Writes a the provided value to the stream.
	 * 
	 * @param data The value to be written to the stream.
	 */
	void put(const arithmetic auto& data) requires(writeable<buf_type>) {
		buffer_.write(&data, sizeof(data));
		total_write_ += sizeof(data);
	}

	/**
	 * @brief Writes data to the stream.
	 * 
	 * @param data The element to be written to the stream.
	 */
	template<endian::conversion conversion>
	void put(const arithmetic auto& data) requires(writeable<buf_type>) {
		const auto swapped = endian::convert<conversion>(data);
		buffer_.write(&swapped, sizeof(data));
		total_write_ += sizeof(data);
	}

	/**
	 * @brief Writes count elements from the provided buffer to the stream.
	 * 
	 * @param data Pointer to the buffer from which data will be copied to the stream.
	 * @param count The number of elements to write.
	 */
	template<pod T>
	void put(const T* data, size_type count) requires(writeable<buf_type>) {
		const auto write_size = count * sizeof(T);
		buffer_.write(data, write_size);
		total_write_ += write_size;
	}

	/**
	 * @brief Writes the data from the iterator range to the stream.
	 * 
	 * @param begin Iterator to the beginning of the data.
	 * @param end Iterator to the end of the data.
	 */
	template<typename It>
	void put(It begin, const It end) requires(writeable<buf_type>) {
		for(auto it = begin; it != end; ++it) {
			*this << *it;
		}
	}

	/**
	 * @brief Allows for writing a provided byte value a specified number of times to
	 * the stream.
	 * 
	 * @param The byte value that will fill the specified number of bytes.
	 */
	template<size_type size>
	void fill(const std::uint8_t value) requires(writeable<buf_type>) {
		const auto filled = generate_filled<size>(value);
		buffer_.write(filled.data(), filled.size());
		total_write_ += size;
	}

	/*** Read ***/

	// terminates when it hits a null byte, empty string if none found
	binary_stream& operator>>(std::string& dest) {
		auto pos = buffer_.find_first_of(value_type(0));

		if(pos == buf_type::npos) {
			dest.clear();
			return *this;
		}

		dest.resize_and_overwrite(pos, [&](char* strbuf, size_type size) {
			buffer_.read(strbuf, size);
			total_read_ += size;
			return size;
		});

		total_read_ += 1;
		buffer_.skip(1); // skip null term
		return *this;
	}

	// terminates when it hits a null byte, empty string_view if none found
	// goes without saying that the buffer must outlive the string_view
	binary_stream& operator>>(std::string_view& dest) requires(contiguous<buf_type>) {
		dest = view();
		return *this;
	}

	binary_stream& operator>>(has_shr_override<binary_stream> auto&& data) {
		return data.operator>>(*this);
	}

	template<pod T>
	requires (!has_shr_override<T, binary_stream>)
	binary_stream& operator>>(T& data) {
		STREAM_READ_BOUNDS_CHECK(sizeof(data), *this);
		buffer_.read(&data, sizeof(data));
		return *this;
	}

	/**
	 * @brief Read an arithmetic type from the stream.
	 * 
	 * @return The destination for the read value.
	 */
	void get(arithmetic auto& dest) {
		STREAM_READ_BOUNDS_CHECK(sizeof(dest), void());
		buffer_.read(&dest, sizeof(dest));
	}

	/**
	 * @brief Read an arithmetic type from the stream.
	 * 
	 * @return The arithmetic value.
	 */
	template<arithmetic T>
	T get() {
		STREAM_READ_BOUNDS_CHECK(sizeof(T), void());
		T t{};
		buffer_.read(&t, sizeof(T));
		return t;
	}

	/**
	 * @brief Read an arithmetic type from the stream, allowing for endian
	 * conversion.
	 * 
	 * @param The destination for the read value.
	 */
	template<endian::conversion conversion>
	void get(arithmetic auto& dest) {
		STREAM_READ_BOUNDS_CHECK(sizeof(dest), void());
		buffer_.read(&dest, sizeof(dest));
		dest = endian::convert<conversion>(dest);
	}

	/**
	 * @brief Read an arithmetic type from the stream, allowing for endian
	 * conversion.
	 * 
	 * @return The arithmetic value.
	 */
	template<arithmetic T, endian::conversion conversion>
	T get() {
		STREAM_READ_BOUNDS_CHECK(sizeof(T), void());
		T t{};
		buffer_.read(&t, sizeof(T));
		return endian::convert<conversion>(t);
	}


	/**
	 * @brief Reads a string from the stream.
	 * 
	 * @param dest The destination string.
	 */
	void get(std::string& dest) {
		*this >> dest;
	}

	/**
	 * @brief Reads a fixed-length string from the stream.
	 * 
	 * @param dest The destination string.
	 * @param count The number of bytes to be read.
	 */
	void get(std::string& dest, size_type size) {
		STREAM_READ_BOUNDS_CHECK(size, void());
		dest.resize_and_overwrite(size, [&](char* strbuf, size_type len) {
			buffer_.read(strbuf, len);
			return len;
		});
	}

	/**
	 * @brief Read data from the stream into the provided destination argument.
	 * 
	 * @param dest The destination buffer.
	 * @param count The number of bytes to be read into the destination.
	 */
	template<typename T>
	void get(T* dest, size_type count) {
		assert(dest);
		const auto read_size = count * sizeof(T);
		STREAM_READ_BOUNDS_CHECK(read_size, void());
		buffer_.read(dest, read_size);
	}

	/**
	 * @brief Read data from the stream to the destination represented by the iterators.
	 * 
	 * @param begin The beginning iterator.
	 * @param end The end iterator.
	 */
	template<typename It>
	void get(It begin, const It end) {
		for(; begin != end; ++begin) {
			*this >> *begin;
		}
	}

	/**
	 * @brief Read data from the stream into the provided destination argument.
	 * 
	 * @param dest A contiguous range into which the data should be read.
	 */
	template<std::ranges::contiguous_range range>
	void get(range& dest) {
		const auto read_size = dest.size() * sizeof(range::value_type);
		STREAM_READ_BOUNDS_CHECK(read_size, void());
		buffer_.read(dest.data(), read_size);
	}

	/**
	 * @brief Skip over count bytes
	 *
	 * Skips over a number of bytes from the stream. This should be used
	 * if the stream holds data that you don't care about but don't want
	 * to have to read it to another buffer to move beyond it.
	 * 
	 * @param length The number of bytes to skip.
	 */
	void skip(const size_type count) {
		STREAM_READ_BOUNDS_CHECK(count, void());
		buffer_.skip(count);
	}

	/**
	 * @brief Provides a string_view over the stream's data, up to the terminator value.
	 * 
	 * @param terminator An optional terminating/sentinel value.
	 * 
	 * @return A string view over data up to the provided terminator.
	 * An empty string_view if a terminator is not found
	 */
	std::string_view view(value_type terminator = value_type(0)) requires(contiguous<buf_type>) {
		const auto pos = buffer_.find_first_of(terminator);

		if(pos == buf_type::npos) {
			return {};
		}

		std::string_view view { reinterpret_cast<char*>(buffer_.read_ptr()), pos };
		buffer_.skip(pos + 1);
		total_read_ += (pos + 1);
		return view;
	}

	/**
	 * @brief Provides a span over the specified number of elements in the stream.
	 * 
	 * @param count The number of elements the span will provide a view over.
	 * 
	 * @return A span representing a view over the requested number of elements
	 * in the stream.
	 * 
	 * @note The stream will error if the stream does not contain the requested amount of data.
	 */
	template<typename out_type = value_type>
	std::span<out_type> span(size_type count) requires(contiguous<buf_type>) {
		STREAM_READ_BOUNDS_CHECK(sizeof(out_type) * count, {});
		std::span span { reinterpret_cast<out_type*>(buffer_.read_ptr()), count };
		buffer_.skip(sizeof(out_type) * count);
		return span;
	}

	/**  Misc functions **/

	/**
	 * @brief Determine whether the adaptor supports write seeking.
	 * 
	 * This is determined at compile-time and does not need to checked at
	 * run-time.
	 * 
	 * @return True if write seeking is supported, otherwise false.
	 */
	constexpr static bool can_write_seek() requires(writeable<buf_type>) {
		return std::is_same_v<seeking, supported>;
	}

	/**
	 * @brief Performs write seeking within the stream.
	 * 
	 * @param direction Specify whether to seek in a given direction or to absolute seek.
	 * @param offset The offset relative to the seek direction or the absolute value
	 * when using absolute seeking.
	 */
	void write_seek(const stream_seek direction, const offset_type offset)
		requires(seekable<buf_type> && writeable<buf_type>) {
		if(direction == stream_seek::sk_stream_absolute) {
			buffer_.write_seek(buffer_seek::sk_backward, total_write_ - offset);
		} else {
			buffer_.write_seek(static_cast<buffer_seek>(direction), offset);
		}
	}

	/**
	 * @brief Returns the size of the stream.
	 * 
	 * @return The number of bytes of data available to read within the stream.
	 */
	size_type size() const {
		return buffer_.size();
	}

	/**
	 * @brief Whether the stream is empty.
	 * 
	 * @return Returns true if the stream is empty (has no data to be read).
	 */
	[[nodiscard]]
	bool empty() const {
		return buffer_.empty();
	}

	/**
	 * @return The total number of bytes written to the stream.
	 */
	size_type total_write() const requires(writeable<buf_type>) {
		return total_write_;
	}

	/**
	 * @return Pointer to stream's underlying buffer.
	 */
	const buf_type* buffer() const {
		return &buffer_;
	}

	/**
	 * @return Pointer to stream's underlying buffer.
	 */
	buf_type* buffer() {
		return &buffer_;
	}

	/**
	 * @return The stream's state.
	 */
	stream_state state() const {
		return state_;
	}

	/**
	 * @return The total number of bytes read from the stream.
	 */
	size_type total_read() const {
		return total_read_;
	}

	/**
	 * @return If provided to the constructor, the upper limit on how much data
	 * can be read from this stream before an error is triggers.
	 */
	size_type read_limit() const {
		return read_limit_;
	}

	/**
	 * @brief Determine whether the stream is in an error state.
	 * 
	 * @return True if no errors occurred on this stream.
	 */
	bool good() const {
		return state_ == stream_state::ok;
	}

	/**
	 * @brief Clears the reset state of the stream if an error has occurred.
	 */
	void clear_error_state() {
		state_ = stream_state::ok;
	}

	operator bool() const {
		return good();
	}

	/**
	 * @brief Set the stream to an error state.
	 */
	void set_error_state() {
		state_ = stream_state::user_defined_err;
	}
};

} // hexi

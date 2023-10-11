// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "pevent_lib.hpp"
#include "ringbuffer_holder.hpp"
#include "ringbuffer_utils.hpp"

#include <gtest/gtest.h>
#include <thread>

using namespace ddprof;

struct MyElement {
  perf_event_header hdr;
  int64_t x, y, z;
};

class jthread {
public:
  using id = std::thread::id;
  using native_handle_type = std::thread::native_handle_type;

  jthread() noexcept {}

  template <typename _Callable, typename... _Args,
            typename = std::enable_if_t<
                !std::is_same_v<std::remove_cvref_t<_Callable>, jthread>>>
  explicit jthread(_Callable &&f, _Args &&...args)
      : _thread{std::forward<_Callable>(f), std::forward<_Args>(args)...} {}

  jthread(const jthread &) = delete;
  jthread(jthread &&) noexcept = default;

  ~jthread() {
    if (joinable()) {
      join();
    }
  }

  jthread &operator=(const jthread &) = delete;

  jthread &operator=(jthread &&other) noexcept {
    jthread(std::move(other)).swap(*this);
    return *this;
  }

  void swap(jthread &other) noexcept { std::swap(_thread, other._thread); }

  [[nodiscard]] bool joinable() const noexcept { return _thread.joinable(); }

  void join() { _thread.join(); }

  void detach() { _thread.detach(); }

  [[nodiscard]] id get_id() const noexcept { return _thread.get_id(); }

  [[nodiscard]] native_handle_type native_handle() {
    return _thread.native_handle();
  }

  [[nodiscard]] static unsigned hardware_concurrency() noexcept {
    return std::thread::hardware_concurrency();
  }

  friend void swap(jthread &__lhs, jthread &__rhs) noexcept {
    __lhs.swap(__rhs);
  }

private:
  std::thread _thread;
};

void perf_reader_fun(RingBuffer *rb, size_t nb_elements, bool use_new_object,
                     bool advance_eagerly) {
  std::optional<PerfRingBufferReader> reader(rb);

  size_t count = 0;
  while (count < nb_elements) {
    if (use_new_object) {
      reader.emplace(rb);
    }
    while (reader->available_size() == 0) {
      sched_yield();
      reader->update_available();
    }
    auto buf = reader->read_all_available();
    while (!buf.empty()) {
      const MyElement *elem = reinterpret_cast<const MyElement *>(buf.data());

      ASSERT_EQ(elem->hdr.size, sizeof(MyElement));
      ASSERT_EQ(elem->hdr.misc, 5);
      ASSERT_EQ(elem->hdr.type, 3);
      ASSERT_EQ(elem->x, count);
      ASSERT_EQ(elem->y, count * 2);
      ASSERT_EQ(elem->z, count * 3);
      ++count;
      buf = remaining(buf, elem->hdr.size);
      if (advance_eagerly) {
        reader->advance(elem->hdr.size);
      }
    }
    if (!advance_eagerly && !use_new_object) {
      reader->advance();
    }
  }
}

void perf_writer_fun(RingBuffer *rb, size_t nb_elements, bool use_new_object,
                     bool use_reserve) {
  std::optional<PerfRingBufferWriter> writer(rb);

  for (int64_t i = 0; i < static_cast<int64_t>(nb_elements); ++i) {
    if (use_new_object) {
      writer.emplace(rb);
    }
    while (writer->available_size() < sizeof(MyElement)) {
      sched_yield();
      writer->update_available();
    }

    if (use_reserve) {
      auto buf = writer->reserve(sizeof(MyElement));
      ASSERT_EQ(buf.size(), sizeof(MyElement));
      MyElement *elem = reinterpret_cast<MyElement *>(buf.data());
      elem->hdr.size = sizeof(MyElement);
      elem->hdr.misc = 5;
      elem->hdr.type = 3;

      elem->x = i;
      elem->y = 2 * i;
      elem->z = 3 * i;

    } else {
      MyElement elem{.hdr = {.type = 3, .misc = 5, .size = sizeof(MyElement)},
                     .x = i,
                     .y = 2 * i,
                     .z = 3 * i};
      ASSERT_TRUE(writer->write(ddprof::ConstBuffer{
          reinterpret_cast<std::byte *>(&elem), elem.hdr.size}));
    }
    if (!use_new_object) {
      writer->commit();
    }
  }
}

TEST(ringbuffer, round) {
  ASSERT_EQ(align_up(0, 8), 0);
  ASSERT_EQ(align_up(1, 8), 8);
  ASSERT_EQ(align_up(7, 8), 8);
  ASSERT_EQ(align_up(8, 8), 8);
  ASSERT_EQ(align_up(9, 8), 16);
  ASSERT_EQ(align_up(std::numeric_limits<uint64_t>::max() - 6, 8), 0);

  ASSERT_EQ(align_down(0, 8), 0);
  ASSERT_EQ(align_down(1, 8), 0);
  ASSERT_EQ(align_down(7, 8), 0);
  ASSERT_EQ(align_down(8, 8), 8);
  ASSERT_EQ(align_down(9, 8), 8);
  ASSERT_EQ(align_down(std::numeric_limits<uint64_t>::max() - 6, 8),
            std::numeric_limits<uint64_t>::max() - 7);
}

TEST(ringbuffer, perf_ring_buffer) {
  const size_t buf_size_order = 1;
  RingBufferHolder ring_buffer{buf_size_order, RingBufferType::kPerfRingBuffer};
  constexpr size_t nelem = 1000;
  for (int producer_use_new_object = 0; producer_use_new_object < 2;
       ++producer_use_new_object) {
    for (int producer_use_reserve = 0; producer_use_reserve < 2;
         ++producer_use_reserve) {
      for (int consumer_use_new_object = 0; consumer_use_new_object < 2;
           ++consumer_use_new_object) {
        for (int consumer_advance_eagerly = 0; consumer_advance_eagerly < 2;
             ++consumer_advance_eagerly) {
          jthread producer{perf_writer_fun, &ring_buffer.get_ring_buffer(),
                           nelem, producer_use_new_object,
                           producer_use_reserve};
          jthread consumer{perf_reader_fun, &ring_buffer.get_ring_buffer(),
                           nelem, consumer_use_new_object,
                           consumer_advance_eagerly};
        }
      }
    }
  }
}

TEST(ringbuffer, edge_cases) {
  const size_t buf_size_order = 0;
  RingBufferHolder ring_buffer{buf_size_order, RingBufferType::kPerfRingBuffer};
  PerfRingBufferWriter writer{&ring_buffer.get_ring_buffer()};

  auto buf = writer.reserve(0);
  ASSERT_TRUE(buf.empty());
  ASSERT_EQ(buf.data(), nullptr);
  buf = writer.reserve(4095);
  ASSERT_TRUE(buf.empty());
  ASSERT_EQ(buf.data(), nullptr);
  buf = writer.reserve(std::numeric_limits<size_t>::max() - 1);
  ASSERT_TRUE(buf.empty());
  ASSERT_EQ(buf.data(), nullptr);

  buf = writer.reserve(1);
  ASSERT_FALSE(buf.empty());
  ASSERT_EQ(buf.size(), 1);
  *buf.data() = std::byte{'z'};
  writer.commit();

  PerfRingBufferReader reader{&ring_buffer.get_ring_buffer()};
  auto read_buf = reader.read_all_available();
  ASSERT_FALSE(read_buf.empty());
  ASSERT_EQ(*read_buf.data(), std::byte{'z'});
  reader.advance(1);

  buf = writer.reserve(1);
  ASSERT_FALSE(buf.empty());
  ASSERT_EQ(buf.size(), 1);
  *buf.data() = std::byte{'y'};
  writer.commit();

  reader.update_available();
  read_buf = reader.read_all_available();
  ASSERT_FALSE(read_buf.empty());
  ASSERT_EQ(*read_buf.data(), std::byte{'y'});
}

TEST(ringbuffer, full) {
  const size_t buf_size_order = 0;
  RingBufferHolder ring_buffer{buf_size_order, RingBufferType::kPerfRingBuffer};
  PerfRingBufferWriter writer{&ring_buffer.get_ring_buffer()};
  PerfRingBufferReader reader{&ring_buffer.get_ring_buffer()};
  EXPECT_EQ(reader.available_size(), 0);

  auto sz = writer.available_size();
  EXPECT_GT(sz, 0);
  // size is rounded up to multiple of 8 by reserve
  auto sz2 = align_down(sz, 8);
  auto buffer = writer.reserve(sz2);
  ASSERT_FALSE(buffer.empty());
  std::fill(buffer.begin(), buffer.end(), std::byte{0xff});

  EXPECT_EQ(writer.available_size(), sz - sz2);
  writer.commit();

  reader.update_available();
  EXPECT_EQ(reader.available_size(), sz2);
  auto buffer2 = reader.read_all_available();
  EXPECT_EQ(reader.available_size(), 0);
  EXPECT_TRUE(
      std::equal(buffer.begin(), buffer.end(), buffer2.begin(), buffer2.end()));
}

void mpsc_reader_fun(RingBuffer *rb, size_t nb_elements, size_t nb_producers,
                     bool use_new_object, bool advance_eagerly) {
  std::optional<MPSCRingBufferReader> reader(rb);

  std::vector<size_t> counts(nb_producers);
  size_t total_count = 0;
  while (total_count < nb_elements * nb_producers) {
    if (use_new_object) {
      reader.emplace(rb);
    } else {
      reader->update_available();
    }
    for (ConstBuffer buf = reader->read_sample(); !buf.empty();
         buf = reader->read_sample()) {
      const MyElement *elem = reinterpret_cast<const MyElement *>(buf.data());

      ASSERT_EQ(elem->hdr.size, sizeof(MyElement));
      ASSERT_EQ(elem->hdr.misc, 5);
      ASSERT_EQ(elem->hdr.type, 3);

      int64_t producer_idx = elem->y;
      size_t &count = counts[producer_idx];
      ASSERT_EQ(elem->x, count);
      ASSERT_EQ(elem->z, count * (producer_idx + 1));
      ++count;
      ++total_count;
      if (advance_eagerly) {
        reader->advance();
      }
    }

    if (!advance_eagerly && !use_new_object) {
      reader->advance();
    }
  }
}

void mpsc_writer_fun(RingBuffer *rb, size_t nb_elements, size_t producer_idx,
                     bool use_new_object) {
  std::optional<MPSCRingBufferWriter> writer(rb);

  for (int64_t i = 0; i < static_cast<int64_t>(nb_elements); ++i) {
    if (use_new_object) {
      writer.emplace(rb);
    }
    auto buf = writer->reserve(sizeof(MyElement));
    while (buf.empty()) {
      sched_yield();
      writer->update_tail();
      buf = writer->reserve(sizeof(MyElement));
    }

    ASSERT_EQ(buf.size(), sizeof(MyElement));
    MyElement *elem = reinterpret_cast<MyElement *>(buf.data());
    elem->hdr.size = sizeof(MyElement);
    elem->hdr.misc = 5;
    elem->hdr.type = 3;

    elem->x = i;
    elem->y = producer_idx;
    elem->z = i * (producer_idx + 1);
    writer->commit(buf);
  }
}

TEST(ringbuffer, mpsc_ring_buffer_simple) {
  const size_t buf_size_order = 1;
  RingBufferHolder ring_buffer{buf_size_order, RingBufferType::kMPSCRingBuffer};
  MPSCRingBufferWriter writer{&ring_buffer.get_ring_buffer()};
  MPSCRingBufferReader reader{&ring_buffer.get_ring_buffer()};
  auto buf = writer.reserve(4);
  ASSERT_EQ(buf.size(), 4);
  *reinterpret_cast<uint32_t *>(buf.data()) = 0xdeadbeef;
  writer.commit(buf);
  ASSERT_TRUE(reader.read_sample().empty());
  reader.update_available();
  auto buf2 = reader.read_sample();
  ASSERT_EQ(buf2.size(), 4);
  ASSERT_EQ(*reinterpret_cast<uint32_t *>(buf.data()), 0xdeadbeef);
}

TEST(ringbuffer, mpsc_ring_buffer_single_producer) {
  const size_t buf_size_order = 0;
  RingBufferHolder ring_buffer{buf_size_order, RingBufferType::kMPSCRingBuffer};
  constexpr size_t nelem = 1000;

  for (int producer_use_new_object = 0; producer_use_new_object < 2;
       ++producer_use_new_object) {
    for (int consumer_use_new_object = 0; consumer_use_new_object < 2;
         ++consumer_use_new_object) {
      for (int consumer_advance_eagerly = 0; consumer_advance_eagerly < 2;
           ++consumer_advance_eagerly) {
        jthread producer{mpsc_writer_fun, &ring_buffer.get_ring_buffer(), nelem,
                         0, producer_use_new_object};
        jthread consumer{
            mpsc_reader_fun,         &ring_buffer.get_ring_buffer(), nelem, 1,
            consumer_use_new_object, consumer_advance_eagerly};
      }
    }
  }
}

TEST(ringbuffer, mpsc_ring_buffer_multiple_producer) {
  const size_t buf_size_order = 0;
  RingBufferHolder ring_buffer{buf_size_order, RingBufferType::kMPSCRingBuffer};
  constexpr size_t nelem = 1000;
  constexpr size_t nproducer = 8;

  for (int producer_use_new_object = 0; producer_use_new_object < 2;
       ++producer_use_new_object) {
    for (int consumer_use_new_object = 0; consumer_use_new_object < 2;
         ++consumer_use_new_object) {
      for (int consumer_advance_eagerly = 0; consumer_advance_eagerly < 2;
           ++consumer_advance_eagerly) {
        std::vector<jthread> producers;
        for (size_t i = 0; i < nproducer; ++i) {
          producers.emplace_back(mpsc_writer_fun,
                                 &ring_buffer.get_ring_buffer(), nelem, i,
                                 producer_use_new_object);
        }
        jthread consumer{mpsc_reader_fun,
                         &ring_buffer.get_ring_buffer(),
                         nelem,
                         nproducer,
                         consumer_use_new_object,
                         consumer_advance_eagerly};
      }
    }
  }
}

TEST(ringbuffer, mpsc_ring_buffer_stale_lock) {
  const size_t buf_size_order = 0;
  RingBufferHolder ring_buffer{buf_size_order, RingBufferType::kMPSCRingBuffer};
  MPSCRingBufferWriter writer{&ring_buffer.get_ring_buffer()};

  // simulate stale lock
  ring_buffer.get_ring_buffer().spinlock->lock();

  bool timeout = false;
  ASSERT_TRUE(writer.reserve(4, &timeout).empty());
  ASSERT_TRUE(timeout);
}

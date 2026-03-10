#include <catch2/catch_test_macros.hpp>

#include "../../src/core/unity_uabe/libCompression/lz4.h"
#include "../../src/core/unity_uabe/libCompression/lz4dec.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
struct ReadState {
  const std::vector<std::uint8_t> *data;
  std::size_t pos;
  std::size_t maxChunk;
};

struct WriteState {
  std::vector<std::uint8_t> *out;
  std::size_t maxChunk;
  bool forceZero;
};

int read_cb(void *buffer, int size, LZ4e_instream_t *stream) {
  if (size <= 0) {
    return 0;
  }
  auto *st = static_cast<ReadState *>(stream->user);
  if (st->pos >= st->data->size()) {
    return 0;
  }
  std::size_t toRead = static_cast<std::size_t>(size);
  toRead = std::min(toRead, st->maxChunk);
  toRead = std::min(toRead, st->data->size() - st->pos);
  std::memcpy(buffer, st->data->data() + st->pos, toRead);
  st->pos += toRead;
  return static_cast<int>(toRead);
}

int write_cb(const void *buffer, int size, LZ4e_outstream_t *stream) {
  if (size <= 0) {
    return 0;
  }
  auto *st = static_cast<WriteState *>(stream->user);
  if (st->forceZero) {
    return 0;
  }
  std::size_t toWrite = static_cast<std::size_t>(size);
  toWrite = std::min(toWrite, st->maxChunk);
  const auto *p = static_cast<const std::uint8_t *>(buffer);
  st->out->insert(st->out->end(), p, p + toWrite);
  return static_cast<int>(toWrite);
}

std::vector<std::uint8_t> make_payload() {
  std::vector<std::uint8_t> payload(64 * 1024);
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::uint8_t>((i * 31u) & 0xFFu);
  }
  return payload;
}

bool compress_payload(const std::vector<std::uint8_t> &input,
                      std::vector<std::uint8_t> &compressed) {
  const int maxDst = LZ4_compressBound(static_cast<int>(input.size()));
  if (maxDst <= 0) {
    return false;
  }
  compressed.resize(static_cast<std::size_t>(maxDst));
  const int written =
      LZ4_compress_default(reinterpret_cast<const char *>(input.data()),
                           reinterpret_cast<char *>(compressed.data()),
                           static_cast<int>(input.size()), maxDst);
  if (written <= 0) {
    compressed.clear();
    return false;
  }
  compressed.resize(static_cast<std::size_t>(written));
  return true;
}
} // namespace

TEST_CASE("uabe lz4 rejects broken block") {
  std::vector<std::uint8_t> bad(256, 0xFF);
  std::vector<std::uint8_t> sink;

  ReadState rs{&bad, 0, 128};
  WriteState ws{&sink, 4096, false};
  LZ4e_instream_t in{0, read_cb, &rs};
  LZ4e_outstream_t out{write_cb, &ws};

  std::vector<char> sourceBuf(1024 * 1024);
  std::vector<char> destBuf(1024 * 1024);
  const int rc = LZ4e_decompress_safe(
      sourceBuf.data(), destBuf.data(), static_cast<int>(sourceBuf.size()),
      static_cast<int>(destBuf.size()), &in, &out);
  REQUIRE(rc <= 0);
}

TEST_CASE("uabe lz4 detects truncated short-read stream") {
  const auto payload = make_payload();
  std::vector<std::uint8_t> compressed;
  REQUIRE(compress_payload(payload, compressed));
  REQUIRE(compressed.size() > 32);

  std::vector<std::uint8_t> truncated(
      compressed.begin(), compressed.begin() + (compressed.size() / 2));
  std::vector<std::uint8_t> sink;

  ReadState rs{&truncated, 0, 7};
  WriteState ws{&sink, 4096, false};
  LZ4e_instream_t in{0, read_cb, &rs};
  LZ4e_outstream_t out{write_cb, &ws};

  std::vector<char> sourceBuf(1024 * 1024);
  std::vector<char> destBuf(1024 * 1024);
  const int rc = LZ4e_decompress_safe(
      sourceBuf.data(), destBuf.data(), static_cast<int>(sourceBuf.size()),
      static_cast<int>(destBuf.size()), &in, &out);
  REQUIRE(rc <= 0);
}

TEST_CASE("uabe lz4 fails when write callback returns zero") {
  const auto payload = make_payload();
  std::vector<std::uint8_t> compressed;
  REQUIRE(compress_payload(payload, compressed));

  std::vector<std::uint8_t> sink;
  ReadState rs{&compressed, 0, 4096};
  WriteState ws{&sink, 4096, true};
  LZ4e_instream_t in{0, read_cb, &rs};
  LZ4e_outstream_t out{write_cb, &ws};

  std::vector<char> sourceBuf(1024 * 1024);
  std::vector<char> destBuf(1024 * 1024);
  const int rc = LZ4e_decompress_safe(
      sourceBuf.data(), destBuf.data(), static_cast<int>(sourceBuf.size()),
      static_cast<int>(destBuf.size()), &in, &out);
  REQUIRE(rc <= 0);
}

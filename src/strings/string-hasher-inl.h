// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_STRINGS_STRING_HASHER_INL_H_
#define V8_STRINGS_STRING_HASHER_INL_H_

#include "src/strings/string-hasher.h"

// Comment inserted to prevent header reordering.
#include <type_traits>

#include "src/objects/name-inl.h"
#include "src/objects/string-inl.h"
#include "src/strings/char-predicates-inl.h"
#include "src/utils/utils-inl.h"

namespace v8 {
namespace internal {

uint32_t StringHasher::AddCharacterCore(uint32_t running_hash, uint16_t c) {
  running_hash += c;
  running_hash += (running_hash << 10);
  running_hash ^= (running_hash >> 6);
  return running_hash;
}

uint32_t StringHasher::GetHashCore(uint32_t running_hash) {
  running_hash += (running_hash << 3);
  running_hash ^= (running_hash >> 11);
  running_hash += (running_hash << 15);
  int32_t hash = static_cast<int32_t>(running_hash & String::HashBits::kMax);
  // Ensure that the hash is kZeroHash, if the computed value is 0.
  int32_t mask = (hash - 1) >> 31;
  running_hash |= (kZeroHash & mask);
  return running_hash;
}

uint32_t StringHasher::GetTrivialHash(uint32_t length) {
  DCHECK_GT(length, String::kMaxHashCalcLength);
  // The hash of a large string is simply computed from the length.
  // Ensure that the max length is small enough to be encoded without losing
  // information.
  static_assert(String::kMaxLength <= String::HashBits::kMax);
  uint32_t hash = length;
  return String::CreateHashFieldValue(hash, String::HashFieldType::kHash);
}

template <typename char_t>
uint32_t StringHasher::HashSequentialString(const char_t* chars_raw,
                                            uint32_t length, uint64_t seed) {
  static_assert(std::is_integral<char_t>::value);
  static_assert(sizeof(char_t) <= 2);
  using uchar = typename std::make_unsigned<char_t>::type;
  const uchar* chars = reinterpret_cast<const uchar*>(chars_raw);
  DCHECK_IMPLIES(length > 0, chars != nullptr);
  if (length >= 1) {
    if (IsDecimalDigit(chars[0]) && (length == 1 || chars[0] != '0')) {
      if (length <= String::kMaxArrayIndexSize) {
        // Possible array index; try to compute the array index hash.
        uint32_t index = chars[0] - '0';
        uint32_t i = 1;
        do {
          if (i == length) {
            return MakeArrayIndexHash(index, length);
          }
        } while (TryAddArrayIndexChar(&index, chars[i++]));
      }
      // The following block wouldn't do anything on 32-bit platforms,
      // because kMaxArrayIndexSize == kMaxIntegerIndexSize there, and
      // if we wanted to compile it everywhere, then {index_big} would
      // have to be a {size_t}, which the Mac compiler doesn't like to
      // implicitly cast to uint64_t for the {TryAddIndexChar} call.
#if V8_HOST_ARCH_64_BIT
      // No "else" here: if the block above was entered and fell through,
      // we'll have to take this branch.
      if (length <= String::kMaxIntegerIndexSize) {
        // Not an array index, but it could still be an integer index.
        // Perform a regular hash computation, and additionally check
        // if there are non-digit characters.
        String::HashFieldType type = String::HashFieldType::kIntegerIndex;
        uint32_t running_hash = static_cast<uint32_t>(seed);
        uint64_t index_big = 0;
        const uchar* end = &chars[length];
        while (chars != end) {
          if (type == String::HashFieldType::kIntegerIndex &&
              !TryAddIntegerIndexChar(&index_big, *chars)) {
            type = String::HashFieldType::kHash;
          }
          running_hash = AddCharacterCore(running_hash, *chars++);
        }
        uint32_t hash =
            String::CreateHashFieldValue(GetHashCore(running_hash), type);
        if (Name::ContainsCachedArrayIndex(hash)) {
          // The hash accidentally looks like a cached index. Fix that by
          // setting a bit that looks like a longer-than-cacheable string
          // length.
          hash |= (String::kMaxCachedArrayIndexLength + 1)
                  << String::ArrayIndexLengthBits::kShift;
        }
        DCHECK(!Name::ContainsCachedArrayIndex(hash));
        return hash;
      }
#endif
    }
    // No "else" here: if the first character was a decimal digit, we might
    // still have to take this branch.
    if (length > String::kMaxHashCalcLength) {
      return GetTrivialHash(length);
    }
  }

  // Non-index hash.
  uint32_t running_hash = static_cast<uint32_t>(seed);
  const uchar* end = &chars[length];
  while (chars != end) {
    running_hash = AddCharacterCore(running_hash, *chars++);
  }

  return String::CreateHashFieldValue(GetHashCore(running_hash),
                                      String::HashFieldType::kHash);
}

std::size_t SeededStringHasher::operator()(const char* name) const {
  return StringHasher::HashSequentialString(
      name, static_cast<uint32_t>(strlen(name)), hashseed_);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_STRINGS_STRING_HASHER_INL_H_

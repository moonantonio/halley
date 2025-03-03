#pragma once

/*****************************************************************\
           __
          / /
		 / /                     __  __
		/ /______    _______    / / / / ________   __       __
	   / ______  \  /_____  \  / / / / / _____  | / /      / /
	  / /      | / _______| / / / / / / /____/ / / /      / /
	 / /      / / / _____  / / / / / / _______/ / /      / /
	/ /      / / / /____/ / / / / / / |______  / |______/ /
   /_/      /_/ |________/ / / / /  \_______/  \_______  /
                          /_/ /_/                     / /
			                                         / /
		       High Level Game Framework            /_/

  ---------------------------------------------------------------

  Copyright (c) 2007-2014 - Rodrigo Braz Monteiro.
  This file is subject to the terms of halley_license.txt.

\*****************************************************************/

#include <cstdint>

namespace Halley {
	template <typename T, size_t blockLen = 16384, bool threadSafe = true>
	class MappedPool {
		struct Entry {
			alignas(T) std::array<char, sizeof(T)> data;
			uint32_t nextFreeEntryIndex;
			uint32_t revision;
		};

		struct Block {
			Vector<Entry> data;

			Block(size_t blockIndex)
				: data(blockLen)
			{
				size_t base = blockIndex * blockLen;
				for (size_t i = 0; i < blockLen; i++) {
					// Each entry points to the next
					data[i].nextFreeEntryIndex = static_cast<uint32_t>(i + 1 + base);
					data[i].revision = 0;
				}
			}

			Block(const Block& other) = delete;
			Block& operator=(const Block& other) = delete;
			Block(Block&& other) noexcept = default;
			Block& operator=(Block&& other) noexcept = default;
		};

	public:
		MappedPool(size_t maxBlocks = 64)
		{
			blocks.reserve(maxBlocks);
		}

		std::pair<T*, int64_t> alloc() {
			auto lock = lockMutex();

			// Next entry will be at position "entryIdx", which is just what was stored on next
			const uint32_t entryIdx = next;

			// Figure which block it goes into, and make sure that exists
			const size_t blockIdx = entryIdx / blockLen;
			if (blockIdx >= blocks.size()) {
				// We never grow beyond pre-reserved size as that could cause a block pointer invalidation, which would make MappedPool::get() thread-unsafe.
				// Locking that method in a mutex would perform too slowly
				if (blocks.size() + 1 > blocks.capacity()) {
					throw Exception("Run out of maximum space on MappedPool", HalleyExceptions::Utils);
				}
				blocks.push_back(Block(blocks.size()));
			}
			auto& block = blocks[blockIdx];

			// Find the local entry inside that block and initialize it
			const size_t localIdx = entryIdx % blockLen;
			auto& data = block.data[localIdx];
			const int rev = data.revision;
			T* result = reinterpret_cast<T*>(&(data.data));

			// Next block is what was stored on the nextFreeEntryIndex
			std::swap(next, block.data[localIdx].nextFreeEntryIndex);

			// External index composes the revision with the index, so it's unique, but easily mappable
			const int64_t externalIdx = static_cast<int64_t>(entryIdx) | (static_cast<int64_t>(rev & 0x7FFFFFFF) << 32); // TODO: compute properly
			return std::pair<T*, int64_t>(result, externalIdx);
		}

		void free(T* p) {
			auto lock = lockMutex();

			// Swaps the data with the next, so this will actually be the next one to be allocated
			Entry* entry = reinterpret_cast<Entry*>(p);
			std::swap(entry->nextFreeEntryIndex, next);

			// Increase revision so the next one to allocate this gets a unique number
			++entry->revision;
		}

		void freeId(int64_t externalIdx) {
			free(get(externalIdx));
		}

		T* get(int64_t externalIdx) {
			auto idx = static_cast<uint32_t>(externalIdx & 0xFFFFFFFFll);
			auto rev = static_cast<uint32_t>(externalIdx >> 32);

			int blockN = idx / blockLen;
			if (blockN < 0 || blockN >= int(blocks.capacity())) {
				return nullptr;
			}

			auto& block = blocks[blockN];
			int localIdx = idx % blockLen;
			auto& data = block.data[localIdx];
			if (data.revision != rev) {
				return nullptr;
			}
			return reinterpret_cast<T*>(&(data.data));
		}

		const T* get(int64_t externalIdx) const {
			auto idx = static_cast<uint32_t>(externalIdx & 0xFFFFFFFFll);
			auto rev = static_cast<uint32_t>(externalIdx >> 32);

			int blockN = idx / blockLen;
			if (blockN < 0 || blockN >= int(blocks.capacity())) {
				return nullptr;
			}

			auto& block = blocks[blockN];
			int localIdx = idx % blockLen;
			auto& data = block.data[localIdx];
			if (data.revision != rev) {
				return nullptr;
			}
			return reinterpret_cast<const T*>(&(data.data));
		}

	private:
		VectorStd<Block, uint32_t, false> blocks; // Ensure no SBO
		uint32_t next = 0;

		mutable std::mutex mutex;

		std::unique_lock<std::mutex> lockMutex() const
		{
			if constexpr (threadSafe) {
				return std::unique_lock<std::mutex>(mutex);
			} else {
				return std::unique_lock<std::mutex>();
			}
		}
	};
}

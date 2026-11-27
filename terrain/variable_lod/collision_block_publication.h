#ifndef VOXEL_COLLISION_BLOCK_PUBLICATION_H
#define VOXEL_COLLISION_BLOCK_PUBLICATION_H

#include <cstdint>
#include <limits>

namespace zylann::voxel {

// Worker-owned monotonic request IDs for one terrain volume. Do not reset this source when mesh maps are rebuilt.
class CollisionRequestTickets {
public:
	uint64_t issue() {
		if (_exhausted) {
			return 0;
		}

		const uint64_t ticket = _next_ticket;
		if (_next_ticket == std::numeric_limits<uint64_t>::max()) {
			_exhausted = true;
		} else {
			++_next_ticket;
		}
		return ticket;
	}

private:
	uint64_t _next_ticket = 1;
	bool _exhausted = false;
};

// Tracks whether the current request for a collision block has been physically published.
// The terrain owns synchronization and must call publish_* only after its physics publication fence.
class CollisionBlockPublication {
public:
	enum class State : uint8_t {
		UNAVAILABLE,
		EMPTY,
		BODY
	};

	uint64_t get_ticket() const {
		return _ticket;
	}

	State get_state() const {
		return _state;
	}

	static bool is_current_result(uint64_t result_ticket, uint64_t expected_ticket) {
		return result_ticket != 0 && result_ticket == expected_ticket;
	}

	void begin_request(uint64_t ticket) {
		if (ticket == 0) {
			clear();
			return;
		}
		_ticket = ticket;
		_state = State::UNAVAILABLE;
	}

	bool publish_empty(uint64_t ticket) {
		return _publish(ticket, State::EMPTY);
	}

	bool publish_body(uint64_t ticket) {
		return _publish(ticket, State::BODY);
	}

	void clear() {
		_ticket = 0;
		_state = State::UNAVAILABLE;
	}

private:
	bool _publish(uint64_t ticket, State state) {
		if (ticket == 0 || ticket != _ticket || _state != State::UNAVAILABLE) {
			return false;
		}
		_state = state;
		return true;
	}

	uint64_t _ticket = 0;
	State _state = State::UNAVAILABLE;
};

} // namespace zylann::voxel

#endif // VOXEL_COLLISION_BLOCK_PUBLICATION_H

#ifndef VOXEL_COLLISION_BLOCK_PUBLICATION_H
#define VOXEL_COLLISION_BLOCK_PUBLICATION_H

#include <cstdint>

namespace zylann::voxel {

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

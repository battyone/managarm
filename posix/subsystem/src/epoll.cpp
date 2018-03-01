
#include <string.h>
#include <iostream>

#include <async/doorbell.hpp>
#include <boost/intrusive/list.hpp>
#include <cofiber.hpp>
#include <helix/ipc.hpp>
#include "common.hpp"
#include "epoll.hpp"

namespace {

bool logEpoll = false;

struct OpenFile : File {
	// ------------------------------------------------------------------------
	// Internal API.
	// ------------------------------------------------------------------------
private:
	struct Item : boost::intrusive::list_base_hook<> {
		Item(OpenFile *epoll, File *file, int mask, uint64_t cookie)
		: epoll{epoll}, file{file}, eventMask{mask}, cookie{cookie},
				isPending{false} { }

		// Basic data of this item.
		OpenFile *epoll;
		File *file;
		int eventMask;
		uint64_t cookie;

		// True iff this item is in the pending queue.
		bool isPending;
	};

	static void _awaitPoll(Item *item, PollResult result) {
		auto self = item->epoll;

		// Note that items only become pending if there is an edge.
		// This is the correct behavior for edge-triggered items.
		// Level-triggered items stay pending until the event disappears.
		if(!item->isPending && (std::get<1>(result) & item->eventMask)
				&& (std::get<2>(result) & item->eventMask)) {
			if(logEpoll)
				std::cout << "posix.epoll \e[1;34m" << item->epoll->structName() << "\e[0m"
						<< ": Item \e[1;34m" << item->file->structName()
						<< "\e[0m becomes pending" << std::endl;
			// Note that we stop watching once an item becomes pending.
			// We do this as we have to poll() again anyway before we report the item.
			item->isPending = true;
			self->_pendingQueue.push_back(*item);
			self->_currentSeq++;
			self->_pendingBell.ring();
		}else{
			// Here, we assume that the lambda does not execute on the current stack.
			// TODO: Use some callback queueing mechanism to ensure this.
			if(logEpoll)
				std::cout << "posix.epoll \e[1;34m" << item->epoll->structName() << "\e[0m"
						<< ": Item \e[1;34m" << item->file->structName()
						<< "\e[0m still not pending after poll()."
						<< " Mask is " << item->eventMask << ", while "
						<< std::get<2>(result) << " is active" << std::endl;
			auto poll = item->file->poll(std::get<0>(result));
			poll.then([item] (PollResult next_result) {
				_awaitPoll(item, next_result);
			});
		}
	}

public:
	~OpenFile() {
		assert(!"close() does not work correctly for epoll files");
	}

	void addItem(File *file, int mask, uint64_t cookie) {
		if(logEpoll)
			std::cout << "posix.epoll \e[1;34m" << structName() << "\e[0m: Adding item \e[1;34m"
					<< file->structName() << "\e[0m. Mask is " << mask << std::endl;
		// TODO: Fix the memory-leak.
		assert(_fileMap.find(file) == _fileMap.end());
		auto item = new Item{this, file, mask, cookie};

		auto poll = item->file->poll(0);
		poll.then([item] (PollResult result) {
			_awaitPoll(item, result);
		});

		_fileMap.insert({file, item});
	}
	
	void modifyItem(File *file, int mask, uint64_t cookie) {
		std::cout << "posix.epoll \e[1;34m" << structName() << "\e[0m: Modifying item \e[1;34m"
				<< file->structName() << "\e[0m. New mask is " << mask << std::endl;
	}

	void deleteItem(File *file, int mask) {
		std::cout << "posix.epoll \e[1;34m" << structName() << "\e[0m: Deleting item \e[1;34m"
				<< file->structName() << "\e[0m" << std::endl;
	}

	COFIBER_ROUTINE(async::result<size_t>, waitForEvents(struct epoll_event *events,
			size_t max_events), ([=] {
		assert(max_events);
		if(logEpoll)
			std::cout << "posix.epoll \e[1;34m" << structName() << "\e[0m: Entering wait."
					" There are " << _pendingQueue.size() << " pending items" << std::endl;
		size_t k = 0;
		boost::intrusive::list<Item> repoll_queue;
		while(!k) {
			while(_pendingQueue.empty())
				COFIBER_AWAIT _pendingBell.async_wait();

			while(!_pendingQueue.empty()) {
				auto item = &_pendingQueue.front();
				_pendingQueue.pop_front();
				assert(item->isPending);

				auto result = COFIBER_AWAIT item->file->poll(0);	
				if(logEpoll)
					std::cout << "posix.epoll \e[1;34m" << structName() << "\e[0m: Checking item "
							<< "\e[1;34m" << item->file->structName() << "\e[0m."
							" Mask is " << item->eventMask << ", while " << std::get<2>(result)
							<< " is active" << std::endl;
				auto status = std::get<2>(result) & item->eventMask;

				// Abort early (i.e before requeuing) if the item is not pending.
				if(!status) {
					item->isPending = false;

					// Once an item is not pending anymore, we continue watching it.
					auto poll = item->file->poll(std::get<0>(result));
					poll.then([item] (PollResult next_result) {
						_awaitPoll(item, next_result);
					});

					continue;
				}
				
				// We have to increment the sequence again as concurrent waiters
				// might have seen an empty _pendingQueue.
				// TODO: Edge-triggered watches should not be requeued here.
				repoll_queue.push_back(*item);

				memset(events + k, 0, sizeof(struct epoll_event));
				events[k].events = status;
				events[k].data.u64 = item->cookie;

				k++;
				if(k == max_events)
					break;
			}
		}

		// Before returning, we have to reinsert the level-triggered events that we report.
		if(!repoll_queue.empty()) {
			_pendingQueue.splice(_pendingQueue.end(), repoll_queue);
			_currentSeq++;
			_pendingBell.ring();
		}

		COFIBER_RETURN(k);
	}))

	// ------------------------------------------------------------------------
	// File implementation.
	// ------------------------------------------------------------------------

	COFIBER_ROUTINE(FutureMaybe<size_t>, readSome(void *, size_t) override, ([=] {
		throw std::runtime_error("Cannot read from epoll FD");
	}))
	
	COFIBER_ROUTINE(FutureMaybe<PollResult>, poll(uint64_t past_seq) override, ([=] {
		assert(past_seq <= _currentSeq);
		while(_currentSeq == past_seq)
			COFIBER_AWAIT _pendingBell.async_wait();

		COFIBER_RETURN(PollResult(_currentSeq, EPOLLIN, _pendingQueue.empty() ? 0 : EPOLLIN));
	}))
	
	helix::BorrowedDescriptor getPassthroughLane() override {
		return _passthrough;
	}

public:
	static void serve(std::shared_ptr<OpenFile> file) {
//TODO:		assert(!file->_passthrough);

		helix::UniqueLane lane;
		std::tie(lane, file->_passthrough) = helix::createStream();
		protocols::fs::servePassthrough(std::move(lane), std::shared_ptr<File>{file},
				&File::fileOperations);
	}

	OpenFile()
	: File{StructName::get("epoll")}, _currentSeq{1} { }

private:
	helix::UniqueLane _passthrough;

	// FIXME: This really has to map std::weak_ptrs or std::shared_ptrs.
	std::unordered_map<File *, Item *> _fileMap;

	boost::intrusive::list<Item> _pendingQueue;
	async::doorbell _pendingBell;
	uint64_t _currentSeq;
};

} // anonymous namespace

namespace epoll {

std::shared_ptr<File> createFile() {
	auto file = std::make_shared<OpenFile>();
	OpenFile::serve(file);
	return std::move(file);
}

void addItem(File *epfile, File *file, int flags, uint64_t cookie) {
	auto epoll = static_cast<OpenFile *>(epfile);
	epoll->addItem(file, flags, cookie);
}

void modifyItem(File *epfile, File *file, int flags, uint64_t cookie) {
	auto epoll = static_cast<OpenFile *>(epfile);
	epoll->modifyItem(file, flags, cookie);
}

void deleteItem(File *epfile, File *file, int flags) {
	auto epoll = static_cast<OpenFile *>(epfile);
	epoll->deleteItem(file, flags);
}

async::result<size_t> wait(File *epfile, struct epoll_event *events,
		size_t max_events) {
	auto epoll = static_cast<OpenFile *>(epfile);
	return epoll->waitForEvents(events, max_events);
}

} // namespace epoll


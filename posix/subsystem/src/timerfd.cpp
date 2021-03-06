
#include <string.h>
#include <sys/epoll.h>
#include <iostream>

#include <async/doorbell.hpp>
#include <cofiber.hpp>
#include <helix/ipc.hpp>
#include "timerfd.hpp"

namespace {

bool logTimerfd = false;

struct OpenFile : File {
private:
	struct Timer {
		uint64_t asyncId;
		uint64_t initial;
		uint64_t interval;
	};
	
	COFIBER_ROUTINE(cofiber::no_future, arm(Timer *timer), ([=] {
		assert(timer->initial || timer->interval);
//		std::cout << "posix: Timer armed" << std::endl;

		uint64_t tick;
		HEL_CHECK(helGetClock(&tick));

		if(timer->initial) {
			helix::AwaitClock await_initial;
			auto &&submit = helix::submitAwaitClock(&await_initial, tick + timer->initial,
					helix::Dispatcher::global());
			timer->asyncId = await_initial.asyncId();
			COFIBER_AWAIT submit.async_wait();
			timer->asyncId = 0;
			assert(!await_initial.error() || await_initial.error() == kHelErrCancelled);
			tick += timer->initial;

			if(_activeTimer == timer) {
				_expirations++;
				_theSeq++;
				_seqBell.ring();
			}else{
				delete timer;
				COFIBER_RETURN();
			}
		}

		if(!timer->interval) {
			if(_activeTimer == timer)
				_activeTimer = nullptr;
			delete timer;
			COFIBER_RETURN();
		}

		while(true) {
			helix::AwaitClock await_interval;
			auto &&submit = helix::submitAwaitClock(&await_interval, tick + timer->interval,
					helix::Dispatcher::global());
			timer->asyncId = await_interval.asyncId();
			COFIBER_AWAIT submit.async_wait();
			timer->asyncId = 0;
			assert(!await_interval.error() || await_interval.error() == kHelErrCancelled);
			tick += timer->interval;

			if(_activeTimer == timer) {
				_expirations++;
				_theSeq++;
				_seqBell.ring();
			}else{
				delete timer;
				COFIBER_RETURN();
			}
		}
	}))

public:
	static void serve(smarter::shared_ptr<OpenFile> file) {
//TODO:		assert(!file->_passthrough);

		helix::UniqueLane lane;
		std::tie(lane, file->_passthrough) = helix::createStream();
		async::detach(protocols::fs::servePassthrough(std::move(lane),
				file, &File::fileOperations, file->_cancelServe));
	}

	OpenFile(bool non_block)
	: File{StructName::get("timerfd")}, _nonBlock{non_block},
			_activeTimer{nullptr}, _expirations{0}, _theSeq{1} {
		(void)_nonBlock;
	}

	~OpenFile() {
		// Nothing to do here.
	}

	void handleClose() override {
		_seqBell.ring();
		_cancelServe.cancel();
	}

	COFIBER_ROUTINE(expected<size_t>,
	readSome(Process *, void *data, size_t max_length) override, ([=] {
		assert(max_length == sizeof(uint64_t));
		assert(_expirations);

		memcpy(data, &_expirations, sizeof(uint64_t));
		_expirations = 0;
		COFIBER_RETURN(sizeof(uint64_t));
	}))
	
	COFIBER_ROUTINE(expected<PollResult>, poll(Process *, uint64_t in_seq,
			async::cancellation_token cancellation) override, ([=] {
		if(logTimerfd)
			std::cout << "posix: timerfd::poll(" << in_seq << ")" << std::endl;
		assert(in_seq <= _theSeq);
		while(in_seq == _theSeq && !cancellation.is_cancellation_requested()) {
			if(!isOpen())
				COFIBER_RETURN(Error::fileClosed);

			COFIBER_AWAIT _seqBell.async_wait(cancellation);
		}

		COFIBER_RETURN(PollResult(_theSeq, EPOLLIN, _expirations ? EPOLLIN : 0));
	}))

	helix::BorrowedDescriptor getPassthroughLane() override {
		return _passthrough;
	}

	void setTime(uint64_t initial, uint64_t interval) {
		auto current = std::exchange(_activeTimer, nullptr);
		if(current) {
			assert(current->asyncId);
			HEL_CHECK(helCancelAsync(helix::Dispatcher::global().acquire(), current->asyncId));
		}

		if(initial || interval) {
			_activeTimer = new Timer;
			_activeTimer->asyncId = 0;
			_activeTimer->initial = initial;
			_activeTimer->interval = interval;
			arm(_activeTimer);
		}
	}

private:
	helix::UniqueLane _passthrough;
	async::cancellation_event _cancelServe;
	bool _nonBlock;

	// Currently active timer.
	Timer *_activeTimer;

	// Number of expirations since last read().
	uint64_t _expirations;

	uint64_t _theSeq;
	async::doorbell _seqBell;
};

} // anonymous namespace

namespace timerfd {

smarter::shared_ptr<File, FileHandle> createFile(bool non_block) {
	auto file = smarter::make_shared<OpenFile>(non_block);
	file->setupWeakFile(file);
	OpenFile::serve(file);
	return File::constructHandle(std::move(file));
}

void setTime(File *file, struct timespec initial, struct timespec interval) {
	assert(initial.tv_sec >= 0 && initial.tv_nsec >= 0);
	assert(interval.tv_sec >= 0 && interval.tv_nsec >= 0);

	if(logTimerfd)
		std::cout << "setTime() initial: " << initial.tv_sec << " + " << initial.tv_nsec
				<< ", interval: " << interval.tv_sec << " + " << interval.tv_nsec << std::endl;

	// Note: __builtin_mul_overflow() with signed arguments requires a call to a
	// compiler-rt function for clang. Cast to unsigned to avoid this issue.

	uint64_t initial_nanos;
	if(__builtin_mul_overflow(static_cast<uint64_t>(initial.tv_sec), 1000000000, &initial_nanos)
			|| __builtin_add_overflow(initial.tv_nsec, initial_nanos, &initial_nanos))
		throw std::runtime_error("Overflow in timerfd setup");

	uint64_t interval_nanos;
	if(__builtin_mul_overflow(static_cast<uint64_t>(interval.tv_sec), 1000000000, &interval_nanos)
			|| __builtin_add_overflow(interval.tv_nsec, interval_nanos, &interval_nanos))
		throw std::runtime_error("Overflow in timerfd setup");

	auto timerfd = static_cast<OpenFile *>(file);
	timerfd->setTime(initial_nanos, interval_nanos);
}

} // namespace timerfd


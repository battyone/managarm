
#include <sys/epoll.h>

#include "file.hpp"

namespace epoll {

std::shared_ptr<File> createFile();

void addItem(File *epfile, File *file, int flags, uint64_t cookie);
void modifyItem(File *epfile, File *file, int flags, uint64_t cookie);
void deleteItem(File *epfile, File *file, int flags);

async::result<size_t> wait(File *epfile, struct epoll_event *events,
		size_t max_events);

} // namespace epoll


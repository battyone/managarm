
namespace thor {

extern frigg::util::LazyInitializer<ThreadQueue> scheduleQueue;

void switchThread(UnsafePtr<Thread, KernelAlloc> thread);

void doSchedule();

void enqueueInSchedule(SharedPtr<Thread, KernelAlloc> &&thread);

} // namespace thor

